#include "config.h"

#include "utils.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>

namespace {

std::string read_all_fd(int fd) {
    std::string out;
    char buf[4096];
    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            return {};
        }
        if (n == 0) {
            break;
        }
        out.append(buf, static_cast<size_t>(n));
    }
    return out;
}

bool find_key(const std::string& json, const char* key, size_t* value_pos) {
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    pos++;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        pos++;
    }
    *value_pos = pos;
    return true;
}

} // namespace

ModuleConfig parse_config_json(const std::string& json) {
    ModuleConfig cfg;
    if (json.empty()) {
        return cfg;
    }

    size_t pos = 0;
    if (find_key(json, "fps", &pos)) {
        cfg.fps = std::atoi(json.c_str() + pos);
    }
    if (find_key(json, "powerSave", &pos) || find_key(json, "power_save", &pos)) {
        cfg.power_save = json.compare(pos, 4, "true") == 0 || json[pos] == '1';
    }
    if (find_key(json, "delaySeconds", &pos) || find_key(json, "delay_seconds", &pos)) {
        cfg.delay_seconds = std::atoi(json.c_str() + pos);
    }

    if (find_key(json, "packages", &pos) && pos < json.size() && json[pos] == '[') {
        std::vector<std::string> pkgs;
        size_t i = pos + 1;
        while (i < json.size() && json[i] != ']') {
            if (json[i] == '"') {
                size_t end = json.find('"', i + 1);
                if (end == std::string::npos) {
                    break;
                }
                pkgs.emplace_back(json.substr(i + 1, end - i - 1));
                i = end + 1;
            } else {
                i++;
            }
        }
        if (!pkgs.empty()) {
            cfg.packages = std::move(pkgs);
        }
    }

    cfg.fps = std::clamp(cfg.fps, kFpsMin, kFpsMax);
    cfg.delay_seconds = std::clamp(cfg.delay_seconds, 0, 60);
    return cfg;
}

bool read_config_from_fd(int dirfd, ModuleConfig* out) {
    if (dirfd < 0 || out == nullptr) {
        return false;
    }
    int fd = openat(dirfd, "config.json", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    std::string json = read_all_fd(fd);
    close(fd);
    if (json.empty()) {
        return false;
    }
    *out = parse_config_json(json);
    return true;
}

bool read_config_from_path(const char* path, ModuleConfig* out) {
    if (path == nullptr || out == nullptr) {
        return false;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    std::string json = read_all_fd(fd);
    close(fd);
    if (json.empty()) {
        return false;
    }
    *out = parse_config_json(json);
    return true;
}

bool is_target_package(const ModuleConfig& cfg, const char* name) {
    if (name == nullptr) {
        return false;
    }
    for (const auto& pkg : cfg.packages) {
        if (pkg == name) {
            return true;
        }
    }
    return false;
}

IpcData config_to_ipc(const ModuleConfig& cfg) {
    IpcData data{};
    data.status = IpcStatus::None;
    data.framerate = cfg.fps;
    data.power_save = cfg.power_save ? 1 : 0;
    data.mode = UnlockMode::Unknown;
    data.pid = 0;
    return data;
}

void write_status_file(const char* path, const IpcData& data, int pid, const char* message) {
    if (path == nullptr) {
        return;
    }
    const char* status = "none";
    switch (data.status) {
        case IpcStatus::Ready:
            status = "ready";
            break;
        case IpcStatus::Error:
            status = "error";
            break;
        case IpcStatus::None:
        default:
            status = "none";
            break;
    }
    const char* mode = "unknown";
    switch (data.mode) {
        case UnlockMode::Pattern:
            mode = "pattern";
            break;
        case UnlockMode::UnityIcall:
            mode = "unity";
            break;
        default:
            break;
    }
    FILE* f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(
        f,
        "{\"status\":\"%s\",\"mode\":\"%s\",\"pid\":%d,\"fps\":%d,\"powerSave\":%s,\"message\":\"%s\"}\n",
        status,
        mode,
        pid,
        data.framerate,
        data.power_save ? "true" : "false",
        message ? message : ""
    );
    fclose(f);
}
