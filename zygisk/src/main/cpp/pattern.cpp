#include "pattern.h"

#include "arm64.h"
#include "log.h"
#include "utils.h"

namespace {

bool in_range(uintptr_t addr, uintptr_t start, uintptr_t end) {
    return addr >= start && addr + 4 <= end;
}

uint32_t load_insn(uintptr_t addr) {
    return *reinterpret_cast<const uint32_t*>(addr);
}

uintptr_t follow_branches(uintptr_t addr, uintptr_t start, uintptr_t end, int max_hops) {
    for (int i = 0; i < max_hops; i++) {
        if (!in_range(addr, start, end)) {
            return 0;
        }
        uint32_t insn = load_insn(addr);
        if (arm64_is_bl(insn) || arm64_is_b(insn)) {
            uintptr_t target = arm64_branch_target(insn, addr);
            if (target == addr) {
                break;
            }
            addr = target;
            continue;
        }
        break;
    }
    return addr;
}

constexpr uint32_t kArm64Ret = 0xD65F03C0u;

int32_t* global_from_w0_store(uintptr_t callee, uintptr_t start, uintptr_t end) {
    if (!in_range(callee, start, end) || !in_range(callee + 8, start, end)) {
        return nullptr;
    }
    int rd = -1;
    int64_t page_off = 0;
    if (!arm64_is_adrp(load_insn(callee), &rd, &page_off)) {
        return nullptr;
    }
    int rt = -1;
    int rn = -1;
    uint32_t off = 0;
    if (!arm64_is_str_w_uoff(load_insn(callee + 4), &rt, &rn, &off) || rt != 0 || rn != rd) {
        return nullptr;
    }
    if (load_insn(callee + 8) != kArm64Ret) {
        return nullptr;
    }
    uintptr_t page = (callee & ~static_cast<uintptr_t>(0xFFF)) + static_cast<uintptr_t>(page_off);
    auto* ptr = reinterpret_cast<int32_t*>(page + off);
    if (is_address_writable(ptr)) {
        return ptr;
    }
    return nullptr;
}

} // namespace

std::vector<ScanHit> scan_framerate_candidates(uintptr_t start, uintptr_t end) {
    std::vector<ScanHit> hits;
    if (start == 0 || end <= start + 8) {
        return hits;
    }

    for (uintptr_t pc = start; pc + 8 <= end; pc += 4) {
        uint32_t insn = load_insn(pc);
        int rd = -1;
        if (!arm64_is_movz_w_imm(insn, 60, &rd) || rd != 0) {
            continue;
        }
        uint32_t next = load_insn(pc + 4);
        if (!arm64_is_bl(next)) {
            continue;
        }
        uintptr_t callee = follow_branches(pc + 4, start, end, 8);
        int32_t* fps = global_from_w0_store(callee, start, end);
        if (fps == nullptr) {
            continue;
        }
        ScanHit hit;
        hit.address = pc;
        hit.framerate = fps;
        hits.push_back(hit);
        if (hits.size() >= 8) {
            break;
        }
    }
    return hits;
}

int32_t* pick_writable_framerate(const std::vector<ScanHit>& hits) {
    for (const auto& hit : hits) {
        if (hit.framerate && is_address_writable(hit.framerate)) {
            LOGI("pattern hit at %p -> fps@%p", reinterpret_cast<void*>(hit.address), hit.framerate);
            return hit.framerate;
        }
    }
    return nullptr;
}
