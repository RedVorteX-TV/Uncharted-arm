#pragma once
// Ported from BilalReloaded's Uncharted: The Nathan Drake Collection PC port
// (github.com/bc160201983/Uncharted-The-Nathan-Drake-Collection-PC-port, GPL-2.0-or-later,
// gpu/shim/gow3_movie.h), for the render resolution patch (scripts/render_res.py).
// Movie frames at a patched render resolution. Uncharted's movie player reads the decoded frames
// texel for texel with the display size it renders at (luma W x H, chroma W/2 x H/2 after
// W * align(H, 64) bytes), assuming movies are as large as the screen; its movies are 1080p and
// its frame buffers 4 MiB each, so above 1080p they came out green and tiled. The decoder
// (shim/videodec2) also writes each frame scaled to the display size into a scratch frame of
// guest memory, and the texture cache (ImageInfo) reads the planes from there. Without a scratch
// frame the planes are read as decoded (right colors, 1:1 in a corner).
// BB_MOVIE_SIZE=<width>x<height> is that display size (run.py, scripts/render_res.py); unset, nothing
// changes.

#include <cstddef>
#include <cstdint>

namespace BbMovie {

// The display size movies are read with (BB_MOVIE_SIZE); false when unset.
bool DisplaySize(std::uint32_t& width, std::uint32_t& height);

// A decoded NV12 frame at base: luma width x height with pitch bytes per row, chroma after rows.
void RecordFrame(std::uint64_t base, std::uint32_t width, std::uint32_t height, std::uint32_t pitch,
                 std::uint32_t rows);

// The scratch frame for the game's frame buffer at base (mapped on first use): the display size
// in NV12, luma pitch ScaledPitch(), chroma (pitch ScaledChromaPitch()) after ScaledRows() rows.
// Null when none can be mapped.
std::uint8_t* ScratchFrame(std::uint64_t base);
std::uint32_t ScaledPitch();
std::uint32_t ScaledRows();
/// Bytes per chroma row (2-byte texels): its width padded to 64 texels, as the GPU reads a
/// linear texture (1440 texels at 1620p are read with a pitch of 1472).
std::uint32_t ScaledChromaPitch();
/// Bytes of a scratch frame: luma, then the chroma plane (ScaledRows() / 2 rows).
std::uint64_t ScaledFrameBytes();

// The movie player's conversion shader samples a frame at u = (x + 0.5) / 1920 and
// v = (y + 0.5) / 1088 with those factors as literals, so above 1080p it covers only the top-left
// 1920x1080. A shader holding both literals is a movie shader: its cache key gets the display size
// mixed in (a patched copy never meets an unpatched one in the shader cache), and its literals are
// rescaled to the display size (the scratch frame). Without BB_MOVIE_SIZE nothing changes.
std::uint64_t ShaderKey(std::uint64_t hash, const std::uint32_t* code, std::size_t words);
bool IsMovieShader(const std::uint32_t* code, std::size_t words);
// A movie shader literal at the display size; false for any other value.
bool MovieShaderConstant(std::uint32_t bits, float& patched);

// An image over a recorded frame described with the display size: the plane in the scratch
// frame, else as decoded (address, size and pitch in elements). bits: bits per element (8 luma,
// 16 chroma). True when changed.
bool Remap(std::uint64_t& address, std::uint32_t& width, std::uint32_t& height, std::uint32_t& pitch,
           std::uint32_t bits);

} // namespace BbMovie
