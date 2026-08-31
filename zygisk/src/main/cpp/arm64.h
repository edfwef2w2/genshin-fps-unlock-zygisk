#pragma once

#include <cstdint>

// ARM64 decode helpers. Compile-time checked against known encodings.

constexpr uint32_t kMovzW60_W0 = 0x52800780; // MOVZ W0, #0x3c

constexpr bool arm64_is_movz_w_imm(uint32_t insn, uint32_t imm, int* rd = nullptr) {
    // sf=0 opc=10 100101 hw imm16 Rd  -> MOVZ Wd, #imm
    if ((insn & 0xFF800000u) != 0x52800000u) {
        return false;
    }
    const uint32_t hw = (insn >> 21) & 0x3u;
    if (hw != 0) {
        return false;
    }
    const uint32_t imm16 = (insn >> 5) & 0xFFFFu;
    if (imm16 != imm) {
        return false;
    }
    if (rd) {
        *rd = static_cast<int>(insn & 0x1Fu);
    }
    return true;
}

constexpr bool arm64_is_bl(uint32_t insn) {
    return (insn & 0xFC000000u) == 0x94000000u;
}

constexpr bool arm64_is_b(uint32_t insn) {
    return (insn & 0xFC000000u) == 0x14000000u;
}

constexpr int64_t arm64_imm26_offset(uint32_t insn) {
    int32_t imm26 = static_cast<int32_t>(insn & 0x03FFFFFFu);
    if (imm26 & 0x02000000) {
        imm26 |= static_cast<int32_t>(0xFC000000);
    }
    return static_cast<int64_t>(imm26) * 4;
}

constexpr uintptr_t arm64_branch_target(uint32_t insn, uintptr_t pc) {
    return static_cast<uintptr_t>(static_cast<int64_t>(pc) + arm64_imm26_offset(insn));
}

constexpr bool arm64_is_adrp(uint32_t insn, int* rd, int64_t* page_off) {
    if ((insn & 0x9F000000u) != 0x90000000u) {
        return false;
    }
    const uint64_t immlo = (insn >> 29) & 0x3u;
    const uint64_t immhi = (insn >> 5) & 0x7FFFFu;
    int64_t imm = static_cast<int64_t>((immhi << 2) | immlo);
    if (imm & (1LL << 20)) {
        imm |= ~((1LL << 21) - 1);
    }
    if (rd) {
        *rd = static_cast<int>(insn & 0x1Fu);
    }
    if (page_off) {
        *page_off = imm << 12;
    }
    return true;
}

constexpr bool arm64_is_ldr_w_uoff(uint32_t insn, int* rt, int* rn, uint32_t* off) {
    if ((insn & 0xFFC00000u) != 0xB9400000u) {
        return false;
    }
    if (rt) {
        *rt = static_cast<int>(insn & 0x1Fu);
    }
    if (rn) {
        *rn = static_cast<int>((insn >> 5) & 0x1Fu);
    }
    if (off) {
        *off = ((insn >> 10) & 0xFFFu) * 4u;
    }
    return true;
}

constexpr bool arm64_is_str_w_uoff(uint32_t insn, int* rt, int* rn, uint32_t* off) {
    if ((insn & 0xFFC00000u) != 0xB9000000u) {
        return false;
    }
    if (rt) {
        *rt = static_cast<int>(insn & 0x1Fu);
    }
    if (rn) {
        *rn = static_cast<int>((insn >> 5) & 0x1Fu);
    }
    if (off) {
        *off = ((insn >> 10) & 0xFFFu) * 4u;
    }
    return true;
}

static_assert(arm64_is_movz_w_imm(kMovzW60_W0, 60));
static_assert(arm64_is_bl(0x94000000u));
static_assert(arm64_is_b(0x14000000u));
static_assert(arm64_branch_target(0x14000000u, 0x1000) == 0x1000);
