// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>
#include <fmt/format.h>
#include "core/emulator_settings.h"
#include "shader_recompiler/ir/program.h"
// After program.h: resource_pass.h uses IR::Inst without including it.
#include "shader_recompiler/ir/passes/resource_pass.h"
#include "shader_recompiler/profile.h"
#include "video_core/buffer_cache/buffer_cache.h"

namespace Shader::Optimization {

// bbport: how much of each buffer the shader reads (BufferResource::read_bytes). Every read whose
// position has a known upper bound extends the range; any other access (a store, a formatted
// load, a position without a bound) makes it unknown: the whole buffer.

// The largest value `value` can take, when the expression bounds it (constants, sums, products,
// shifts and masks of them); none otherwise or when 32-bit arithmetic could wrap.
std::optional<u64> UpperBound(const IR::Value& value, int depth = 0) {
    const IR::Value& v = value;
    if (v.IsImmediate()) {
        return v.Type() == IR::Type::U32 ? std::optional<u64>{v.U32()} : std::nullopt;
    }
    if (depth > 16) {
        return std::nullopt;
    }
    const IR::Inst* inst = v.Inst();
    const auto arg = [&](u32 i) { return UpperBound(inst->Arg(i), depth + 1); };
    const auto fits = [](u64 x) { return x <= 0xFFFFFFFFull ? std::optional<u64>{x} : std::nullopt; };
    const auto imm = [&](u32 i) -> std::optional<u64> {
        const IR::Value a = inst->Arg(i);
        if (a.IsImmediate() && a.Type() == IR::Type::U32) {
            return a.U32();
        }
        return std::nullopt;
    };
    switch (inst->GetOpcode()) {
    case IR::Opcode::IAdd32: {
        const auto a = arg(0), b = arg(1);
        return a && b ? fits(*a + *b) : std::nullopt;
    }
    case IR::Opcode::IMul32: {
        const auto a = arg(0), b = arg(1);
        return a && b ? fits(*a * *b) : std::nullopt;
    }
    case IR::Opcode::ShiftLeftLogical32: {
        const auto a = arg(0), s = imm(1);
        return a && s && *s < 32 ? fits(*a << *s) : std::nullopt;
    }
    case IR::Opcode::ShiftRightLogical32: {
        const auto a = arg(0), s = imm(1);
        if (s && *s < 32) {
            return (a ? *a : 0xFFFFFFFFull) >> *s;
        }
        return std::nullopt;
    }
    case IR::Opcode::BitwiseAnd32: {
        const auto a = arg(0), b = arg(1);
        if (a && b) {
            return std::min(*a, *b);
        }
        return a ? a : b;
    }
    case IR::Opcode::BitwiseOr32: {
        const auto a = arg(0), b = arg(1);
        if (!a || !b) {
            return std::nullopt;
        }
        return (u64(1) << std::bit_width(std::max(*a, *b))) - 1;
    }
    case IR::Opcode::UMin32: {
        const auto a = arg(0), b = arg(1);
        if (a && b) {
            return std::min(*a, *b);
        }
        return a ? a : b;
    }
    case IR::Opcode::BitFieldUExtract: {
        const auto count = imm(2);
        return count && *count < 32 ? std::optional<u64>{(u64(1) << *count) - 1} : std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

/// Bytes past the end of what `inst` reads, when bounded: the position argument's units are those
/// of the access (dwords for 32-bit loads and constant reads; BufferAddressShift).
std::optional<u64> ReadEnd(const IR::Inst& inst) {
    const auto at = [&](u64 unit, u64 count) -> std::optional<u64> {
        const auto bound = UpperBound(inst.Arg(1));
        return bound ? std::optional<u64>{(*bound + count) * unit} : std::nullopt;
    };
    switch (inst.GetOpcode()) {
    case IR::Opcode::ReadConstBuffer:
    case IR::Opcode::LoadBufferU32:
    case IR::Opcode::LoadBufferF32:
        return at(4, 1);
    case IR::Opcode::LoadBufferU32x2:
    case IR::Opcode::LoadBufferF32x2:
        return at(4, 2);
    case IR::Opcode::LoadBufferU32x3:
    case IR::Opcode::LoadBufferF32x3:
        return at(4, 3);
    case IR::Opcode::LoadBufferU32x4:
    case IR::Opcode::LoadBufferF32x4:
        return at(4, 4);
    case IR::Opcode::LoadBufferU8:
        return at(1, 1);
    case IR::Opcode::LoadBufferU16:
        return at(2, 1);
    case IR::Opcode::LoadBufferU64:
        return at(8, 1);
    default:
        return std::nullopt; // formatted loads, stores, atomics
    }
}

void VisitBufferRange(Info& info, const IR::Inst& inst, std::vector<std::string>* report) {
    if (!IsBufferInstruction(inst) || !inst.Arg(0).IsImmediate()) {
        return;
    }
    const u32 handle = inst.Arg(0).U32();
    if (handle >= info.buffers.size()) {
        return;
    }
    auto& buffer = info.buffers[handle];
    // An indexed load's position depends on the buffer's stride, which permutations may differ in.
    const bool indexed = inst.GetOpcode() != IR::Opcode::ReadConstBuffer &&
                         inst.Flags<IR::BufferInstInfo>().index_enable;
    const auto end = buffer.is_formatted || indexed ? std::nullopt : ReadEnd(inst);
    if (end) {
        buffer.read_bytes = u32(std::min<u64>(std::max<u64>(buffer.read_bytes, *end), UINT32_MAX));
        return;
    }
    if (buffer.read_range_known && report) {
        const IR::Value position = inst.NumArgs() > 1 ? inst.Arg(1) : IR::Value{};
        report->push_back(fmt::format(
            "buffer {}: {} at {}", handle, IR::NameOf(inst.GetOpcode()),
            position.IsEmpty()       ? std::string{"-"}
            : position.IsImmediate() ? std::string{"constant"}
                                     : std::string{IR::NameOf(position.Inst()->GetOpcode())}));
    }
    buffer.read_range_known = false;
}

void Visit(Info& info, const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::GetAttribute:
    case IR::Opcode::GetAttributeU1:
    case IR::Opcode::GetAttributeU32:
        info.loads.Set(inst.Arg(0).Attribute(), inst.Arg(1).U32());
        break;
    case IR::Opcode::SetAttribute:
        info.stores.Set(inst.Arg(0).Attribute(), inst.Arg(2).U32());
        break;
    case IR::Opcode::GetUserData:
        info.ud_mask.Set(inst.Arg(0).ScalarReg());
        break;
    case IR::Opcode::SetPatch: {
        const auto patch = inst.Arg(0).Patch();
        if (patch <= IR::Patch::TessellationLodBottom) {
            info.stores_tess_level_outer = true;
        } else if (patch <= IR::Patch::TessellationLodInteriorV) {
            info.stores_tess_level_inner = true;
        } else {
            info.uses_patches |= 1U << IR::GenericPatchIndex(patch);
        }
        break;
    }
    case IR::Opcode::GetPatch: {
        const auto patch = inst.Arg(0).Patch();
        info.uses_patches |= 1U << IR::GenericPatchIndex(patch);
        break;
    }
    case IR::Opcode::LoadSharedU16:
    case IR::Opcode::WriteSharedU16:
        info.shared_types |= IR::Type::U16;
        break;
    case IR::Opcode::LoadSharedU32:
    case IR::Opcode::WriteSharedU32:
    case IR::Opcode::SharedAtomicIAdd32:
    case IR::Opcode::SharedAtomicISub32:
    case IR::Opcode::SharedAtomicSMin32:
    case IR::Opcode::SharedAtomicUMin32:
    case IR::Opcode::SharedAtomicSMax32:
    case IR::Opcode::SharedAtomicUMax32:
    case IR::Opcode::SharedAtomicInc32:
    case IR::Opcode::SharedAtomicDec32:
    case IR::Opcode::SharedAtomicAnd32:
    case IR::Opcode::SharedAtomicOr32:
    case IR::Opcode::SharedAtomicXor32:
    case IR::Opcode::SharedAtomicCmpSwap32:
        info.shared_types |= IR::Type::U32;
        break;
    case IR::Opcode::SharedAtomicIAdd64:
    case IR::Opcode::SharedAtomicISub64:
    case IR::Opcode::SharedAtomicSMin64:
    case IR::Opcode::SharedAtomicUMin64:
    case IR::Opcode::SharedAtomicSMax64:
    case IR::Opcode::SharedAtomicUMax64:
    case IR::Opcode::SharedAtomicInc64:
    case IR::Opcode::SharedAtomicDec64:
    case IR::Opcode::SharedAtomicAnd64:
    case IR::Opcode::SharedAtomicOr64:
    case IR::Opcode::SharedAtomicXor64:
    case IR::Opcode::SharedAtomicCmpSwap64:
        info.uses_shared_int64_atomics = true;
        [[fallthrough]];
    case IR::Opcode::LoadSharedU64:
    case IR::Opcode::WriteSharedU64:
        info.shared_types |= IR::Type::U64;
        break;
    case IR::Opcode::ConvertF16F32:
    case IR::Opcode::ConvertF32F16:
    case IR::Opcode::BitCastU16F16:
    case IR::Opcode::BitCastF16U16:
        info.uses_fp16 = true;
        break;
    case IR::Opcode::PackDouble2x32:
    case IR::Opcode::UnpackDouble2x32:
        info.uses_fp64 = true;
        break;
    case IR::Opcode::ImageWrite:
        info.has_storage_images = true;
        break;
    case IR::Opcode::QuadBroadcast:
        info.uses_group_quad = true;
        break;
    case IR::Opcode::Shuffle:
    case IR::Opcode::ShuffleXor:
        info.uses_group_shuffle = true;
        break;
    case IR::Opcode::ReadLane:
    case IR::Opcode::ReadFirstLane:
    case IR::Opcode::WriteLane:
        info.uses_group_ballot = true;
        break;
    case IR::Opcode::Discard:
    case IR::Opcode::DiscardCond:
        info.has_discard = true;
        break;
    case IR::Opcode::BitwiseXor32:
        info.has_bitwise_xor = true;
        break;
    case IR::Opcode::ImageGather:
    case IR::Opcode::ImageGatherDref:
        info.has_image_gather = true;
        break;
    case IR::Opcode::ImageQueryDimensions:
    case IR::Opcode::ImageQueryLod:
        info.has_image_query = true;
        break;
    case IR::Opcode::ImageAtomicFMax32:
    case IR::Opcode::ImageAtomicFMin32:
        info.uses_image_atomic_float_min_max = true;
        break;
    case IR::Opcode::BufferAtomicFMax32:
    case IR::Opcode::BufferAtomicFMin32:
        info.uses_buffer_atomic_float_min_max = true;
        break;
    case IR::Opcode::BufferAtomicIAdd64:
    case IR::Opcode::BufferAtomicSMax64:
    case IR::Opcode::BufferAtomicSMin64:
    case IR::Opcode::BufferAtomicUMax64:
    case IR::Opcode::BufferAtomicUMin64:
        info.uses_buffer_int64_atomics = true;
        break;
    case IR::Opcode::DataAppend:
    case IR::Opcode::DataConsume:
    case IR::Opcode::Ballot:
    case IR::Opcode::InverseBallot:
    case IR::Opcode::BallotFindLsb:
        info.uses_group_ballot = true;
        [[fallthrough]];
    case IR::Opcode::LaneId:
        info.uses_lane_id = true;
        break;
    case IR::Opcode::Memtime:
        info.uses_shader_clock = true;
        break;
    case IR::Opcode::ReadConst:
        if (!info.has_readconst) {
            info.buffers.push_back({
                .used_types = IR::Type::U32,
                .buffer_type = BufferType::Flatbuf,
            });
            info.has_readconst = true;
        }
        if (inst.Flags<u32>() == 0) {
            info.readconst_types |= Info::ReadConstType::Immediate;
            info.readconst_types |= Info::ReadConstType::Dynamic;
            info.uses_dma = true;
        }
        break;
    case IR::Opcode::PackUfloat10_11_11:
        info.uses_pack_10_11_11 = true;
        break;
    case IR::Opcode::UnpackUfloat10_11_11:
        info.uses_unpack_10_11_11 = true;
        break;
    default:
        break;
    }
}

void CollectShaderInfoPass(IR::Program& program, const Profile& profile) {
    Info& info = program.info;
    for (auto& buffer : info.buffers) {
        buffer.read_bytes = 0;
        buffer.read_range_known = buffer.buffer_type == BufferType::Guest;
    }
    // BB_RING_REPORT=1: why a buffer's range stays unknown, per compiled shader.
    static const bool ring_report = std::getenv("BB_RING_REPORT") != nullptr;
    std::vector<std::string> report;
    for (IR::Block* const block : program.post_order_blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            VisitBufferRange(info, inst, ring_report ? &report : nullptr);
            Visit(info, inst);
        }
    }
    if (ring_report) {
        std::string known;
        for (u32 i = 0; i < info.buffers.size(); ++i) {
            if (info.buffers[i].read_range_known) {
                known += fmt::format(" {}:{}B", i, info.buffers[i].read_bytes);
            }
        }
        std::printf("Ring report: shader %08x known [%s ]; unknown:", u32(info.pgm_hash),
                    known.c_str());
        for (const auto& line : report) {
            std::printf(" (%s)", line.c_str());
        }
        std::printf("\n");
    }

    if (!EmulatorSettings.IsDirectMemoryAccessEnabled()) {
        info.uses_dma = false;
        info.readconst_types = Info::ReadConstType::None;
    }

    if (info.uses_dma) {
        info.buffers.push_back({
            .used_types = IR::Type::U64,
            .buffer_type = BufferType::BdaPagetable,
            .is_written = true,
        });
        info.buffers.push_back({
            .used_types = IR::Type::U32,
            .buffer_type = BufferType::FaultBuffer,
            .is_written = true,
        });
        LOG_ERROR(Render, "Enabling DMA for shader {:#x}", info.pgm_hash);
    }
}

} // namespace Shader::Optimization
