#pragma once

#include <cstdint>
#include <vector>

struct ScanHit {
    uintptr_t address = 0;
    int32_t* framerate = nullptr;
};

std::vector<ScanHit> scan_framerate_candidates(uintptr_t start, uintptr_t end);
int32_t* pick_writable_framerate(const std::vector<ScanHit>& hits);
