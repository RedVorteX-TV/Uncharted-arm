// Uncharted ARM port: scripts/render_720p.py in C++ (the same sites, the same BBPATCH2 bytes).
// The collection's engine keeps its video settings in one structure: two default setups and a mode
// switch whose 1080p case sets every size field (scene, UI, viewport) to 1920x1080; the PS4's 720p
// case already exists. The defaults get the 720p values (1280x720, and 160x180 for 0x40/0x44) and the
// 1080p case jumps to the 720p case, so the game allocates and renders everything at 720p, movies and
// menus included. The sites are found by their instruction bytes, the same in U1, U2 and U3.
#include "render720.h"

#include <cstring>
#include <map>

namespace {
constexpr u32 WIDTH = 1920, HEIGHT = 1080, NEW_WIDTH = 1280, NEW_HEIGHT = 720;
constexpr size_t WINDOW = 0x90;
constexpr size_t MAX_SITES = 3;

bool modrm(u8 b) { return b >= 0x40 && b <= 0x47 && b != 0x44; }
u32 le32(const u8* p) { u32 v; std::memcpy(&v, p, 4); return v; }
u64 le64(const u8* p) { u64 v; std::memcpy(&v, p, 8); return v; }

// MOVL = \x41?\xc7[modrm] followed by `tail` (disp8 + imm32): the end of the match, or 0.
size_t movl(const u8* e, size_t n, size_t p, const u8* tail, size_t tail_n) {
    size_t q = p;
    if (q < n && e[q] == 0x41) q++;  // greedy optional REX.B; nothing else can start with 0x41 here
    if (q + 2 + tail_n > n || e[q] != 0xc7 || !modrm(e[q + 1])) return 0;
    if (std::memcmp(e + q + 2, tail, tail_n)) return 0;
    return q + 2 + tail_n;
}

// SITE: movl $1920, 0x8(%reg); movl $1080, 0xc(%reg). finditer: leftmost, non-overlapping.
std::vector<size_t> sites(const u8* e, size_t n) {
    static const u8 a[] = {0x08, 0x80, 0x07, 0x00, 0x00}, b[] = {0x0c, 0x38, 0x04, 0x00, 0x00};
    std::vector<size_t> out;
    for (size_t p = 0; p < n;) {
        size_t m = movl(e, n, p, a, sizeof a);
        size_t end = m ? movl(e, n, m, b, sizeof b) : 0;
        if (end) {
            out.push_back(p);
            p = end;
        } else p++;
    }
    return out;
}

// DECODER: c7 44 24 X 80 07 00 00 c7 44 24 Y 40 04 00 00 (non-overlapping), kept where Y == X + 4.
std::vector<size_t> decoders(const u8* e, size_t n) {
    std::vector<size_t> out;
    for (size_t p = 0; p + 16 <= n;) {
        const u8* q = e + p;
        if (q[0] == 0xc7 && q[1] == 0x44 && q[2] == 0x24 && le32(q + 4) == 1920 && q[8] == 0xc7 &&
            q[9] == 0x44 && q[10] == 0x24 && le32(q + 12) == 1088) {
            if ((int)q[11] == (int)q[3] + 4) out.push_back(p);
            p += 16;
        } else p++;
    }
    return out;
}
}  // namespace

std::vector<std::pair<u64, Bytes>> render_720p_writes(const Bytes& elf_bytes, std::string& error) {
    const u8* e = elf_bytes.data();
    const size_t n = elf_bytes.size();
    std::vector<std::pair<u64, Bytes>> out;
    if (n < 0x40) { error = "not an ELF image"; return {}; }
    const u64 phoff = le64(e + 0x20);
    if (phoff + 56 > n) { error = "not an ELF image"; return {}; }
    const i64 base = (i64)le64(e + phoff + 8) - (i64)le64(e + phoff + 16);
    auto put = [&](size_t at, std::initializer_list<u8> bytes) {
        out.push_back({(u64)((i64)at - base), Bytes(bytes)});
    };
    auto put32 = [&](size_t at, u32 v) {
        u8 b[4]; std::memcpy(b, &v, 4);
        out.push_back({(u64)((i64)at - base), Bytes(b, b + 4)});
    };
    const std::vector<size_t> found = sites(e, n);
    if (found.empty() || found.size() > MAX_SITES) {
        error = "expected 1 to 3 video setting sites, found " + std::to_string(found.size());
        return {};
    }
    for (size_t index = 0; index < found.size(); index++) {
        const size_t site = found[index];
        size_t end = index + 1 < found.size() ? found[index + 1] - 9 : n;
        end = std::min(site + WINDOW, end);
        // cmp $1, %e?x; jne <next mode>: the 1080p case jumps to the 720p case instead.
        if (site >= 9 && e[site - 9] == 0x83 && e[site - 8] >= 0xf8 && e[site - 7] == 0x01 &&
            e[site - 6] == 0x0f && e[site - 5] == 0x85) {
            int32_t rel;
            std::memcpy(&rel, e + site - 4, 4);
            rel += 1;  // jmp rel32 is one byte shorter
            u8 r[4]; std::memcpy(r, &rel, 4);
            put(site - 6, {0xe9, r[0], r[1], r[2], r[3], 0x90});
            continue;
        }
        for (size_t at = site; at < end; at++) {
            if (at + 4 > n) { error = "unpack_from requires a buffer of at least 4 bytes"; return {}; }
            const u32 value = le32(e + at);
            u32 replacement;
            if (value == WIDTH) replacement = NEW_WIDTH;
            else if (value == HEIGHT) replacement = NEW_HEIGHT;
            else if (value == WIDTH / 8) replacement = NEW_WIDTH / 8;
            else if (value == HEIGHT / 4) replacement = NEW_HEIGHT / 4;
            else continue;
            const bool is_movl = at >= 3 && e[at - 3] == 0xc7 && modrm(e[at - 2]);
            const bool is_movabs = (at >= 2 && e[at - 2] == 0x48 && e[at - 1] == 0xb8) ||
                                   (at >= 6 && e[at - 6] == 0x48 && e[at - 5] == 0xb8);
            // 1920/8 and 1080/4 only in fields 0x40/0x44 (the 1080p mode's 240x270)
            if ((value == WIDTH / 8 || value == HEIGHT / 4) && !(is_movl && (e[at - 1] == 0x40 || e[at - 1] == 0x44)))
                continue;
            if (is_movl || is_movabs) put32(at, replacement);
        }
    }
    const std::vector<size_t> dec = decoders(e, n);
    if (dec.size() != 1) {
        error = "expected one movie decoder setup, found " + std::to_string(dec.size());
        return {};
    }
    put32(dec[0] + 4, NEW_WIDTH);
    put32(dec[0] + 12, NEW_HEIGHT);
    return out;
}

Bytes render_720p_patch(const std::vector<std::pair<u64, Bytes>>& writes) {
    Bytes blob = {'B', 'B', 'P', 'A', 'T', 'C', 'H', '2'};
    auto q = [&](u64 v) { u8 b[8]; std::memcpy(b, &v, 8); blob.insert(blob.end(), b, b + 8); };
    q(0x400000);
    q(writes.size());
    for (auto& [offset, data] : writes) {
        q(offset);
        q(data.size());
        blob.insert(blob.end(), data.begin(), data.end());
    }
    return blob;
}
