#include "pattern.h"

#include "arm64.h"
#include "log.h"
#include "utils.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

bool in_range(uintptr_t addr, uintptr_t start, uintptr_t end) {
    return addr >= start && addr + 4 <= end;
}

uint32_t load_mapped(uintptr_t addr) {
    return *reinterpret_cast<const uint32_t*>(addr);
}

constexpr uint32_t kArm64Ret = 0xD65F03C0u;

template <typename Load>
uintptr_t follow_branches(Load load, uintptr_t addr, uintptr_t start, uintptr_t end, int max_hops) {
    for (int i = 0; i < max_hops; i++) {
        if (!in_range(addr, start, end)) {
            return 0;
        }
        uint32_t insn = load(addr);
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

// set_targetFrameRate store: ADRP Xn; STR W0, [Xn,#off]; RET
template <typename Load>
int32_t* global_from_w0_store(
    Load load,
    uintptr_t callee,
    uintptr_t start,
    uintptr_t end,
    uintptr_t runtime_bias
) {
    if (!in_range(callee, start, end) || !in_range(callee + 8, start, end)) {
        return nullptr;
    }
    int rd = -1;
    int64_t page_off = 0;
    if (!arm64_is_adrp(load(callee), &rd, &page_off)) {
        return nullptr;
    }
    int rt = -1;
    int rn = -1;
    uint32_t off = 0;
    if (!arm64_is_str_w_uoff(load(callee + 4), &rt, &rn, &off) || rt != 0 || rn != rd) {
        return nullptr;
    }
    if (load(callee + 8) != kArm64Ret) {
        return nullptr;
    }
    uintptr_t runtime_callee = runtime_bias + callee;
    uintptr_t page = (runtime_callee & ~static_cast<uintptr_t>(0xFFF)) +
                     static_cast<uintptr_t>(page_off);
    auto* ptr = reinterpret_cast<int32_t*>(page + off);
    if (is_address_writable(ptr)) {
        return ptr;
    }
    return nullptr;
}

template <typename Load>
std::vector<ScanHit> scan_movz60_bl(Load load, uintptr_t start, uintptr_t end, uintptr_t runtime_bias) {
    std::vector<ScanHit> hits;
    if (end <= start + 8) {
        return hits;
    }
    for (uintptr_t pc = start; pc + 8 <= end && hits.size() < 8; pc += 4) {
        int rd = -1;
        if (!arm64_is_movz_w_imm(load(pc), 60, &rd) || rd != 0) {
            continue;
        }
        if (!arm64_is_bl(load(pc + 4))) {
            continue;
        }
        uintptr_t callee = follow_branches(load, pc + 4, start, end, 8);
        int32_t* fps = global_from_w0_store(load, callee, start, end, runtime_bias);
        if (fps == nullptr) {
            continue;
        }
        ScanHit hit;
        hit.address = runtime_bias + pc;
        hit.framerate = fps;
        hits.push_back(hit);
    }
    return hits;
}

} // namespace

std::vector<ScanHit> scan_framerate_candidates(uintptr_t start, uintptr_t end) {
    if (start == 0) {
        return {};
    }
    return scan_movz60_bl(load_mapped, start, end, 0);
}

std::vector<ScanHit> scan_framerate_file(
    const char* path,
    uintptr_t file_off,
    uintptr_t vaddr,
    uintptr_t size,
    uintptr_t bias
) {
    std::vector<ScanHit> hits;
    if (path == nullptr || size < 12) {
        return hits;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOGW("open %s failed", path);
        return hits;
    }
    off_t map_off = static_cast<off_t>(file_off) & ~static_cast<off_t>(4095);
    size_t pad = static_cast<size_t>(file_off - static_cast<uintptr_t>(map_off));
    size_t map_len = pad + static_cast<size_t>(size);
    void* mapped = mmap(nullptr, map_len, PROT_READ, MAP_PRIVATE, fd, map_off);
    close(fd);
    if (mapped == MAP_FAILED) {
        LOGW("mmap il2cpp file failed");
        return hits;
    }
    const auto* sec = reinterpret_cast<const uint8_t*>(mapped) + pad;
    auto load = [sec, vaddr](uintptr_t addr) -> uint32_t {
        return *reinterpret_cast<const uint32_t*>(sec + (addr - vaddr));
    };
    hits = scan_movz60_bl(load, vaddr, vaddr + size, bias);
    munmap(mapped, map_len);
    return hits;
}
