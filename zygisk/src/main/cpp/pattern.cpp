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

int32_t* find_global_from_window(uintptr_t addr, uintptr_t start, uintptr_t end) {
    // After the call chain, look for ADRP + LDR/STR W that references a writable int32.
    for (int i = 0; i < 48; i++) {
        uintptr_t pc = addr + static_cast<uintptr_t>(i) * 4;
        if (!in_range(pc, start, end) || !in_range(pc + 4, start, end)) {
            break;
        }
        uint32_t a = load_insn(pc);
        int rd = -1;
        int64_t page_off = 0;
        if (!arm64_is_adrp(a, &rd, &page_off)) {
            continue;
        }
        uint32_t b = load_insn(pc + 4);
        int rt = -1;
        int rn = -1;
        uint32_t off = 0;
        const bool ldr = arm64_is_ldr_w_uoff(b, &rt, &rn, &off);
        const bool str = !ldr && arm64_is_str_w_uoff(b, &rt, &rn, &off);
        if ((!ldr && !str) || rn != rd) {
            continue;
        }
        uintptr_t page = (pc & ~static_cast<uintptr_t>(0xFFF)) + static_cast<uintptr_t>(page_off);
        auto* ptr = reinterpret_cast<int32_t*>(page + off);
        if (is_address_writable(ptr)) {
            return ptr;
        }
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
        if (!arm64_is_movz_w_imm(insn, 60)) {
            continue;
        }
        uint32_t next = load_insn(pc + 4);
        if (!arm64_is_bl(next)) {
            continue;
        }

        uintptr_t call_target = arm64_branch_target(next, pc + 4);
        if (!in_range(call_target, start, end)) {
            continue;
        }
        // Windows filter: call destination starts with a JMP. ARM64 analogue: B.
        if (!arm64_is_b(load_insn(call_target))) {
            continue;
        }

        uintptr_t resolved = follow_branches(pc + 4, start, end, 16);
        if (resolved == 0) {
            continue;
        }
        int32_t* fps = find_global_from_window(resolved, start, end);
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
