#pragma once

#include <cstdint>

enum class IpcStatus : int32_t {
    None = 0,
    Error = 1,
    Ready = 2,
};

enum class UnlockMode : int32_t {
    Unknown = 0,
    Pattern = 1,
    UnityIcall = 2,
};

struct IpcData {
    IpcStatus status;
    int32_t framerate;
    int32_t power_save;
    UnlockMode mode;
    int32_t pid;
};

static constexpr int kFpsMin = 10;
static constexpr int kFpsMax = 240;
static constexpr int kFpsPowerSave = 10;

bool ipc_send(int fd, const IpcData& data);
bool ipc_recv(int fd, IpcData* data, bool blocking);
