#include "unlocker.h"

#include "arm64.h"
#include "log.h"
#include "pattern.h"
#include "utils.h"

#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <thread>
#include <vector>

namespace {

using Il2CppResolveICall = void* (*)(const char* name);
using SetInt32Fn = void (*)(int32_t);
using AnwSetFrameRateFn = int32_t (*)(void* window, float fps, int8_t compatibility);
using AnwSetFrameRate2Fn = int32_t (*)(void* window, float fps, int8_t compatibility, int8_t strategy);

struct UnityFps {
    SetInt32Fn set_target_frame_rate = nullptr;
    SetInt32Fn set_vsync_count = nullptr;
    SetInt32Fn set_render_interval = nullptr;
};

int32_t g_wanted_fps = 120;
AnwSetFrameRateFn g_orig_anw_set_fps = nullptr;
AnwSetFrameRate2Fn g_orig_anw_set_fps2 = nullptr;

void drain_ipc(int fd, IpcData* ipc) {
    if (fd < 0) {
        return;
    }
    IpcData latest{};
    while (ipc_recv(fd, &latest, false)) {
        *ipc = latest;
    }
}

// CN ships IL2CPP inside libyuanshen.so; older/global builds may still use libil2cpp.so.
constexpr const char* kGameLibs[] = {
    "libil2cpp.so",
    "libyuanshen.so",
    "libGenshinImpact.so",
};

void* try_dlopen(const char* name) {
    // RTLD_NOW on libyuanshen.so (330MB) can stall the unlocker thread in Zygisk.
    return dlopen(name, RTLD_NOLOAD);
}

void* open_game_lib(const char* preferred) {
    if (preferred != nullptr) {
        if (void* handle = try_dlopen(preferred)) {
            return handle;
        }
    }
    for (const char* name : kGameLibs) {
        if (void* handle = try_dlopen(name)) {
            return handle;
        }
    }
    return nullptr;
}

bool wait_for_game_lib(
    uintptr_t* start,
    uintptr_t* end,
    const char** name_out,
    int timeout_ms
) {
    const int step = 250;
    int waited = 0;
    while (waited < timeout_ms) {
        const char* name = find_first_library_exec(
            kGameLibs,
            static_cast<int>(sizeof(kGameLibs) / sizeof(kGameLibs[0])),
            start,
            end
        );
        if (name != nullptr) {
            if (name_out) {
                *name_out = name;
            }
            return true;
        }
        sleep_ms(step);
        waited += step;
    }
    return false;
}

void* find_resolve_icall(void* handle) {
    if (handle) {
        if (void* s = dlsym(handle, "il2cpp_resolve_icall")) {
            return s;
        }
    }
    for (const char* name : kGameLibs) {
        void* h = try_dlopen(name);
        if (h == nullptr) {
            continue;
        }
        if (void* s = dlsym(h, "il2cpp_resolve_icall")) {
            return s;
        }
    }
    return dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
}

UnityFps resolve_unity_fps(void* handle, bool wait) {
    UnityFps out;
    auto resolve = reinterpret_cast<Il2CppResolveICall>(find_resolve_icall(handle));
    if (resolve == nullptr) {
        return out;
    }
    const int step = 250;
    const int timeout = wait ? 30000 : 0;
    for (int waited = 0; waited <= timeout; waited += step) {
        out.set_target_frame_rate = reinterpret_cast<SetInt32Fn>(
            resolve("UnityEngine.Application::set_targetFrameRate(System.Int32)")
        );
        out.set_vsync_count = reinterpret_cast<SetInt32Fn>(
            resolve("UnityEngine.QualitySettings::set_vSyncCount(System.Int32)")
        );
        out.set_render_interval = reinterpret_cast<SetInt32Fn>(
            resolve("UnityEngine.Rendering.OnDemandRendering::set_renderFrameInterval(System.Int32)")
        );
        if (out.set_target_frame_rate != nullptr) {
            break;
        }
        if (waited >= timeout) {
            break;
        }
        sleep_ms(step);
    }
    return out;
}

bool insn_is_pc_rel(uint32_t insn) {
    if (arm64_is_b(insn) || arm64_is_bl(insn)) {
        return true;
    }
    if ((insn & 0x1F000000u) == 0x10000000u) {
        return true; // ADR / ADRP
    }
    if ((insn & 0xFF000000u) == 0x54000000u) {
        return true; // B.cond
    }
    if ((insn & 0x7E000000u) == 0x34000000u) {
        return true; // CBZ / CBNZ
    }
    if ((insn & 0x7E000000u) == 0x36000000u) {
        return true; // TBZ / TBNZ
    }
    if ((insn & 0x3B000000u) == 0x18000000u) {
        return true; // LDR literal
    }
    return false;
}

bool mprotect_range(void* addr, size_t len, int prot) {
    auto start = reinterpret_cast<uintptr_t>(addr) & ~static_cast<uintptr_t>(0xFFF);
    auto end = (reinterpret_cast<uintptr_t>(addr) + len + 0xFFFu) & ~static_cast<uintptr_t>(0xFFF);
    return mprotect(reinterpret_cast<void*>(start), end - start, prot) == 0;
}

void* make_abs_jump(void* from, void* to) {
    auto* stub = reinterpret_cast<uint32_t*>(from);
    stub[0] = 0x58000051u; // LDR X17, #8
    stub[1] = 0xD61F0220u; // BR X17
    auto dest = reinterpret_cast<uint64_t>(to);
    std::memcpy(stub + 2, &dest, sizeof(dest));
    return stub;
}

void* install_hook(void* target, void* replacement) {
    if (target == nullptr || replacement == nullptr) {
        return nullptr;
    }
    uint32_t ins[4]{};
    std::memcpy(ins, target, sizeof(ins));

    void* tramp = mmap(
        nullptr,
        4096,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );
    if (tramp == MAP_FAILED) {
        return nullptr;
    }
    auto* t = reinterpret_cast<uint32_t*>(tramp);
    if (arm64_is_b(ins[0]) && !arm64_is_bl(ins[0])) {
        auto resume = reinterpret_cast<void*>(arm64_branch_target(ins[0], reinterpret_cast<uintptr_t>(target)));
        make_abs_jump(t, resume);
    } else {
        bool safe = true;
        for (uint32_t insn : ins) {
            if (insn_is_pc_rel(insn)) {
                safe = false;
                break;
            }
        }
        if (!safe) {
            munmap(tramp, 4096);
            return nullptr;
        }
        std::memcpy(t, ins, sizeof(ins));
        make_abs_jump(t + 4, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(target) + 16));
    }
    __builtin___clear_cache(reinterpret_cast<char*>(tramp), reinterpret_cast<char*>(tramp) + 64);
    if (mprotect(tramp, 4096, PROT_READ | PROT_EXEC) != 0) {
        munmap(tramp, 4096);
        return nullptr;
    }

    if (!mprotect_range(target, 16, PROT_READ | PROT_WRITE | PROT_EXEC) &&
        !mprotect_range(target, 16, PROT_READ | PROT_WRITE)) {
        LOGW("mprotect hook target failed");
        munmap(tramp, 4096);
        return nullptr;
    }
    make_abs_jump(target, replacement);
    __builtin___clear_cache(reinterpret_cast<char*>(target), reinterpret_cast<char*>(target) + 16);
    mprotect_range(target, 16, PROT_READ | PROT_EXEC);
    return tramp;
}

int32_t hooked_anw_set_fps(void* window, float, int8_t compatibility) {
    float fps = static_cast<float>(g_wanted_fps);
    if (g_orig_anw_set_fps) {
        return g_orig_anw_set_fps(window, fps, compatibility);
    }
    return 0;
}

int32_t hooked_anw_set_fps2(void* window, float, int8_t compatibility, int8_t strategy) {
    float fps = static_cast<float>(g_wanted_fps);
    if (g_orig_anw_set_fps2) {
        return g_orig_anw_set_fps2(window, fps, compatibility, strategy);
    }
    return 0;
}

void hook_anative_window_fps() {
    void* anw = dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate");
    if (anw && g_orig_anw_set_fps == nullptr) {
        auto* tramp = reinterpret_cast<AnwSetFrameRateFn>(
            install_hook(anw, reinterpret_cast<void*>(&hooked_anw_set_fps))
        );
        if (tramp) {
            g_orig_anw_set_fps = tramp;
            LOGI("hooked ANativeWindow_setFrameRate at %p", anw);
        } else {
            LOGW("ANativeWindow_setFrameRate hook skipped");
        }
    }
    void* anw2 = dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRateWithChangeStrategy");
    if (anw2 && g_orig_anw_set_fps2 == nullptr) {
        auto* tramp = reinterpret_cast<AnwSetFrameRate2Fn>(
            install_hook(anw2, reinterpret_cast<void*>(&hooked_anw_set_fps2))
        );
        if (tramp) {
            g_orig_anw_set_fps2 = tramp;
            LOGI("hooked ANativeWindow_setFrameRateWithChangeStrategy at %p", anw2);
        }
    }
}

jclass load_app_class(JNIEnv* env, const char* name) {
    if (env == nullptr || name == nullptr) {
        return nullptr;
    }
    jclass at = env->FindClass("android/app/ActivityThread");
    if (at == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    jmethodID currentApp = env->GetStaticMethodID(
        at, "currentApplication", "()Landroid/app/Application;"
    );
    if (currentApp == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    jobject app = env->CallStaticObjectMethod(at, currentApp);
    if (app == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    jclass ctx = env->FindClass("android/content/Context");
    jmethodID getCl = env->GetMethodID(ctx, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject cl = env->CallObjectMethod(app, getCl);
    env->DeleteLocalRef(app);
    if (cl == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID load = env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jname = env->NewStringUTF(name);
    auto cls = reinterpret_cast<jclass>(env->CallObjectMethod(cl, load, jname));
    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(cl);
    if (cls == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    return cls;
}

struct AndroidFps {
    bool ready = false;
    jint sdk = 0;
    jclass unity_player = nullptr;
    jfieldID current_activity = nullptr;
    jmethodID get_window = nullptr;
    jmethodID get_display = nullptr;
    jclass window_cls = nullptr;
    jmethodID set_frame_rate2 = nullptr;
    jmethodID set_frame_rate3 = nullptr;
    jmethodID get_attr = nullptr;
    jmethodID set_attr = nullptr;
    jmethodID get_decor = nullptr;
    jclass lp_cls = nullptr;
    jfieldID pref_rate = nullptr;
    jfieldID pref_mode = nullptr;
    jclass view_cls = nullptr;
    jmethodID set_requested_fps = nullptr;
    jclass display_cls = nullptr;
    jmethodID get_modes = nullptr;
    jmethodID get_mode = nullptr;
    jclass mode_cls = nullptr;
    jmethodID mode_id = nullptr;
    jmethodID mode_w = nullptr;
    jmethodID mode_h = nullptr;
    jmethodID mode_fps = nullptr;
};

AndroidFps g_android;

jclass global_class(JNIEnv* env, jclass local) {
    if (local == nullptr) {
        return nullptr;
    }
    auto g = reinterpret_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return g;
}

void init_android_fps(JNIEnv* env) {
    if (g_android.ready || env == nullptr) {
        return;
    }
    env->PushLocalFrame(32);
    jclass ver = env->FindClass("android/os/Build$VERSION");
    if (ver) {
        jfieldID sdkF = env->GetStaticFieldID(ver, "SDK_INT", "I");
        if (sdkF) {
            g_android.sdk = env->GetStaticIntField(ver, sdkF);
        }
    }
    env->ExceptionClear();

    jclass up = load_app_class(env, "com.unity3d.player.UnityPlayer");
    if (up == nullptr) {
        LOGW("UnityPlayer class not found");
        env->PopLocalFrame(nullptr);
        return;
    }
    g_android.unity_player = global_class(env, up);
    g_android.current_activity = env->GetStaticFieldID(
        g_android.unity_player, "currentActivity", "Landroid/app/Activity;"
    );
    if (g_android.current_activity == nullptr) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return;
    }

    jclass act = env->FindClass("android/app/Activity");
    if (act == nullptr) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return;
    }
    g_android.get_window = env->GetMethodID(act, "getWindow", "()Landroid/view/Window;");
    g_android.get_display = env->GetMethodID(act, "getDisplay", "()Landroid/view/Display;");
    env->ExceptionClear();

    g_android.window_cls = global_class(env, env->FindClass("android/view/Window"));
    env->ExceptionClear();
    if (g_android.window_cls == nullptr) {
        env->PopLocalFrame(nullptr);
        return;
    }
    g_android.get_attr = env->GetMethodID(
        g_android.window_cls, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;"
    );
    g_android.set_attr = env->GetMethodID(
        g_android.window_cls, "setAttributes", "(Landroid/view/WindowManager$LayoutParams;)V"
    );
    g_android.get_decor = env->GetMethodID(
        g_android.window_cls, "getDecorView", "()Landroid/view/View;"
    );
    g_android.set_frame_rate3 = env->GetMethodID(g_android.window_cls, "setFrameRate", "(FII)V");
    env->ExceptionClear();
    if (g_android.set_frame_rate3 == nullptr) {
        g_android.set_frame_rate2 = env->GetMethodID(g_android.window_cls, "setFrameRate", "(FI)V");
        env->ExceptionClear();
    }

    g_android.lp_cls = global_class(env, env->FindClass("android/view/WindowManager$LayoutParams"));
    env->ExceptionClear();
    if (g_android.lp_cls) {
        g_android.pref_rate = env->GetFieldID(g_android.lp_cls, "preferredRefreshRate", "F");
        env->ExceptionClear();
        g_android.pref_mode = env->GetFieldID(g_android.lp_cls, "preferredDisplayModeId", "I");
        env->ExceptionClear();
    }

    g_android.view_cls = global_class(env, env->FindClass("android/view/View"));
    env->ExceptionClear();
    if (g_android.view_cls) {
        g_android.set_requested_fps = env->GetMethodID(g_android.view_cls, "setRequestedFrameRate", "(F)V");
        env->ExceptionClear();
    }

    g_android.display_cls = global_class(env, env->FindClass("android/view/Display"));
    env->ExceptionClear();
    if (g_android.display_cls) {
        g_android.get_modes = env->GetMethodID(
            g_android.display_cls, "getSupportedModes", "()[Landroid/view/Display$Mode;"
        );
        g_android.get_mode = env->GetMethodID(
            g_android.display_cls, "getMode", "()Landroid/view/Display$Mode;"
        );
        env->ExceptionClear();
    }
    g_android.mode_cls = global_class(env, env->FindClass("android/view/Display$Mode"));
    env->ExceptionClear();
    if (g_android.mode_cls) {
        g_android.mode_id = env->GetMethodID(g_android.mode_cls, "getModeId", "()I");
        g_android.mode_w = env->GetMethodID(g_android.mode_cls, "getPhysicalWidth", "()I");
        g_android.mode_h = env->GetMethodID(g_android.mode_cls, "getPhysicalHeight", "()I");
        g_android.mode_fps = env->GetMethodID(g_android.mode_cls, "getRefreshRate", "()F");
        env->ExceptionClear();
    }

    g_android.ready = g_android.get_window != nullptr && g_android.window_cls != nullptr;
    LOGI(
        "android fps api sdk=%d window.setFrameRate=%d view.setRequestedFrameRate=%d",
        g_android.sdk,
        (g_android.set_frame_rate3 || g_android.set_frame_rate2) ? 1 : 0,
        g_android.set_requested_fps ? 1 : 0
    );
    env->PopLocalFrame(nullptr);
}

jint pick_display_mode(JNIEnv* env, jobject display, int fps) {
    if (display == nullptr || g_android.get_modes == nullptr || g_android.get_mode == nullptr ||
        g_android.mode_w == nullptr || g_android.mode_id == nullptr || g_android.mode_fps == nullptr) {
        return -1;
    }
    jobject cur = env->CallObjectMethod(display, g_android.get_mode);
    if (cur == nullptr) {
        env->ExceptionClear();
        return -1;
    }
    jint cw = env->CallIntMethod(cur, g_android.mode_w);
    jint ch = env->CallIntMethod(cur, g_android.mode_h);
    jobjectArray modes = reinterpret_cast<jobjectArray>(
        env->CallObjectMethod(display, g_android.get_modes)
    );
    if (modes == nullptr) {
        env->ExceptionClear();
        return -1;
    }
    const float want = static_cast<float>(fps);
    jint best_id = -1;
    float best_err = 1e9f;
    jsize n = env->GetArrayLength(modes);
    for (jsize i = 0; i < n; i++) {
        jobject m = env->GetObjectArrayElement(modes, i);
        if (m == nullptr) {
            continue;
        }
        jint w = env->CallIntMethod(m, g_android.mode_w);
        jint h = env->CallIntMethod(m, g_android.mode_h);
        if (w != cw || h != ch) {
            env->DeleteLocalRef(m);
            continue;
        }
        float r = env->CallFloatMethod(m, g_android.mode_fps);
        float err = r > want ? (r - want) : (want - r);
        if (err < best_err) {
            best_err = err;
            best_id = env->CallIntMethod(m, g_android.mode_id);
        }
        env->DeleteLocalRef(m);
    }
    return best_id;
}

void apply_android_fps(JNIEnv* env, int fps) {
    if (env == nullptr) {
        return;
    }
    if (!g_android.ready) {
        init_android_fps(env);
        if (!g_android.ready) {
            return;
        }
    }
    env->PushLocalFrame(24);
    jobject activity = env->GetStaticObjectField(g_android.unity_player, g_android.current_activity);
    if (activity == nullptr) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return;
    }
    jobject window = env->CallObjectMethod(activity, g_android.get_window);
    if (window == nullptr) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return;
    }
    const float rate = static_cast<float>(fps);
    constexpr jint kFixedSource = 1;
    constexpr jint kChangeAlways = 1;
    if (g_android.set_frame_rate3) {
        env->CallVoidMethod(window, g_android.set_frame_rate3, rate, kFixedSource, kChangeAlways);
    } else if (g_android.set_frame_rate2) {
        env->CallVoidMethod(window, g_android.set_frame_rate2, rate, kFixedSource);
    }
    env->ExceptionClear();

    if (g_android.set_requested_fps && g_android.get_decor) {
        jobject decor = env->CallObjectMethod(window, g_android.get_decor);
        if (decor) {
            env->CallVoidMethod(decor, g_android.set_requested_fps, rate);
            env->ExceptionClear();
        }
    }

    jobject display = nullptr;
    if (g_android.get_display) {
        display = env->CallObjectMethod(activity, g_android.get_display);
        env->ExceptionClear();
    }
    jint mode_id = pick_display_mode(env, display, fps);
    static jint last_mode_id = -1;
    if (g_android.get_attr && g_android.set_attr && mode_id > 0 && mode_id != last_mode_id) {
        jobject lp = env->CallObjectMethod(window, g_android.get_attr);
        if (lp) {
            if (g_android.pref_rate) {
                env->SetFloatField(lp, g_android.pref_rate, rate);
            }
            if (g_android.pref_mode) {
                env->SetIntField(lp, g_android.pref_mode, mode_id);
            }
            env->CallVoidMethod(window, g_android.set_attr, lp);
            env->ExceptionClear();
            last_mode_id = mode_id;
        }
    }
    env->PopLocalFrame(nullptr);
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
    for (jint i = 0; i < size; i++) {
        jobject info = env->CallObjectMethod(list, getId, i);
        if (info == nullptr) {
            continue;
        }
        jint pid = env->GetIntField(info, pidF);
        jint importance = env->GetIntField(info, impF);
        env->DeleteLocalRef(info);
        if (pid == self) {
            return importance <= 125;
        }
    }
    return true;
}

void collect_fps_ptrs(const std::vector<ScanHit>& hits, std::vector<int32_t*>* out) {
    for (const auto& hit : hits) {
        if (hit.framerate == nullptr || !is_address_writable(hit.framerate)) {
            continue;
        }
        bool dup = false;
        for (int32_t* p : *out) {
            if (p == hit.framerate) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        LOGI(
            "pattern hit at %p -> fps@%p val=%d",
            reinterpret_cast<void*>(hit.address),
            hit.framerate,
            *hit.framerate
        );
        out->push_back(hit.framerate);
    }
}

void apply_fps(
    const std::vector<int32_t*>& ptrs,
    const UnityFps& unity,
    JNIEnv* env,
    int fps
) {
    fps = clamp_fps(fps);
    g_wanted_fps = fps;
    for (int32_t* p : ptrs) {
        if (p) {
            *p = fps;
        }
    }
    if (unity.set_vsync_count) {
        unity.set_vsync_count(0);
    }
    if (unity.set_render_interval) {
        unity.set_render_interval(1);
    }
    if (unity.set_target_frame_rate) {
        unity.set_target_frame_rate(fps);
    }
    apply_android_fps(env, fps);
}

void unlocker_thread(UnlockContext ctx) {
    pthread_setname_np(pthread_self(), "UnlockFPS");
    LOGI("unlocker thread start, delay=%ds fd=%d", ctx.delay_seconds, ctx.companion_fd);

    uintptr_t start = 0;
    uintptr_t end = 0;
    const char* lib_name = nullptr;
    if (!wait_for_game_lib(&start, &end, &lib_name, 60000)) {
        LOGE("game IL2CPP library not found");
        ctx.ipc.status = IpcStatus::Error;
        ctx.ipc.mode = UnlockMode::Unknown;
        ctx.ipc.pid = getpid();
        ipc_send(ctx.companion_fd, ctx.ipc);
        return;
    }
    LOGI(
        "game lib %s %p-%p",
        lib_name ? lib_name : "?",
        reinterpret_cast<void*>(start),
        reinterpret_cast<void*>(end)
    );

    if (ctx.delay_seconds > 0) {
        sleep_ms(ctx.delay_seconds * 1000);
    }

    drain_ipc(ctx.companion_fd, &ctx.ipc);

    std::vector<int32_t*> fps_ptrs;
    ElfExecSection sec;
    if (find_elf_exec_section(lib_name, "il2cpp", &sec)) {
        LOGI(
            "il2cpp file scan %zu MB off=0x%zx",
            static_cast<size_t>(sec.size / (1024 * 1024)),
            static_cast<size_t>(sec.file_off)
        );
        auto hits = scan_framerate_file(
            sec.path.c_str(),
            sec.file_off,
            sec.vaddr,
            sec.size,
            sec.bias
        );
        collect_fps_ptrs(hits, &fps_ptrs);
        if (fps_ptrs.empty()) {
            hits = scan_framerate_candidates(sec.bias + sec.vaddr, sec.bias + sec.vaddr + sec.size);
            collect_fps_ptrs(hits, &fps_ptrs);
        }
        if (!fps_ptrs.empty()) {
            ctx.ipc.mode = UnlockMode::Pattern;
            LOGI("using ARM set_targetFrameRate store at %p", fps_ptrs.front());
        }
    } else {
        LOGW("ELF il2cpp section not found in %s", lib_name ? lib_name : "?");
    }

    void* handle = open_game_lib(lib_name);
    UnityFps unity = resolve_unity_fps(handle, true);
    if (unity.set_target_frame_rate) {
        LOGI(
            "unity icall set_targetFrameRate=%p set_vSyncCount=%p interval=%p",
            reinterpret_cast<void*>(unity.set_target_frame_rate),
            reinterpret_cast<void*>(unity.set_vsync_count),
            reinterpret_cast<void*>(unity.set_render_interval)
        );
        if (fps_ptrs.empty()) {
            ctx.ipc.mode = UnlockMode::UnityIcall;
            LOGI("using Unity set_targetFrameRate fallback");
        }
    }
    if (fps_ptrs.empty() && unity.set_target_frame_rate == nullptr) {
        LOGE("failed to locate framerate (pattern and unity icall)");
        ctx.ipc.status = IpcStatus::Error;
        ctx.ipc.mode = UnlockMode::Unknown;
        ctx.ipc.pid = getpid();
        ipc_send(ctx.companion_fd, ctx.ipc);
        return;
    }

    hook_anative_window_fps();

    ctx.ipc.status = IpcStatus::Ready;
    ctx.ipc.pid = getpid();
    ipc_send(ctx.companion_fd, ctx.ipc);

    JNIEnv* env = nullptr;
    if (ctx.vm) {
        ctx.vm->AttachCurrentThread(&env, nullptr);
    }
    if (env) {
        init_android_fps(env);
    }

    while (true) {
        drain_ipc(ctx.companion_fd, &ctx.ipc);
        int fps = ctx.ipc.framerate;
        if (ctx.ipc.power_save && env && !is_process_foreground(env)) {
            fps = kFpsPowerSave;
        }
        apply_fps(fps_ptrs, unity, env, fps);
        sleep_ms(16);
    }
}

} // namespace

void start_unlocker(UnlockContext ctx) {
    std::thread(unlocker_thread, ctx).detach();
}
