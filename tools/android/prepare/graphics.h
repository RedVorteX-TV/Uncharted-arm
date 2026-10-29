// Uncharted ARM port: the launcher's graphics patches (patches/graphics_patches.json, from the
// Bachata S4+ catalog), checked against the player's executable once, when it is prepared.
#pragma once
#include <string>
#include <vector>

#include "ps4.h"

/// Writes DATA_DIR/gfx-<id>.bin (a BBPATCH2 file, probe --extra-patches) for every catalog patch
/// whose original bytes match `exe`'s, and graphics-1.txt with what was found (run-uncharted.sh).
void write_graphics_patches(const SelfInfo& self, const std::string& exe, const std::string& out);
/// The file names write_graphics_patches can write (gfx-<id>.bin of every catalog id).
std::vector<std::string> graphics_patch_files();
