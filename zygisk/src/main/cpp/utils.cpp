#include "utils.h"

#include "ipc.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

std::string jstring_to_string(JNIEnv* env, jstring value) {
    if (env == nullptr || value == nullptr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return {};
    }
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

std::vector<MemRange> parse_maps() {
    std::vector<MemRange> ranges;
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) {
        return ranges;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        char perms[8]{};
        char path[256]{};
        int n = sscanf(line, "%lx-%lx %7s %*s %*s %*s %255[^\n]", &start, &end, perms, path);
        if (n < 3) {
            continue;
        }
        MemRange r;
        r.start = start;
        r.end = end;
        r.exec = perms[2] == 'x';
        r.write = perms[1] == 'w';
        if (n >= 4) {
            r.path = path;
            while (!r.path.empty() && (r.path.front() == ' ' || r.path.front() == '\t')) {
                r.path.erase(r.path.begin());
            }
        }
        ranges.push_back(std::move(r));
    }
    fclose(f);
    return ranges;
}

bool find_library_exec(const char* name, uintptr_t* start, uintptr_t* end) {
    if (name == nullptr || start == nullptr || end == nullptr) {
        return false;
    }
    auto ranges = parse_maps();
    uintptr_t lo = UINTPTR_MAX;
    uintptr_t hi = 0;
    bool found = false;
    for (const auto& r : ranges) {
        if (!r.exec || r.path.find(name) == std::string::npos) {
            continue;
        }
        found = true;
        lo = std::min(lo, r.start);
        hi = std::max(hi, r.end);
    }
    if (!found) {
        return false;
    }
    *start = lo;
    *end = hi;
    return true;
}

bool is_address_writable(const void* ptr) {
    auto addr = reinterpret_cast<uintptr_t>(ptr);
    for (const auto& r : parse_maps()) {
        if (addr >= r.start && addr < r.end) {
            return r.write;
        }
    }
    return false;
}

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int clamp_fps(int fps) {
    if (fps < kFpsMin) {
        return kFpsMin;
    }
    if (fps > kFpsMax) {
        return kFpsMax;
    }
    return fps;
}
