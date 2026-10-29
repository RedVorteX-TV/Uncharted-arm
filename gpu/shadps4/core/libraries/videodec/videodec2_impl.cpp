// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include "videodec2_impl.h"

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "video_utils.h"
#include "videodec_error.h"

#include "common/support/avdec.h"
#include "core/memory.h"
#include "bbport_movie.h"

#include <algorithm>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace Libraries::Videodec2 {

namespace {
// BB_FRAME_STATS: where the time of each decoded picture goes, every 5 s (the movies'
// own frame rate is what the game shows while one plays).
struct DecodeStats {
    std::mutex mutex;
    std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    u64 frames = 0, empty = 0;
    double decode = 0, convert = 0, fit = 0, copy = 0, worst = 0;
};
DecodeStats g_stats;
bool StatsWanted() {
    static const bool wanted = std::getenv("BB_FRAME_STATS") != nullptr;
    return wanted;
}
double Ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
void NoteDecode(bool picture, double decode, double convert, double fit, double copy) {
    std::lock_guard lock{g_stats.mutex};
    auto& s = g_stats;
    if (picture) {
        ++s.frames;
        s.decode += decode; s.convert += convert; s.fit += fit; s.copy += copy;
        s.worst = std::max(s.worst, decode + convert + fit + copy);
    } else {
        ++s.empty;
        s.decode += decode;
    }
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - s.since).count();
    if (seconds < 5.0) {
        return;
    }
    const double n = double(std::max<u64>(s.frames, 1));
    std::printf("Videodec2: %.1f pictures/s (%llu calls without one); per picture %.1f ms decode, "
                "%.1f NV12, %.1f fit, %.1f copy; worst %.1f ms\n",
                double(s.frames) / seconds, (unsigned long long)s.empty, s.decode / n,
                s.convert / n, s.fit / n, s.copy / n, s.worst);
    s.frames = s.empty = 0;
    s.decode = s.convert = s.fit = s.copy = s.worst = 0;
    s.since = now;
}
/// BB_VDEC_CONTENT=<w>x<h> (from revvv991's uncharted-android): frames keep the stream's size and
/// the picture is scaled into their top-left w x h, the rest black. Uncharted's movie player maps
/// the movie's own size (1920x1080, from the stream) 1:1 onto its display buffer: at 960x540 it
/// shows the top-left 960x540 of the frame, so that corner holds the whole movie. (Patching the
/// decoder's maximum size to 960x540 instead, as the 720p mode does for 1280x720, gave grey
/// pictures with coloured stripes.)
bool EnvContentSize(u32& width, u32& height) {
    static const std::pair<u32, u32> size = [] {
        const char* env = std::getenv("BB_VDEC_CONTENT");
        unsigned w = 0, h = 0;
        if (!env || std::sscanf(env, "%ux%u", &w, &h) != 2 || w < 64 || h < 64) {
            return std::pair<u32, u32>{0, 0};
        }
        return std::pair<u32, u32>{w & ~1u, h & ~1u};
    }();
    width = size.first;
    height = size.second;
    return width > 0;
}

// From revvv991's uncharted-android (videodec2_impl.cpp, GPL-2.0-or-later), as there:
#if defined(__aarch64__)
/// g2-arm64: an area filter for the exact ratios below 1080p, 16 output pixels at a time.
struct Wide {
    uint16x8_t lo, hi;
};

/// 2 * major + minor, widened.
inline Wide Weigh21(uint8x16_t major, uint8x16_t minor) {
    return {vaddw_u8(vshll_n_u8(vget_low_u8(major), 1), vget_low_u8(minor)),
            vaddw_high_u8(vshll_high_n_u8(major, 1), minor)};
}

inline Wide Weigh21(Wide major, Wide minor) {
    return {vaddq_u16(vshlq_n_u16(major.lo, 1), minor.lo),
            vaddq_u16(vshlq_n_u16(major.hi, 1), minor.hi)};
}

/// x / 9, rounded (x <= 9 * 255): (x * 7282 + 2^15) >> 16 by the doubling multiply.
inline uint8x16_t Div9(Wide x) {
    return vcombine_u8(vqmovun_s16(vqrdmulhq_n_s16(vreinterpretq_s16_u16(x.lo), 3641)),
                       vqmovun_s16(vqrdmulhq_n_s16(vreinterpretq_s16_u16(x.hi), 3641)));
}

/// 3:2 - 16 groups of 3 pixels in 3 rows: 2 rows of 2 pixels each. An output pixel covers one
/// source pixel and half of the next on each axis (weights 2 and 1, sum / 9).
inline void Area32(const u8* r0, const u8* r1, const u8* r2, uint8x16x2_t& top,
                   uint8x16x2_t& bottom) {
    const uint8x16x3_t a = vld3q_u8(r0), b = vld3q_u8(r1), c = vld3q_u8(r2);
    Wide up[3], down[3];
    for (int k = 0; k < 3; ++k) {
        up[k] = Weigh21(a.val[k], b.val[k]);
        down[k] = Weigh21(c.val[k], b.val[k]);
    }
    top.val[0] = Div9(Weigh21(up[0], up[1]));
    top.val[1] = Div9(Weigh21(up[2], up[1]));
    bottom.val[0] = Div9(Weigh21(down[0], down[1]));
    bottom.val[1] = Div9(Weigh21(down[2], down[1]));
}

/// 2:1 - 16 pairs of pixels in 2 rows: the mean of each 2x2.
inline uint8x16_t Area21(const u8* r0, const u8* r1) {
    const uint8x16x2_t a = vld2q_u8(r0), b = vld2q_u8(r1);
    const uint16x8_t lo = vaddq_u16(vaddl_u8(vget_low_u8(a.val[0]), vget_low_u8(a.val[1])),
                                    vaddl_u8(vget_low_u8(b.val[0]), vget_low_u8(b.val[1])));
    const uint16x8_t hi =
        vaddq_u16(vaddl_high_u8(a.val[0], a.val[1]), vaddl_high_u8(b.val[0], b.val[1]));
    return vcombine_u8(vrshrn_n_u16(lo, 2), vrshrn_n_u16(hi, 2));
}

/// 3:1 - 16 groups of 3 pixels in 3 rows: the mean of each 3x3.
inline uint8x16_t Area31(const u8* r0, const u8* r1, const u8* r2) {
    const uint8x16x3_t rows[3] = {vld3q_u8(r0), vld3q_u8(r1), vld3q_u8(r2)};
    Wide sum{vdupq_n_u16(0), vdupq_n_u16(0)};
    for (const uint8x16x3_t& row : rows) {
        for (int k = 0; k < 3; ++k) {
            sum.lo = vaddw_u8(sum.lo, vget_low_u8(row.val[k]));
            sum.hi = vaddw_high_u8(sum.hi, row.val[k]);
        }
    }
    return Div9(sum);
}
#endif

/// g2-arm64: the decoder's 4:2:0 picture (src) into the NV12 frame's top-left width x height,
/// made smaller by an exact 3:2, 2:1 or 3:1 (1080p into 720p, 540p, 360p) with an area filter.
/// sws_scale's bilinear filter took 8 ms of the 16.7 a 60 FPS movie frame has for 720p on the
/// G2 (issue #6). False for another format, ratio or size: the caller scales with sws then.
bool AreaDownscale(const AVFrame& src, AVFrame& dst, s32 width, s32 height) {
#if defined(__aarch64__)
    if (src.format != AV_PIX_FMT_YUV420P) {
        return false;
    }
    const s32 w = src.width, h = src.height;
    const int ratio = width * 3 == w * 2 && height * 3 == h * 2 ? 32
                      : width * 2 == w && height * 2 == h       ? 21
                      : width * 3 == w && height * 3 == h       ? 31
                                                                : 0;
    const s32 block = ratio == 21 ? 32 : 48, rows = ratio == 21 ? 2 : 3;
    if (!ratio || w % (2 * block) || h % (2 * rows)) {
        return false; // whole blocks in the chroma planes too
    }
    const u8* const y_plane = src.data[0];
    const u8* const u_plane = src.data[1];
    const u8* const v_plane = src.data[2];
    const s32 ys = src.linesize[0], us = src.linesize[1], vs = src.linesize[2];
    u8* const luma = dst.data[0];
    u8* const chroma = dst.data[1];
    const s32 ls = dst.linesize[0], cs = dst.linesize[1];
    if (ratio == 32) {
        for (s32 y = 0; y < h; y += 3) {
            const u8* r = y_plane + y * ys;
            u8* d = luma + (y / 3 * 2) * ls;
            for (s32 x = 0; x < w; x += 48) {
                uint8x16x2_t top, bottom;
                Area32(r + x, r + ys + x, r + 2 * ys + x, top, bottom);
                vst2q_u8(d + x / 3 * 2, top);
                vst2q_u8(d + ls + x / 3 * 2, bottom);
            }
        }
        for (s32 y = 0; y < h / 2; y += 3) {
            const u8* u = u_plane + y * us;
            const u8* v = v_plane + y * vs;
            u8* d = chroma + (y / 3 * 2) * cs;
            for (s32 x = 0; x < w / 2; x += 48) {
                uint8x16x2_t ut, ub, vt, vb;
                Area32(u + x, u + us + x, u + 2 * us + x, ut, ub);
                Area32(v + x, v + vs + x, v + 2 * vs + x, vt, vb);
                vst4q_u8(d + x / 3 * 4,
                         (uint8x16x4_t{{ut.val[0], vt.val[0], ut.val[1], vt.val[1]}}));
                vst4q_u8(d + cs + x / 3 * 4,
                         (uint8x16x4_t{{ub.val[0], vb.val[0], ub.val[1], vb.val[1]}}));
            }
        }
    } else if (ratio == 21) {
        for (s32 y = 0; y < h; y += 2) {
            const u8* r = y_plane + y * ys;
            u8* d = luma + (y / 2) * ls;
            for (s32 x = 0; x < w; x += 32) {
                vst1q_u8(d + x / 2, Area21(r + x, r + ys + x));
            }
        }
        for (s32 y = 0; y < h / 2; y += 2) {
            const u8* u = u_plane + y * us;
            const u8* v = v_plane + y * vs;
            u8* d = chroma + (y / 2) * cs;
            for (s32 x = 0; x < w / 2; x += 32) {
                vst2q_u8(d + x,
                         (uint8x16x2_t{{Area21(u + x, u + us + x), Area21(v + x, v + vs + x)}}));
            }
        }
    } else {
        for (s32 y = 0; y < h; y += 3) {
            const u8* r = y_plane + y * ys;
            u8* d = luma + (y / 3) * ls;
            for (s32 x = 0; x < w; x += 48) {
                vst1q_u8(d + x / 3, Area31(r + x, r + ys + x, r + 2 * ys + x));
            }
        }
        for (s32 y = 0; y < h / 2; y += 3) {
            const u8* u = u_plane + y * us;
            const u8* v = v_plane + y * vs;
            u8* d = chroma + (y / 3) * cs;
            for (s32 x = 0; x < w / 2; x += 48) {
                vst2q_u8(d + x / 3 * 2,
                         (uint8x16x2_t{{Area31(u + x, u + us + x, u + 2 * us + x),
                                        Area31(v + x, v + vs + x, v + 2 * vs + x)}}));
            }
        }
    }
    return true;
#else
    return false;
#endif
}
} // namespace

VdecDecoder::VdecDecoder(const OrbisVideodec2DecoderConfigInfo& config_info,
                         const OrbisVideodec2DecoderMemoryInfo& memory_info) {
    const AVCodec* codec = avcodec_find_decoder(
        config_info.codec_type == OrbisVideodec2CodecType::Avc ? AV_CODEC_ID_H264
                                                               : AV_CODEC_ID_HEVC);
    ASSERT(codec);

    m_codec_context = avcodec_alloc_context3(codec);
    ASSERT(m_codec_context);
    m_codec_context->width = config_info.max_frame_width;
    m_codec_context->height = config_info.max_frame_height;
    m_max_width = config_info.max_frame_width;
    m_max_height = config_info.max_frame_height;
    std::printf("Videodec2: decoder for %ux%u frames (%s)\n", m_max_width, m_max_height, codec->name);
    m_codec_context->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
    // ARM port: the collection's cinematics are 1080p H.264 at 60 pictures/s, 16.7 ms each. On
    // one thread, decoding fell behind in busy scenes (13-15 FPS and the sound ahead in Uncharted
    // 2 on the Odin 3); on four, a picture takes about 6 ms (BB_FRAME_STATS: the 720p fit, not
    // the decoding). Frame threads hold a few pictures back: the game's flush at the end of a
    // movie collects them.
    m_codec_context->thread_count = 4;
    m_codec_context->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    m_codec_context->flags |= AV_CODEC_FLAG_COPY_OPAQUE;

    avcodec_open2(m_codec_context, codec, nullptr);
}

VdecDecoder::~VdecDecoder() {
    avcodec_free_context(&m_codec_context);
    sws_freeContext(m_sws_context);
    sws_freeContext(m_scale_context);
    sws_freeContext(m_fit_context);
    sws_freeContext(m_corner_context);
    av_frame_free(&m_scaled);
    av_frame_free(&m_corner);
}

AVFrame* VdecDecoder::PictureInCorner(AVFrame& frame) {
    u32 width = 0, height = 0;
    if (!EnvContentSize(width, height) || width >= u32(frame.width) || height >= u32(frame.height) ||
        (m_max_width && (u32(frame.width) > m_max_width || u32(frame.height) > m_max_height))) {
        return nullptr;
    }
    // One frame for the decoder's life, black once: the picture goes to the same corner each time.
    if (!m_corner || m_corner->width != frame.width || m_corner->height != frame.height) {
        av_frame_free(&m_corner);
        m_corner = av_frame_alloc();
        m_corner->format = AV_PIX_FMT_NV12;
        m_corner->width = frame.width;
        m_corner->height = frame.height;
        if (av_frame_get_buffer(m_corner, 0) < 0) {
            av_frame_free(&m_corner);
            return nullptr;
        }
        for (int y = 0; y < frame.height; ++y) {
            std::memset(m_corner->data[0] + y * m_corner->linesize[0], 16, frame.width);
        }
        for (int y = 0; y < frame.height / 2; ++y) {
            std::memset(m_corner->data[1] + y * m_corner->linesize[1], 128, frame.width);
        }
        std::printf("Videodec2: %dx%d frames with the picture at %ux%u in their top-left corner\n",
                    frame.width, frame.height, width, height);
    }
    if (!AreaDownscale(frame, *m_corner, s32(width), s32(height))) {
        m_corner_context = sws_getCachedContext(m_corner_context, frame.width, frame.height,
                                                AVPixelFormat(frame.format), int(width), int(height),
                                                AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr,
                                                nullptr);
        if (!m_corner_context || sws_scale(m_corner_context, frame.data, frame.linesize, 0,
                                           frame.height, m_corner->data, m_corner->linesize) < 0) {
            return nullptr;
        }
    }
    AVFrame* reference = av_frame_alloc();
    if (!reference || av_frame_ref(reference, m_corner) < 0) {
        av_frame_free(&reference);
        return nullptr;
    }
    reference->pts = frame.pts;
    reference->pkt_dts = frame.pkt_dts < 0 ? 0 : frame.pkt_dts;
    reference->sample_aspect_ratio = frame.sample_aspect_ratio;
    reference->opaque = frame.opaque;
    return reference;
}

s32 VdecDecoder::Decode(const OrbisVideodec2InputData& input_data,
                        OrbisVideodec2FrameBuffer& frame_buffer,
                        OrbisVideodec2OutputInfo& output_info) {
    frame_buffer.is_accepted = false;
    output_info.is_valid = false;
    output_info.picture_count = 0;

    if (!input_data.au_data) {
        LOG_ERROR(Lib_Vdec2, "ORBIS_VIDEODEC2_ERROR_ACCESS_UNIT_POINTER");
        return ORBIS_VIDEODEC2_ERROR_ACCESS_UNIT_POINTER;
    }
    if (input_data.au_size == 0) {
        LOG_ERROR(Lib_Vdec2, "ORBIS_VIDEODEC2_ERROR_ACCESS_UNIT_SIZE");
        return ORBIS_VIDEODEC2_ERROR_ACCESS_UNIT_SIZE;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        LOG_ERROR(Lib_Vdec2, "Failed to allocate packet");
        return ORBIS_VIDEODEC2_ERROR_API_FAIL;
    }

    packet->data = (u8*)input_data.au_data;
    packet->size = input_data.au_size;
    packet->pts = input_data.pts_data;
    packet->dts = input_data.dts_data;
    packet->opaque = reinterpret_cast<void*>(input_data.attached_data);

    const bool stats = StatsWanted();
    const auto t_start = stats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    int ret = avcodec_send_packet(m_codec_context, packet);
    if (ret == AVERROR_EOF) {
        // Attempt to flush buffers and try again.
        avcodec_flush_buffers(m_codec_context);
        ret = avcodec_send_packet(m_codec_context, packet);
    }
    if (ret < 0) {
        LOG_ERROR(Lib_Vdec2, "Error sending packet to decoder: {}", ret);
        av_packet_free(&packet);
        return ORBIS_VIDEODEC2_ERROR_API_FAIL;
    }

    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        LOG_ERROR(Lib_Vdec2, "Failed to allocate frame");
        av_packet_free(&packet);
        return ORBIS_VIDEODEC2_ERROR_API_FAIL;
    }

    ret = avcodec_receive_frame(m_codec_context, frame);
    const auto t_decoded = stats ? std::chrono::steady_clock::now() : t_start;
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        LOG_TRACE(Lib_Vdec2, "AVERROR_EOF or AVERROR(EAGAIN)");
        if (stats) {
            NoteDecode(false, Ms(t_start, t_decoded), 0, 0, 0);
        }
        av_packet_free(&packet);
        av_frame_free(&frame);
        return ORBIS_OK;
    } else if (ret < 0) {
        LOG_ERROR(Lib_Vdec2, "Error receiving frame from decoder: {}", ret);
        av_packet_free(&packet);
        av_frame_free(&frame);
        return ORBIS_VIDEODEC2_ERROR_API_FAIL;
    }

    if (frame->flags & AV_FRAME_FLAG_INTERLACED) {
        LOG_ERROR(Lib_Vdec2, "Interlaced video output is not suported.");
    }

    if (AVFrame* corner = PictureInCorner(*frame)) {
        av_frame_free(&frame);
        frame = corner;
    }
    if (frame->format != AV_PIX_FMT_NV12) {
        AVFrame* nv12_frame = ConvertNV12Frame(*frame);
        ASSERT(nv12_frame);
        av_frame_free(&frame);
        frame = nv12_frame;
    }
    const auto t_converted = stats ? std::chrono::steady_clock::now() : t_start;
    // Linux fork: in the Steam Deck's 720p mode (scripts/render_720p.py) the game asks for
    // 1280x720 decoders and the collection's movies are 1080p: their frames did not fit its
    // 720p buffers (green and pink logo screens). FitToMaxSize makes them fit.
    // The game's 720p movie textures put the chroma plane after 768 luma rows (rows aligned to
    // 64; 1080p frames come out at 1088 rows either way).
    u32 height_align = 16;
    if (m_max_width && m_max_height &&
        (u32(frame->width) > m_max_width || u32(frame->height) > m_max_height)) {
        if (AVFrame* fitted = FitToMaxSize(*frame)) {
            av_frame_free(&frame);
            frame = fitted;
            height_align = 64;
        }
    }
    const auto t_fitted = stats ? std::chrono::steady_clock::now() : t_start;

    Output(*frame, frame_buffer, output_info, height_align);
    if (stats) {
        NoteDecode(true, Ms(t_start, t_decoded), Ms(t_decoded, t_converted),
                   Ms(t_converted, t_fitted), Ms(t_fitted, std::chrono::steady_clock::now()));
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    return ORBIS_OK;
}

s32 VdecDecoder::Flush(OrbisVideodec2FrameBuffer& frame_buffer,
                       OrbisVideodec2OutputInfo& output_info) {
    frame_buffer.is_accepted = false;
    output_info.is_valid = false;
    output_info.picture_count = 0;

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        LOG_ERROR(Lib_Vdec2, "Failed to allocate frame");
        return ORBIS_VIDEODEC2_ERROR_API_FAIL;
    }

    avcodec_send_packet(m_codec_context, nullptr);
    int ret = avcodec_receive_frame(m_codec_context, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        av_frame_free(&frame);
        return ORBIS_OK;
    } else if (ret < 0) {
        LOG_ERROR(Lib_Vdec2, "Error receiving frame from decoder: {}", ret);
        av_frame_free(&frame);
        return ORBIS_VIDEODEC2_ERROR_API_FAIL;
    }

    if (AVFrame* corner = PictureInCorner(*frame)) {
        av_frame_free(&frame);
        frame = corner;
    }
    if (frame->format != AV_PIX_FMT_NV12) {
        AVFrame* nv12_frame = ConvertNV12Frame(*frame);
        ASSERT(nv12_frame);
        av_frame_free(&frame);
        frame = nv12_frame;
    }
    // Linux fork: in the Steam Deck's 720p mode (scripts/render_720p.py) the game asks for
    // 1280x720 decoders and the collection's movies are 1080p: their frames did not fit its
    // 720p buffers (green and pink logo screens). FitToMaxSize makes them fit.
    // The game's 720p movie textures put the chroma plane after 768 luma rows (rows aligned to
    // 64; 1080p frames come out at 1088 rows either way).
    u32 height_align = 16;
    if (m_max_width && m_max_height &&
        (u32(frame->width) > m_max_width || u32(frame->height) > m_max_height)) {
        if (AVFrame* fitted = FitToMaxSize(*frame)) {
            av_frame_free(&frame);
            frame = fitted;
            height_align = 64;
        }
    }

    Output(*frame, frame_buffer, output_info, height_align);

    av_frame_free(&frame);
    return ORBIS_OK;
}

void VdecDecoder::Output(AVFrame& frame, OrbisVideodec2FrameBuffer& frame_buffer,
                         OrbisVideodec2OutputInfo& output_info, u32 height_align) {
    const bool is_avc = m_codec_context->codec_id == AV_CODEC_ID_H264;
    const u64 info_size =
        is_avc ? sizeof(OrbisVideodec2AvcPictureInfo) : sizeof(OrbisVideodec2HevcPictureInfo);
    Videodec::CopyNV12Data((u8*)frame_buffer.frame_buffer,
                           frame_buffer.frame_buffer_size - info_size, frame, height_align);
    // Render resolution (bbport_movie.h): the decoded layout (chroma after the luma rows aligned
    // to 16), and with a copy scaled to the movie display size the frame is reported at that size:
    // the movie player's conversion shader spans the frame size it is given.
    const u64 base = (u64)frame_buffer.frame_buffer;
    BbMovie::RecordFrame(base, (u32)frame.width, (u32)frame.height, Common::AlignUp<u32>(frame.width, 64),
                         Common::AlignUp<u32>(frame.height, height_align));
    u32 shown_width = (u32)frame.width, shown_height = (u32)frame.height;
    u32 shown_align = height_align; // the Deck's 720p fit (FitToMaxSize) aligns rows to 64
    if (WriteMovieCopy(frame, base)) {
        BbMovie::DisplaySize(shown_width, shown_height);
        shown_align = 16;
    }
    frame_buffer.is_accepted = true;

    const auto width = Common::AlignUp<u32>(shown_width, 16);
    const auto pitch = Common::AlignUp<u32>(shown_width, 64);
    const auto height = Common::AlignUp<u32>(shown_height, shown_align);
    // The picture info follows the decoded frame (its own size, not the display size).
    const auto decoded_pitch = Common::AlignUp<u32>(frame.width, 64);
    const auto decoded_height = Common::AlignUp<u32>(frame.height, height_align);

    output_info.is_valid = true;
    output_info.is_error_frame = false;
    output_info.picture_count = 1; // TODO: 2 pictures for interlaced video
    output_info.codec_type = is_avc ? OrbisVideodec2CodecType::Avc : OrbisVideodec2CodecType::Hevc;
    output_info.frame_width = width;
    output_info.frame_pitch = pitch;
    output_info.frame_height = height;
    output_info.frame_buffer = frame_buffer.frame_buffer;
    output_info.frame_buffer_size = (decoded_pitch * decoded_height * 3) / 2;

    // Only set frameFormat and framePitchInBytes if the game uses the newer struct version.
    if (output_info.this_size == sizeof(OrbisVideodec2OutputInfo)) {
        output_info.frame_format = 0;
        output_info.frame_pitch_in_bytes = pitch;
    }

    if (is_avc) {
        auto& picture_info = *(OrbisVideodec2AvcPictureInfo*)((u8*)output_info.frame_buffer +
                                                              output_info.frame_buffer_size);

        picture_info = {};
        picture_info.is_valid = true;

        picture_info.pts_data = frame.pts;
        picture_info.dts_data = frame.pkt_dts;
        picture_info.attached_data = reinterpret_cast<u64>(frame.opaque);

        picture_info.frame_crop_top_offset = 0;
        picture_info.frame_crop_left_offset = 0;
        picture_info.frame_crop_right_offset = pitch - shown_width;
        picture_info.frame_crop_bottom_offset = height - shown_height;
    } else {
        auto& picture_info = *(OrbisVideodec2HevcPictureInfo*)((u8*)output_info.frame_buffer +
                                                               output_info.frame_buffer_size);
        picture_info = {};
        picture_info.is_valid = true;

        picture_info.pts_data = frame.pts;
        picture_info.dts_data = frame.pkt_dts;
        picture_info.attached_data = reinterpret_cast<u64>(frame.opaque);

        picture_info.frame_crop_top_offset = 0;
        picture_info.frame_crop_left_offset = 0;
        picture_info.frame_crop_right_offset = pitch - shown_width;
        picture_info.frame_crop_bottom_offset = height - shown_height;
    }
}

// Render resolution (BB_MOVIE_SIZE, bbport_movie.h; after BilalReloaded's Uncharted PC port): the
// frame also scaled to the display size the movie player reads it with, into the scratch frame
// of the game's frame buffer at base. True when written.
bool VdecDecoder::WriteMovieCopy(const AVFrame& frame, u64 base) {
    u32 width = 0, height = 0;
    if (!BbMovie::DisplaySize(width, height) ||
        ((u32)frame.width == width && (u32)frame.height == height)) {
        return false;
    }
    u8* dst = BbMovie::ScratchFrame(base);
    if (!dst) {
        return false;
    }
    if (!m_scaled) {
        m_scaled = av_frame_alloc();
    }
    if (!m_scaled) {
        return false;
    }
    // The movie keeps its own shape inside the display size: black bars at the top and bottom
    // (16:10) or at the sides (ultrawide) instead of a stretched picture. 16:9 sizes fill it.
    u32 fit_width = width, fit_height = height;
    if ((u64)frame.width * height > (u64)frame.height * width) {
        fit_height = std::min(height, (u32)((u64)width * frame.height / frame.width) & ~1u);
    } else {
        fit_width = std::min(width, (u32)((u64)height * frame.width / frame.height) & ~1u);
    }
    const u32 left = (width - fit_width) / 2 & ~1u, top = (height - fit_height) / 2 & ~1u;
    if ((u32)m_scaled->width != fit_width || (u32)m_scaled->height != fit_height) {
        av_frame_unref(m_scaled);
        m_scaled->format = AV_PIX_FMT_NV12;
        m_scaled->width = (int)fit_width;
        m_scaled->height = (int)fit_height;
        if (av_frame_get_buffer(m_scaled, 0) < 0) {
            av_frame_unref(m_scaled);
            return false;
        }
    }
    m_scale_context = sws_getCachedContext(m_scale_context, frame.width, frame.height,
                                           AV_PIX_FMT_NV12, (int)fit_width, (int)fit_height,
                                           AV_PIX_FMT_NV12, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!m_scale_context || sws_scale(m_scale_context, frame.data, frame.linesize, 0, frame.height,
                                      m_scaled->data, m_scaled->linesize) < 0) {
        return false;
    }
    const u32 pitch = BbMovie::ScaledPitch(), rows = BbMovie::ScaledRows();
    const u32 chroma_pitch = BbMovie::ScaledChromaPitch();
    const u64 bytes = BbMovie::ScaledFrameBytes();
    // Black (video range) around the picture.
    m_movie_copy.assign(bytes, 128);
    u8* copy = m_movie_copy.data();
    u8* chroma = copy + (u64)pitch * rows;
    std::memset(copy, 16, (u64)pitch * rows);
    for (u32 y = 0; y < fit_height; ++y) {
        std::memcpy(copy + (u64)(top + y) * pitch + left,
                    m_scaled->data[0] + (u64)y * m_scaled->linesize[0], fit_width);
        if (y < fit_height / 2) {
            std::memcpy(chroma + (u64)(top / 2 + y) * chroma_pitch + left,
                        m_scaled->data[1] + (u64)y * m_scaled->linesize[1], fit_width);
        }
    }
    // The rows up to the aligned height repeat the last one (sampling at the bottom edge reads
    // them at heights that aren't a multiple of 16, e.g. 1620).
    for (u32 y = height; y < rows; ++y) {
        std::memcpy(copy + (u64)y * pitch, copy + (u64)(height - 1) * pitch, width);
    }
    for (u32 y = height / 2; y < rows / 2; ++y) {
        std::memcpy(chroma + (u64)y * chroma_pitch, chroma + (u64)(height / 2 - 1) * chroma_pitch,
                    width);
    }
    // The GPU caches learn of the new frame explicitly (one write through the backing view, no
    // page faults per row). Invalidated before (any GPU copy of these pages goes back first)
    // and after the write.
    auto* memory = Core::Memory::Instance();
    const VAddr address = reinterpret_cast<VAddr>(dst);
    memory->InvalidateMemory(address, bytes);
    if (!memory->TryWriteBacking(dst, copy, bytes)) {
        std::memcpy(dst, copy, bytes);
    }
    memory->InvalidateMemory(address, bytes);
    return true;
}

s32 VdecDecoder::Reset() {
    avcodec_flush_buffers(m_codec_context);
    return ORBIS_OK;
}

AVFrame* VdecDecoder::FitToMaxSize(AVFrame& frame) {
    // The frame handed over has the decoder's maximum size. The collection's movie shader in its
    // 720p mode samples the picture at 720p/1080p of each screen position from the top left
    // (its 1080p movies were made for 1080p): the picture is scaled to that share of the frame,
    // top left, the rest black, and comes out full screen.
    const u32 width = m_max_width & ~1u;
    const u32 height = m_max_height & ~1u;
    // Rounded up to even sizes: the shader reaches 1280 * 2/3 = 853.3 (852 left a bright line
    // at the right edge of the screen).
    const auto share = [](u32 size, int full) {
        const u64 span = u64(size) * size;
        return std::min<u32>(size, u32((span + u32(full) - 1) / u32(full) + 1) & ~1u);
    };
    const u32 pic_width = share(width, std::max(frame.width, 1));
    const u32 pic_height = share(height, std::max(frame.height, 1));
    static bool logged = false;
    if (!logged) {
        logged = true;
        std::printf("Videodec2: %dx%d frames as %ux%u pictures in %ux%u frames\n", frame.width,
                    frame.height, pic_width, pic_height, width, height);
    }
    AVFrame* fitted = av_frame_alloc();
    fitted->pts = frame.pts;
    fitted->pkt_dts = frame.pkt_dts;
    fitted->format = AV_PIX_FMT_NV12;
    fitted->width = int(width);
    fitted->height = int(height);
    fitted->sample_aspect_ratio = frame.sample_aspect_ratio;
    fitted->opaque = frame.opaque;
    av_frame_get_buffer(fitted, 0);
    for (u32 y = 0; y < height; ++y) {
        std::memset(fitted->data[0] + y * fitted->linesize[0], 16, width); // black
    }
    for (u32 y = 0; y < height / 2; ++y) {
        std::memset(fitted->data[1] + y * fitted->linesize[1], 128, width);
    }
    m_fit_context = sws_getCachedContext(m_fit_context, frame.width, frame.height, AV_PIX_FMT_NV12,
                                         int(pic_width), int(pic_height), AV_PIX_FMT_NV12,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_fit_context || sws_scale(m_fit_context, frame.data, frame.linesize, 0, frame.height,
                                    fitted->data, fitted->linesize) < 0) {
        av_frame_free(&fitted);
        return nullptr;
    }
    return fitted;
}

AVFrame* VdecDecoder::ConvertNV12Frame(AVFrame& frame) {
    AVFrame* nv12_frame = av_frame_alloc();
    nv12_frame->pts = frame.pts;
    nv12_frame->pkt_dts = frame.pkt_dts < 0 ? 0 : frame.pkt_dts;
    nv12_frame->format = AV_PIX_FMT_NV12;
    nv12_frame->width = frame.width;
    nv12_frame->height = frame.height;
    nv12_frame->sample_aspect_ratio = frame.sample_aspect_ratio;
    nv12_frame->crop_top = frame.crop_top;
    nv12_frame->crop_bottom = frame.crop_bottom;
    nv12_frame->crop_left = frame.crop_left;
    nv12_frame->crop_right = frame.crop_right;
    nv12_frame->opaque = frame.opaque;

    av_frame_get_buffer(nv12_frame, 0);

    if (m_sws_context == nullptr) {
        m_sws_context = sws_getContext(frame.width, frame.height, AVPixelFormat(frame.format),
                                       nv12_frame->width, nv12_frame->height, AV_PIX_FMT_NV12,
                                       SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    }

    const auto res = sws_scale(m_sws_context, frame.data, frame.linesize, 0, frame.height,
                               nv12_frame->data, nv12_frame->linesize);
    if (res < 0) {
        LOG_ERROR(Lib_Vdec2, "Could not convert to NV12: {}", av_err2str(res));
        return nullptr;
    }

    return nv12_frame;
}

} // namespace Libraries::Videodec2
