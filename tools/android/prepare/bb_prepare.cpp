// bb-prepare GAME_DIR DATA_DIR [EXE]: builds the runtime image (data/boot-linked.bin, content.bin) from the
// player's own game files. A native (Bionic) port of scripts/prepare.py, link_modules.py and
// content_profile.py (from ducvd89's Bloodborne-Android; the Uncharted ARM port adds the executable
// argument, eboot.bin, big2-ps4_Shipping.elf or big3-ps4_Shipping.elf, the runtime's import spelling
// of link_modules.py, and render-720p.bin from scripts/render_720p.py). Same outputs byte for byte.
// The release APK contains no game executable, module, patch image, or save.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>

#include "boot.h"
#include "graphics.h"
#include "hash.h"
#include "io.h"
#include "ps4.h"
#include "render720.h"

namespace {
constexpr u64 LIMIT = 512u * 1024 * 1024;

void say(const std::string& line) {
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// The game at width x height (scripts/render_res.py's patch, without the movie decoder's size):
// render-WxH-2.bin, or render-WxH.txt with the reason. 960x540 is the 540p render resolution
// (the 0.1.6 render-960x540.bin had the decoder's size); 1280x720, 1136x636 and 640x360 are the
// upscaling presets' sizes (Quality, Balanced, Ultra Performance; Performance is 960x540), which
// run-uncharted.sh upscales to 1920x1080 with FSR 3.1 or GSR 2.
constexpr std::pair<u32, u32> RENDER_SIZES[] = {{960, 540}, {1280, 720}, {1136, 636}, {640, 360}};

void write_render_size(const Bytes& elf, const std::string& out, u32 width, u32 height) {
    const std::string name = "render-" + std::to_string(width) + "x" + std::to_string(height);
    std::string error;
    auto writes = render_size_writes(elf, width, height, error, false);
    if (error.empty()) {
        Bytes blob = render_720p_patch(writes);
        OutFile f(path_join(out, name + "-2.bin"));
        f.write(blob.data(), blob.size());
        f.close();
        say("Render " + name.substr(7) + ": " + std::to_string(writes.size()) + " size values patched");
    } else {
        write_text_file(path_join(out, name + ".txt"), error + "\n");
        say("Render " + name.substr(7) + ": " + error);
    }
}

void write_render_sizes(const Bytes& elf, const std::string& out) {
    for (const auto& [w, h] : RENDER_SIZES) write_render_size(elf, out, w, h);
}

/// The files write_render_sizes writes.
std::vector<std::string> render_size_files() {
    std::vector<std::string> names;
    for (const auto& [w, h] : RENDER_SIZES) {
        const std::string name = "render-" + std::to_string(w) + "x" + std::to_string(h);
        names.push_back(name + "-2.bin");
        names.push_back(name + ".txt");
    }
    return names;
}

// prepare.prepare(): the BBPROBE2 image in memory (the Python writes it to boot.bin, eboot.elf, entry.bin and
// analysis.json in the work folder, which are deleted again; only boot-linked.bin and content.bin stay).
Boot prepare(const std::string& game, const std::string& out, const std::string& exe) {
    SelfInfo s;
    {
        MappedFile source = read_file(path_join(game, exe));
        s = parse_self(source.view());
    }
    // render_720p.py on the prepared ELF (eboot.elf of prepare.py): render-720p.bin, or render-720p.txt
    // with the reason it is not available (the game then renders at its usual 1080p).
    {
        std::string error;
        auto writes = render_720p_writes(s.elf, error);
        if (error.empty()) {
            Bytes blob = render_720p_patch(writes);
            OutFile f(path_join(out, "render-720p.bin"));
            f.write(blob.data(), blob.size());
            f.close();
            say("Render 720p: " + std::to_string(writes.size()) + " size values patched");
        } else {
            write_text_file(path_join(out, "render-720p.txt"), error + "\n");
            say("Render 720p: " + error);
        }
    }
    write_render_sizes(s.elf, out);
    write_graphics_patches(s, exe, out);
    std::vector<Phdr> loads;
    for (auto& p : s.ph)
        if (p.type == 1 || p.type == PT_SCE_RELRO) loads.push_back(p);
    auto mapped = [&](u128 addr, u128 len = 8) {
        for (auto& p : loads)
            if (p.vaddr <= addr && addr + len <= (u128)p.vaddr + p.memsz) return true;
        return false;
    };
    if (loads.empty()) raise("ValueError", "max() iterable argument is empty");
    u128 size128 = 0;
    for (auto& p : loads) size128 = std::max(size128, (u128)p.vaddr + p.memsz);
    if (size128 > LIMIT || !mapped(s.header.entry, 1)) raise("ValueError", "invalid memory image");
    u64 size = (u64)size128;
    Boot boot;
    boot.size = size;
    boot.entry = s.header.entry;
    boot.image.reserve(size + (16u << 20));  // room for the linked modules
    boot.image.resize(size);
    View elf = view(s.elf);
    for (auto& p : loads) {
        if (p.filesz > p.memsz) raise("ValueError", "segment file size exceeds memory size");
        View src = span(elf, p.offset, p.filesz);
        std::memcpy(boot.image.data() + p.vaddr, src.p, src.n);
    }
    Dynamic dyn = open_dynamic(s, true);
    u64 a = dyn.tag(DT_SCE_SYMTAB), b = dyn.tag(DT_SCE_SYMTABSZ);
    View syms = span(dyn.blob, a, b);
    if (syms.n % 24) raise("ValueError", "bad symbol table size");
    std::vector<Sym> symbols;
    for (size_t pos = 0; pos < syms.n; pos += 24) {
        Cur c = unpack(syms, pos, 24);
        u32 name = c.u32_();
        u8 info = c.u8_();
        c.u8_();
        u16 shndx = c.u16_();
        u64 value = c.u64_(), sz = c.u64_();
        symbols.push_back({string_at(dyn.strings, name), (u8)(info & 15), (u8)(info >> 4), shndx, value, sz});
    }
    std::vector<size_t> import_order;  // symbol index per import number
    std::vector<i64> import_of(symbols.size(), -1);
    u64 reloc_total = 0;
    const u64 tables[2][2] = {{0x61000029, 0x6100002d}, {0x6100002f, 0x61000031}};
    for (auto& t : tables) {
        u64 ot = dyn.tag(t[0]), st = dyn.tag(t[1]);
        View table = span(dyn.blob, ot, st);
        if (table.n % 24) raise("ValueError", "bad relocation table size");
        for (size_t pos = 0; pos < table.n; pos += 24) {
            Cur c = unpack(table, pos, 24);
            u64 target = c.u64_(), info = c.u64_();
            i64 addend = c.i64_();
            u64 kind = info & 0xffffffff, sym = info >> 32;
            reloc_total++;
            if (!mapped(target)) raise("ValueError", "relocation outside mapped segments: " + hex(target));
            if (kind == 8) {
                boot.relocs.push_back({target, 0, addend, 0});  // base-relative
            } else if (kind == 1 || kind == 6 || kind == 7) {
                if (sym >= symbols.size()) raise("IndexError", "list index out of range");
                const Sym& symbol = symbols[sym];
                if (symbol.section) {
                    i128 v = (i128)symbol.value + addend;
                    if (symbol.section == 0xfff1) {
                        u64 q = checked_q_unsigned(v);
                        std::memcpy(boot.image.data() + target, &q, 8);
                    } else {
                        boot.relocs.push_back({target, 0, v, 0});
                    }
                } else {
                    if (import_of[sym] < 0) {
                        import_of[sym] = (i64)import_order.size();
                        import_order.push_back(sym);
                    }
                    u64 k = symbol.type == 1 ? 2 : 1;
                    if ((k == 1 && addend) || !(0 <= addend && addend < 4096)) raise("ValueError", "unsupported import addend");
                    boot.relocs.push_back({target, k, import_of[sym], addend});
                }
            } else {
                raise("ValueError", "unsupported relocation " + std::to_string(kind));
            }
        }
    }
    for (size_t i : import_order) boot.names.push_back(symbols[i].name);
    bool init_env_ret;
    {
        MappedFile libc = read_file(path_join(game, "sce_module/libc.prx"));
        init_env_ret = inspect_libc_init_env_is_ret(libc.view());
    }
    make_dirs(out);
    for (auto& n : boot.names)
        if (n.size() >= 128) raise("ValueError", "import name too long");
    for (auto& r : boot.relocs) checked_q_signed(r.value);  // boot.bin's struct.pack('<QQqq')
    for (auto& p : loads) boot.segments.push_back({p.vaddr, p.memsz, p.flags});
    boot.flags = init_env_ret;
    // analysis.json's inputs that can fail: bundled modules, param.sfo, the NEEDED names.
    Sfo values;
    {
        MappedFile f = read_file(path_join(game, "sce_sys/param.sfo"));
        values = sfo(f.view());
    }
    u64 needed = 0;
    for (auto& [tag, value] : dyn.dyn)
        if (tag == 1) {
            string_at(dyn.strings, value);
            needed++;
        }
    auto title = values.find("TITLE");
    say((title == values.end() ? std::string("None") : title->second.str()) + " | entry=" + hex(s.header.entry) +
        " | image=" + commas(size) + " bytes");
    say(std::to_string(boot.names.size()) + " imported symbols; " + commas(reloc_total) + " relocations; " +
        std::to_string(needed) + " required modules");
    if (!s.missing.empty()) {
        std::string l;
        for (size_t m : s.missing) l += (l.empty() ? "" : ", ") + std::to_string(m);
        say("Unavailable non-loadable metadata headers: [" + l + "]; not a byte-exact ELF reconstruction");
    }
    say("Output: " + resolve_path(out));
    say(std::string("libc _init_env verified RET: ") + (init_env_ret ? "True" : "False"));
    return boot;
}

// content_profile.prepare(): offline AppContent metadata; SKU is a probe setting, not a license check.
void content_profile(const std::string& game, const std::string& out) {
    Sfo values;
    {
        MappedFile f = read_file(path_join(game, "sce_sys/param.sfo"));
        values = sfo(f.view());
    }
    u32 params[4];
    std::string list;
    for (int i = 0; i < 4; i++) {
        auto it = values.find("USER_DEFINED_PARAM_" + std::to_string(i + 1));
        if (it != values.end() && !it->second.is_int) raise("ValueError", "invalid user-defined parameter");
        params[i] = it == values.end() ? 0 : it->second.i;
        list += (i ? ", " : "") + std::to_string(params[i]);
    }
    make_dirs(out);
    u8 rec[28] = {'B', 'B', 'C', 'O', 'N', 'T', '0', '1'};
    u32 sku = 3;  // 'full'
    std::memcpy(rec + 8, &sku, 4);
    std::memcpy(rec + 12, params, 16);
    OutFile f(path_join(out, "content.bin"));
    f.write(rec, sizeof rec);
    f.close();
    say("AppContent profile: SKU=full (probe setting), user params=[" + list + "], mounted add-ons=0");
}

std::string json_string(const std::string& raw) {  // json.dumps of an os.fsdecode()d str (ensure_ascii)
    std::vector<u32> cps;
    for (size_t i = 0; i < raw.size();) {
        u8 b = (u8)raw[i];
        size_t need = b < 0x80 ? 0 : b >= 0xc2 && b <= 0xdf ? 1 : b >= 0xe0 && b <= 0xef ? 2 : b >= 0xf0 && b <= 0xf4 ? 3 : 99;
        u32 cp = need == 0 ? b : need == 1 ? b & 0x1f : need == 2 ? b & 0x0f : b & 0x07;
        bool ok = need != 99 && i + need < raw.size();
        for (size_t k = 1; ok && k <= need; k++) {
            u8 c = (u8)raw[i + k];
            if ((c & 0xc0) != 0x80) ok = false;
            else cp = cp << 6 | (c & 0x3f);
        }
        if (ok && ((need == 2 && (cp < 0x800 || (cp >= 0xd800 && cp < 0xe000))) || (need == 3 && (cp < 0x10000 || cp > 0x10ffff))))
            ok = false;
        if (ok) { cps.push_back(cp); i += need + 1; }
        else { cps.push_back(0xdc00 + b); i++; }  // surrogateescape
    }
    std::string r = "\"";
    char buf[16];
    for (u32 cp : cps) {
        if (cp == '"') r += "\\\"";
        else if (cp == '\\') r += "\\\\";
        else if (cp == '\n') r += "\\n";
        else if (cp == '\r') r += "\\r";
        else if (cp == '\t') r += "\\t";
        else if (cp == '\b') r += "\\b";
        else if (cp == '\f') r += "\\f";
        else if (cp < 0x20) { std::snprintf(buf, sizeof buf, "\\u%04x", cp); r += buf; }
        else if (cp < 0x7f || cp == 0x7f) r += (char)cp;
        else if (cp >= 0x10000) {
            cp -= 0x10000;
            std::snprintf(buf, sizeof buf, "\\u%04x\\u%04x", 0xd800 + (cp >> 10), 0xdc00 + (cp & 0x3ff));
            r += buf;
        } else { std::snprintf(buf, sizeof buf, "\\u%04x", cp); r += buf; }
    }
    return r + "\"";
}

// signature(): {"exe": [size, mtime_ns], "path": ..., modules, param.sfo} (the image is made again when
// any of them changes, or another executable is asked for).
std::string signature(const std::string& game, const std::string& exe) {
    std::string result = "{" + json_string(exe) + ": ";
    std::string resolved = resolve_path(game);  // game.resolve() comes first in the Python
    const std::string names[] = {exe, "sce_module/libc.prx", "sce_module/libSceFios2.prx", "sce_sys/param.sfo"};
    std::string st_text[4];
    for (int i = 0; i < 4; i++) {
        std::string p = path_join(game, names[i]);
        struct stat st;
        if (stat(p.c_str(), &st) < 0) raise_errno(errno, p);
        st_text[i] = "[" + std::to_string((long long)st.st_size) + ", " +
                     std::to_string((long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec) + "]";
    }
    // sorted keys: eboot.bin, path, sce_module/libSceFios2.prx, sce_module/libc.prx, sce_sys/param.sfo
    result += st_text[0] + ", \"path\": " + json_string(resolved) + ", \"sce_module/libSceFios2.prx\": " + st_text[2] +
              ", \"sce_module/libc.prx\": " + st_text[1] + ", \"sce_sys/param.sfo\": " + st_text[3] + "}";
    return result;
}

int run(const std::string& game, const std::string& data, const std::string& exe) {
    make_dirs(data);
    std::string identity = signature(game, exe);
    std::string marker = path_join(data, "game-source.json");
    if (is_file(path_join(data, "boot-linked.bin")) && is_file(path_join(data, "content.bin")) && is_file(marker)) {
        try {
            MappedFile f = read_file(marker);
            std::string text((const char*)f.view().p, f.view().n);
            while (!text.empty() && (text.back() == '\n' || text.back() == ' ' || text.back() == '\r' || text.back() == '\t'))
                text.pop_back();
            if (text == identity) {
                say("Game image: existing image matches selected game");
                // Images prepared before the 540p patch or the graphics patches existed get them
                // on their own.
                // (0.1.7: the upscaling presets' sizes, 640x360 the last.)
                const bool r540 = !is_file(path_join(data, "render-640x360-2.bin")) && !is_file(path_join(data, "render-640x360.txt"));
                const bool gfx = !is_file(path_join(data, "graphics-1.txt"));
                if (r540 || gfx) {
                    MappedFile source = read_file(path_join(game, exe));
                    SelfInfo self = parse_self(source.view());
                    if (r540) write_render_sizes(self.elf, data);
                    if (gfx) write_graphics_patches(self, exe, data);
                }
                return 0;
            }
        } catch (PyError&) {
        }
    }
    std::string work = path_join(data, "prepare-tmp");
    rmtree_ignore_errors(work);
    make_dir(work);
    try {
        say("Game image: preparing selected game files (first launch may take a few minutes)");
        {
            Boot boot = prepare(game, work, exe);
            link_modules(game, work, boot, exe);
        }
        content_profile(game, work);
        if (rename(path_join(work, "boot-linked.bin").c_str(), path_join(data, "boot-linked.bin").c_str()) < 0)
            raise_errno(errno, path_join(work, "boot-linked.bin"));
        if (rename(path_join(work, "content.bin").c_str(), path_join(data, "content.bin").c_str()) < 0)
            raise_errno(errno, path_join(work, "content.bin"));
        std::vector<std::string> renders = render_size_files();
        for (const char* name : {"render-720p.bin", "render-720p.txt", "render-960x540.bin"}) renders.push_back(name);
        for (const std::string& name : renders) {
            unlink(path_join(data, name).c_str());
            if (is_file(path_join(work, name))) rename(path_join(work, name).c_str(), path_join(data, name).c_str());
        }
        // The graphics patches (gfx-*.bin, graphics-1.txt).
        std::vector<std::string> gfx = graphics_patch_files();
        gfx.push_back("graphics-1.txt");
        for (const std::string& name : gfx) {
            unlink(path_join(data, name).c_str());
            if (is_file(path_join(work, name))) rename(path_join(work, name).c_str(), path_join(data, name).c_str());
        }
        write_text_file(marker, identity + "\n");
        say("Game image: ready");
    } catch (...) {
        rmtree_ignore_errors(work);
        throw;
    }
    rmtree_ignore_errors(work);
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::fputs("usage: bb-prepare GAME_DIR DATA_DIR [EXE]\n", stderr);
        return 1;
    }
    try {
        return run(argv[1], argv[2], argc == 4 ? argv[3] : "eboot.bin");
    } catch (PyError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "%s%s%s\n", e.kind.c_str(), e.msg.empty() ? "" : ": ", e.msg.c_str());
        return 1;
    } catch (std::bad_alloc&) {
        std::fflush(stdout);
        std::fputs("MemoryError\n", stderr);
        return 1;
    }
}
