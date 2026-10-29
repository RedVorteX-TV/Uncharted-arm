// Uncharted ARM port: the launcher's graphics patches (graphics.h; scripts/graphics_patches.py does
// the same on Linux).
#include "graphics.h"

#include <unistd.h>

#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "hash.h"
#include "io.h"
#include "render720.h"

namespace {
struct Write {
    u64 address;
    u32 length;
    const char* sha256;
    const char* bytes;
};
struct Patch {
    const char* id;
    const char* elf;
    const char* version;
    std::vector<Write> writes;
};
const std::vector<Patch>& catalog() {
    static const std::vector<Patch> patches = {
#include "graphics_catalog.inc"
    };
    return patches;
}
constexpr u64 BASE = 0x400000;  // the catalog's PS4 addresses; the image starts at vaddr 0

// The file bytes at image offset `at` (a loaded segment's file part), or null.
const u8* image_bytes(const SelfInfo& self, u64 at, u64 length) {
    for (const Phdr& p : self.ph) {
        if (p.type != PT_LOAD && p.type != PT_SCE_RELRO) continue;
        if (at >= p.vaddr && at - p.vaddr <= p.filesz && length <= p.filesz - (at - p.vaddr) &&
            p.offset + (at - p.vaddr) + length <= self.elf.size())
            return self.elf.data() + p.offset + (at - p.vaddr);
    }
    return nullptr;
}

Bytes unhex(const char* text) {
    Bytes out;
    for (size_t i = 0; text[i] && text[i + 1]; i += 2) {
        char pair[3] = {text[i], text[i + 1], 0};
        out.push_back((u8)std::strtoul(pair, nullptr, 16));
    }
    return out;
}
}  // namespace

void write_graphics_patches(const SelfInfo& self, const std::string& exe, const std::string& out) {
    // Every patch of this executable, by id: the versions whose original bytes all match.
    std::map<std::string, std::vector<const Patch*>> matches;
    std::set<std::string> known;
    for (const Patch& patch : catalog()) {
        if (exe != patch.elf) continue;
        known.insert(patch.id);
        bool fits = true;
        for (const Write& w : patch.writes) {
            const u8* original = w.address >= BASE ? image_bytes(self, w.address - BASE, w.length) : nullptr;
            if (!original || sha256_hex(View{original, w.length}) != w.sha256) {
                fits = false;
                break;
            }
        }
        if (fits) matches[patch.id].push_back(&patch);
    }
    std::string report;
    for (const std::string& id : known) {
        const std::string path = path_join(out, "gfx-" + id + ".bin");
        unlink(path.c_str());
        auto found = matches.find(id);
        if (found == matches.end() || found->second.size() != 1) {
            report += id + ": " + (found == matches.end() ? "not for this executable" : "ambiguous") + "\n";
            continue;
        }
        const Patch& patch = *found->second.front();
        std::vector<std::pair<u64, Bytes>> writes;
        for (const Write& w : patch.writes) writes.push_back({w.address - BASE, unhex(w.bytes)});
        Bytes blob = render_720p_patch(writes);  // BBPATCH2, base 0x400000
        OutFile f(path);
        f.write(blob.data(), blob.size());
        f.close();
        report += id + ": " + patch.version + "\n";
    }
    write_text_file(path_join(out, "graphics-2.txt"), report);
    std::printf("Graphics patches: %zu of %zu available for %s\n", matches.size(), known.size(), exe.c_str());
    std::fflush(stdout);
}

std::vector<std::string> graphics_patch_files() {
    std::set<std::string> ids;
    for (const Patch& patch : catalog()) ids.insert(patch.id);
    std::vector<std::string> names;
    for (const std::string& id : ids) names.push_back("gfx-" + id + ".bin");
    return names;
}
