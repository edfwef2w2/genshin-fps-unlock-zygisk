#include "unlocker.h"

#include "log.h"
#include "pattern.h"
#include "utils.h"

#include <dlfcn.h>
#include <unistd.h>
#include <algorithm>
#include <thread>

namespace {

using Il2CppResolveICall = void* (*)(const char* name);
using SetInt32Fn = void (*)(int32_t);

struct UnityFps {
    SetInt32Fn set_target_frame_rate = nullptr;
    SetInt32Fn set_vsync_count = nullptr;
};

void drain_ipc(int fd, IpcData* ipc) {
    if (fd < 0) {
        return;
    }
    IpcData latest{};
    bool any = false;
    while (ipc_recv(fd, &latest, false)) {
        *ipc = latest;
        any = true;
    }
    (void)any;
}

bool wait_for_il2cpp(uintptr_t* start, uintptr_t* end, int timeout_ms) {
    const int step = 250;
    int waited = 0;
    while (waited < timeout_ms) {
        if (find_library_exec("libil2cpp.so", start, end)) {
            return true;
        }
        sleep_ms(step);
        waited += step;
    }
    return false;
}

UnityFps resolve_unity_fps() {
    UnityFps out;
    void* handle = dlopen("libil2cpp.so", RTLD_NOLOAD);
    if (handle == nullptr) {
        handle = dlopen("libil2cpp.so", RTLD_NOW);
    }
    if (handle == nullptr) {
        return out;
    }
    auto resolve = reinterpret_cast<Il2CppResolveICall>(dlsym(handle, "il2cpp_resolve_icall"));
    if (resolve == nullptr) {
        return out;
    }
    const int step = 250;
    for (int waited = 0; waited < 30000; waited += step) {
        out.set_target_frame_rate = reinterpret_cast<SetInt32Fn>(
            resolve("UnityEngine.Application::set_targetFrameRate(System.Int32)")
        );
        out.set_vsync_count = reinterpret_cast<SetInt32Fn>(
            resolve("UnityEngine.QualitySettings::set_vSyncCount(System.Int32)")
        );
        if (out.set_target_frame_rate != nullptr) {
            break;
        }
        sleep_ms(step);
    }
    return out;
}

bool is_process_foreground(JNIEnv* env) {
    if (env == nullptr) {
        return true;
    }
    jclass at = env->FindClass("android/app/ActivityThread");
    if (at == nullptr) {
        env->ExceptionClear();
        return true;
    }
    jmethodID currentApp = env->GetStaticMethodID(
        at, "currentApplication", "()Landroid/app/Application;"
    );
    if (currentApp == nullptr) {
        env->ExceptionClear();
        return true;
    }
    jobject app = env->CallStaticObjectMethod(at, currentApp);
    if (app == nullptr) {
        env->ExceptionClear();
        return true;
    }
    jclass ctx = env->FindClass("android/content/Context");
    jmethodID getSys = env->GetMethodID(
        ctx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"
    );
    jstring act = env->NewStringUTF("activity");
    jobject am = env->CallObjectMethod(app, getSys, act);
    env->DeleteLocalRef(act);
    if (am == nullptr) {
        env->ExceptionClear();
        return true;
    }
    jclass amCls = env->GetObjectClass(am);
    jmethodID getProcs = env->GetMethodID(
        amCls, "getRunningAppProcesses", "()Ljava/util/List;"
    );
    if (getProcs == nullptr) {
        env->ExceptionClear();
        return true;
    }
    jobject list = env->CallObjectMethod(am, getProcs);
    if (list == nullptr) {
        env->ExceptionClear();
        return true;
    }
    jclass listCls = env->FindClass("java/util/List");
    jmethodID sizeId = env->GetMethodID(listCls, "size", "()I");
    jmethodID getId = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jint size = env->CallIntMethod(list, sizeId);
    pid_t self = getpid();
    jclass infoCls = env->FindClass("android/app/ActivityManager$RunningAppProcessInfo");
    jfieldID pidF = env->GetFieldID(infoCls, "pid", "I");
    jfieldID impF = env->GetFieldID(infoCls, "importance", "I");
    // IMPORTANCE_FOREGROUND == 100
    for (jint i = 0; i < size; i++) {
        jobject info = env->CallObjectMethod(list, getId, i);
        if (info == nullptr) {
            continue;
        }
        jint pid = env->GetIntField(info, pidF);
        jint importance = env->GetIntField(info, impF);
        env->DeleteLocalRef(info);
        if (pid == self) {
            return importance <= 125; // FOREGROUND or FOREGROUND_SERVICE
        }
    }
    return true;
}

void apply_fps(int32_t* p_fps, const UnityFps& unity, int fps) {
    fps = clamp_fps(fps);
    if (p_fps) {
        *p_fps = fps;
    }
    if (unity.set_vsync_count) {
        unity.set_vsync_count(0);
    }
    if (unity.set_target_frame_rate) {
        unity.set_target_frame_rate(fps);
    }
}

void unlocker_thread(UnlockContext ctx) {
    LOGI("unlocker thread start, delay=%ds fd=%d", ctx.delay_seconds, ctx.companion_fd);

    uintptr_t start = 0;
    uintptr_t end = 0;
    if (!wait_for_il2cpp(&start, &end, 60000)) {
        LOGE("libil2cpp.so not found");
        ctx.ipc.status = IpcStatus::Error;
        ctx.ipc.mode = UnlockMode::Unknown;
        ctx.ipc.pid = getpid();
        ipc_send(ctx.companion_fd, ctx.ipc);
        return;
    }
    LOGI("libil2cpp.so %p-%p", reinterpret_cast<void*>(start), reinterpret_cast<void*>(end));

    if (ctx.delay_seconds > 0) {
        sleep_ms(ctx.delay_seconds * 1000);
    }

    drain_ipc(ctx.companion_fd, &ctx.ipc);

    int32_t* p_fps = nullptr;
    auto hits = scan_framerate_candidates(start, end);
    p_fps = pick_writable_framerate(hits);

    UnityFps unity{};
    if (p_fps) {
        ctx.ipc.mode = UnlockMode::Pattern;
        LOGI("using pattern framerate at %p", p_fps);
    } else {
        unity = resolve_unity_fps();
        if (unity.set_target_frame_rate == nullptr) {
            LOGE("failed to locate framerate (pattern and unity icall)");
            ctx.ipc.status = IpcStatus::Error;
            ctx.ipc.mode = UnlockMode::Unknown;
            ctx.ipc.pid = getpid();
            ipc_send(ctx.companion_fd, ctx.ipc);
            return;
        }
        ctx.ipc.mode = UnlockMode::UnityIcall;
        LOGI("using Unity set_targetFrameRate fallback");
    }

    ctx.ipc.status = IpcStatus::Ready;
    ctx.ipc.pid = getpid();
    ipc_send(ctx.companion_fd, ctx.ipc);

    JNIEnv* env = nullptr;
    if (ctx.vm) {
        ctx.vm->AttachCurrentThread(&env, nullptr);
    }

    while (true) {
        drain_ipc(ctx.companion_fd, &ctx.ipc);
        int fps = ctx.ipc.framerate;
        if (ctx.ipc.power_save && env && !is_process_foreground(env)) {
            fps = kFpsPowerSave;
        }
        apply_fps(p_fps, unity, fps);
        sleep_ms(62);
    }
}

} // namespace

void start_unlocker(UnlockContext ctx) {
    std::thread(unlocker_thread, ctx).detach();
}
