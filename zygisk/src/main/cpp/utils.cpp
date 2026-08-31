#include "utils.h"

#include "ipc.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

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
        uintptr_t file_off = 0;
        char perms[8]{};
        char path[256]{};
        int n = sscanf(
            line,
            "%lx-%lx %7s %lx %*s %*s %255[^\n]",
            &start,
            &end,
            perms,
            &file_off,
            path
        );
        if (n < 3) {
            continue;
        }
        MemRange r;
        r.start = start;
        r.end = end;
        r.file_off = file_off;
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

const char* find_first_library_exec(
    const char* const* names,
    int count,
    uintptr_t* start,
    uintptr_t* end
) {
    if (names == nullptr || count <= 0) {
        return nullptr;
    }
    for (int i = 0; i < count; i++) {
        if (find_library_exec(names[i], start, end)) {
            return names[i];
        }
    }
    return nullptr;
}

bool find_elf_exec_section(
    const char* lib_name,
    const char* section,
    uintptr_t* start,
    uintptr_t* end
) {
    if (lib_name == nullptr || section == nullptr || start == nullptr || end == nullptr) {
        return false;
    }
    std::string path;
    uintptr_t bias = 0;
    bool have_bias = false;
    for (const auto& r : parse_maps()) {
        if (!r.exec || r.path.find(lib_name) == std::string::npos) {
            continue;
        }
        if (path.empty()) {
            path = r.path;
        }
        if (r.file_off == 0) {
            bias = r.start;
            have_bias = true;
            break;
        }
    }
    if (!have_bias || path.empty()) {
        return false;
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    unsigned char eh[64]{};
    if (fread(eh, 1, 64, f) != 64 || eh[0] != 0x7f || eh[1] != 'E') {
        fclose(f);
        return false;
    }
    uint64_t shoff = 0;
    uint16_t shentsize = 0, shnum = 0, shstrndx = 0;
    memcpy(&shoff, eh + 40, 8);
    memcpy(&shentsize, eh + 58, 2);
    memcpy(&shnum, eh + 60, 2);
    memcpy(&shstrndx, eh + 62, 2);
    if (shentsize < 40 || shnum == 0 || shstrndx >= shnum) {
        fclose(f);
        return false;
    }
    std::vector<unsigned char> sh(static_cast<size_t>(shentsize) * shnum);
    if (fseek(f, static_cast<long>(shoff), SEEK_SET) != 0 ||
        fread(sh.data(), shentsize, shnum, f) != shnum) {
        fclose(f);
        return false;
    }
    uint64_t str_off = 0, str_size = 0;
    memcpy(&str_off, sh.data() + static_cast<size_t>(shstrndx) * shentsize + 24, 8);
    memcpy(&str_size, sh.data() + static_cast<size_t>(shstrndx) * shentsize + 32, 8);
    if (str_size == 0 || str_size > 1u << 20) {
        fclose(f);
        return false;
    }
    std::vector<char> strtab(static_cast<size_t>(str_size));
    if (fseek(f, static_cast<long>(str_off), SEEK_SET) != 0 ||
        fread(strtab.data(), 1, strtab.size(), f) != strtab.size()) {
        fclose(f);
        return false;
    }
    fclose(f);
    for (uint16_t i = 0; i < shnum; i++) {
        const unsigned char* ent = sh.data() + static_cast<size_t>(i) * shentsize;
        uint32_t name_off = 0;
        memcpy(&name_off, ent, 4);
        if (name_off >= strtab.size()) {
            continue;
        }
        if (strcmp(strtab.data() + name_off, section) != 0) {
            continue;
        }
        uint64_t addr = 0, size = 0;
        memcpy(&addr, ent + 16, 8);
        memcpy(&size, ent + 32, 8);
        if (size < 8) {
            return false;
        }
        *start = bias + static_cast<uintptr_t>(addr);
        *end = *start + static_cast<uintptr_t>(size);
        return true;
    }
    return false;
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
