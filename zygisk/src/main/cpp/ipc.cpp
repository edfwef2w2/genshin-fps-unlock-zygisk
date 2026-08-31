#include "ipc.h"

#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>

bool ipc_send(int fd, const IpcData& data) {
    if (fd < 0) {
        return false;
    }
    const auto* ptr = reinterpret_cast<const uint8_t*>(&data);
    size_t left = sizeof(data);
    while (left > 0) {
        ssize_t n = send(fd, ptr, left, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        ptr += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

bool ipc_recv(int fd, IpcData* data, bool blocking) {
    if (fd < 0 || data == nullptr) {
        return false;
    }
    auto* ptr = reinterpret_cast<uint8_t*>(data);
    size_t left = sizeof(*data);
    int flags = blocking ? 0 : MSG_DONTWAIT;
    while (left > 0) {
        ssize_t n = recv(fd, ptr, left, flags);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!blocking && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return false;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        ptr += n;
        left -= static_cast<size_t>(n);
        flags = 0;
    }
    return true;
}
