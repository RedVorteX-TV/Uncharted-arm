// Uncharted ARM port: scripts/render_720p.py (the BBPATCH2 that renders a game at 1280x720).
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "pyish.h"

/// The (image offset, bytes) writes for the prepared ELF; empty with `error` set when the sites
/// are not found (the game then renders at its usual 1080p).
std::vector<std::pair<u64, Bytes>> render_720p_writes(const Bytes& elf, std::string& error);
/// The BBPATCH2 file of those writes (src/probe.c --patches).
Bytes render_720p_patch(const std::vector<std::pair<u64, Bytes>>& writes);
