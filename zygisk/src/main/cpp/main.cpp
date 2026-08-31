#include "config.h"
#include "ipc.h"
#include "log.h"
#include "unlocker.h"
#include "utils.h"
#include "zygisk.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstring>
#include <string>

using zygisk::Api;
using zygisk::AppSpecializeArgs;

namespace {

constexpr const char* kModuleId = "genshin_fps_unlock";
constexpr const char* kConfigPath = "/data/adb/modules/genshin_fps_unlock/config.json";
constexpr const char* kStatusPath = "/data/adb/modules/genshin_fps_unlock/status.json";

JavaVM* g_vm = nullptr;

std::string package_from_args(JNIEnv* env, AppSpecializeArgs* args) {
    if (env == nullptr || args == nullptr || args->nice_name == nullptr) {
        return {};
    }
    return jstring_to_string(env, args->nice_name);
}

class UnlockModule : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        api_ = api;
        env_ = env;
        if (env) {
            env->GetJavaVM(&g_vm);
        }
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        if (args->is_child_zygote && *args->is_child_zygote) {
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        ModuleConfig cfg;
        int dirfd = api_->getModuleDir();
        if (dirfd >= 0) {
            read_config_from_fd(dirfd, &cfg);
            close(dirfd);
        } else {
            read_config_from_path(kConfigPath, &cfg);
        }

        const std::string pkg = package_from_args(env_, args);
        if (!is_target_package(cfg, pkg.c_str())) {
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        LOGI("target process: %s", pkg.c_str());
        ipc_ = config_to_ipc(cfg);
        delay_seconds_ = cfg.delay_seconds;

        companion_fd_ = api_->connectCompanion();
        if (companion_fd_ >= 0) {
            if (!api_->exemptFd(companion_fd_)) {
                LOGW("exemptFd failed, live config updates will not work");
                close(companion_fd_);
                companion_fd_ = -1;
            }
        } else {
            LOGW("connectCompanion failed, using snapshot config only");
        }

        target_ = true;
    }

    void postAppSpecialize(const AppSpecializeArgs* args) override {
        (void)args;
        if (!target_) {
            return;
        }
        UnlockContext ctx;
        ctx.vm = g_vm;
        ctx.companion_fd = companion_fd_;
        ctx.ipc = ipc_;
        ctx.delay_seconds = delay_seconds_;
        start_unlocker(ctx);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs* args) override {
        (void)args;
        api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api* api_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool target_ = false;
    int companion_fd_ = -1;
    int delay_seconds_ = 8;
    IpcData ipc_{};
};

void companion_handler(int client) {
    ModuleConfig cfg;
    if (!read_config_from_path(kConfigPath, &cfg)) {
        LOGI("companion: using default config");
    }
    IpcData snapshot = config_to_ipc(cfg);
    if (!ipc_send(client, snapshot)) {
        close(client);
        return;
    }

    int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    int watch = -1;
    if (inotify_fd >= 0) {
        watch = inotify_add_watch(
            inotify_fd,
            "/data/adb/modules/genshin_fps_unlock",
            IN_CLOSE_WRITE | IN_MOVED_TO | IN_MODIFY | IN_CREATE
        );
    }

    char inotify_buf[1024];
    while (true) {
        pollfd fds[2]{};
        fds[0].fd = client;
        fds[0].events = POLLIN | POLLHUP | POLLERR;
        int nfds = 1;
        if (inotify_fd >= 0) {
            fds[1].fd = inotify_fd;
            fds[1].events = POLLIN;
            nfds = 2;
        }
        int pr = poll(fds, nfds, 1000);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (fds[0].revents & (POLLHUP | POLLERR)) {
            break;
        }
        if (fds[0].revents & POLLIN) {
            IpcData from_game{};
            if (!ipc_recv(client, &from_game, true)) {
                break;
            }
            write_status_file(
                kStatusPath,
                from_game,
                from_game.pid,
                from_game.status == IpcStatus::Error ? "unlock failed" : ""
            );
        }

        bool reload = pr == 0;
        if (nfds > 1 && (fds[1].revents & POLLIN)) {
            ssize_t n = read(inotify_fd, inotify_buf, sizeof(inotify_buf));
            ssize_t off = 0;
            while (n > 0 && off < n) {
                auto* ev = reinterpret_cast<inotify_event*>(inotify_buf + off);
                if (ev->len == 0 || std::strstr(ev->name, "config.json") != nullptr) {
                    reload = true;
                }
                off += static_cast<ssize_t>(sizeof(inotify_event) + ev->len);
            }
        }
        if (reload) {
            ModuleConfig updated;
            if (read_config_from_path(kConfigPath, &updated)) {
                IpcData next = config_to_ipc(updated);
                if (!ipc_send(client, next)) {
                    break;
                }
            }
        }
    }

    IpcData gone{};
    gone.status = IpcStatus::None;
    write_status_file(kStatusPath, gone, 0, "game exited");

    if (watch >= 0 && inotify_fd >= 0) {
        inotify_rm_watch(inotify_fd, watch);
    }
    if (inotify_fd >= 0) {
        close(inotify_fd);
    }
    close(client);
    (void)kModuleId;
}

} // namespace

REGISTER_ZYGISK_MODULE(UnlockModule)
REGISTER_ZYGISK_COMPANION(companion_handler)
