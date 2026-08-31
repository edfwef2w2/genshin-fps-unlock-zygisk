#pragma once

#include <cstdint>
#include <vector>

struct ScanHit {
    uintptr_t address = 0;
    int32_t* framerate = nullptr;
};

std::vector<ScanHit> scan_framerate_candidates(uintptr_t start, uintptr_t end);
std::vector<ScanHit> scan_framerate_file(
    const char* path,
    uintptr_t file_off,
    uintptr_t vaddr,
    uintptr_t size,
    uintptr_t bias
);
