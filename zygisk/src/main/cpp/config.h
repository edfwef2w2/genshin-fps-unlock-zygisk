#pragma once

#include "ipc.h"

#include <string>
#include <vector>

struct ModuleConfig {
    int fps = 120;
    bool power_save = false;
    int delay_seconds = 8;
    std::vector<std::string> packages = {
        "com.miHoYo.Yuanshen",
        "com.miHoYo.GenshinImpact",
        "com.miHoYo.ys",
    };
};

ModuleConfig parse_config_json(const std::string& json);
bool read_config_from_fd(int dirfd, ModuleConfig* out);
bool read_config_from_path(const char* path, ModuleConfig* out);
bool is_target_package(const ModuleConfig& cfg, const char* name);
IpcData config_to_ipc(const ModuleConfig& cfg);
void write_status_file(const char* path, const IpcData& data, int pid, const char* message);
