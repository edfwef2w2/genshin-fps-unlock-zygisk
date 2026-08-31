#pragma once

#include "ipc.h"

#include <jni.h>

struct UnlockContext {
    JavaVM* vm = nullptr;
    int companion_fd = -1;
    IpcData ipc{};
    int delay_seconds = 8;
};

void start_unlocker(UnlockContext ctx);
