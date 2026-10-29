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

/// scripts/render_res.py's display settings patch below 1080p (the memory table needs no change
/// there): the stores of the display settings code get `width` x `height` (and the fields derived
/// from it), as (image offset, bytes) writes for the prepared ELF; empty with `error` set.
/// `movie_decoder`: the movie decoder's maximum frame size too (off for 540p: the picture goes
/// into a corner of 1080p frames, BB_VDEC_CONTENT).
std::vector<std::pair<u64, Bytes>> render_size_writes(const Bytes& elf, u32 width, u32 height,
                                                      std::string& error, bool movie_decoder = true);
