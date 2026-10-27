// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: stereo 3D in one pass (stereo.h). Full-screen passes that rebuild positions from the
// scene depth (Uncharted's sun shadow pass f614041e) read the depth at their texture coordinate
// (s, t) and form the position as (s * z, t * z, z) for the game's matrices, which are the centre
// camera's. Drawn once per eye, s is where the eye's picture shows the point, not where the
// centre camera does: the shadows of each eye were looked up a parallax away, and on surfaces at
// an angle the right eye's points fell behind the surface and were shadowed (a crate lit in the
// left eye, dark in the right). The product s * z is replaced by StereoUnshiftX, which moves it
// back by the eye's shift at depth z (emit_spirv_special.cpp).
//
// The pattern: two consecutive coordinates s, t of an image sample, each multiplied by the same
// value z, where z is computed from a sample at (s, t) of a per-eye image (the depth: in a
// shader that merely scales a coordinate the multiplier is a constant or a uniform, never a
// value read at the coordinate). Only s * z changes; t, the sample itself and everything else
// stay. No per-game list: every fragment shader of the geo mode is examined, so a variant of
// the shadow pass (another cascade count, a character option) or a deferred pass with the same
// construction is corrected too.

#include <cstdio>
#include <unordered_set>
#include <vector>
#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/stereo.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/program.h"

namespace Shader::Optimization {

namespace {
void Flatten(const IR::Value& value, std::vector<IR::Value>& out) {
    if (!value.IsImmediate()) {
        if (const IR::Inst* inst = value.TryInst()) {
            switch (inst->GetOpcode()) {
            case IR::Opcode::CompositeConstructF32x2:
            case IR::Opcode::CompositeConstructF32x3:
            case IR::Opcode::CompositeConstructF32x4:
            case IR::Opcode::CompositeConstructF32x2x2:
                for (size_t i = 0; i < inst->NumArgs(); ++i) {
                    Flatten(inst->Arg(i), out);
                }
                return;
            default:
                break;
            }
        }
    }
    out.push_back(value);
}

/// The coordinates an image sample reads, in address order.
std::vector<IR::Value> SampleCoords(const IR::Inst& inst) {
    std::vector<IR::Value> coords;
    switch (inst.GetOpcode()) {
    case IR::Opcode::ImageSampleRaw:
        for (size_t i = 2; i < 5; ++i) {
            Flatten(inst.Arg(i), coords);
        }
        break;
    case IR::Opcode::ImageSampleImplicitLod:
    case IR::Opcode::ImageSampleExplicitLod:
    case IR::Opcode::ImageSampleDrefImplicitLod:
    case IR::Opcode::ImageSampleDrefExplicitLod:
    case IR::Opcode::ImageGather:
    case IR::Opcode::ImageGatherDref:
    case IR::Opcode::ImageRead:
        Flatten(inst.Arg(1), coords);
        break;
    default:
        break;
    }
    return coords;
}

bool IsSample(IR::Opcode op) {
    switch (op) {
    case IR::Opcode::ImageSampleRaw:
    case IR::Opcode::ImageSampleImplicitLod:
    case IR::Opcode::ImageSampleExplicitLod:
    case IR::Opcode::ImageSampleDrefImplicitLod:
    case IR::Opcode::ImageSampleDrefExplicitLod:
    case IR::Opcode::ImageGather:
    case IR::Opcode::ImageGatherDref:
    case IR::Opcode::ImageRead:
        return true;
    default:
        return false;
    }
}

/// Arithmetic that carries a sampled depth to the multiplier (z = c / (d - c0), scales, ...).
bool CarriesValue(IR::Opcode op) {
    switch (op) {
    case IR::Opcode::FPMul32:
    case IR::Opcode::FPDiv32:
    case IR::Opcode::FPRecip32:
    case IR::Opcode::FPAdd32:
    case IR::Opcode::FPSub32:
    case IR::Opcode::FPFma32:
    case IR::Opcode::FPNeg32:
    case IR::Opcode::FPAbs32:
    case IR::Opcode::FPMax32:
    case IR::Opcode::FPMin32:
    case IR::Opcode::FPSaturate32:
    case IR::Opcode::FPClamp32:
    case IR::Opcode::CompositeExtractF32x2:
    case IR::Opcode::CompositeExtractF32x3:
    case IR::Opcode::CompositeExtractF32x4:
    case IR::Opcode::BitCastF32U32:
    case IR::Opcode::BitCastU32F32:
    case IR::Opcode::SelectF32:
        return true;
    default:
        return false;
    }
}

/// Whether `value` is computed from a sample of a per-eye image taken at coordinate `coord`
/// (within a few arithmetic steps): the multiplier is then that pixel's depth.
bool FromDepthAt(const IR::Value& value, const IR::Value& coord, const Info& info,
                 std::unordered_set<const IR::Inst*>& seen, u32 depth) {
    if (value.IsImmediate() || depth > 12) {
        return false;
    }
    const IR::Inst* inst = value.TryInst();
    if (!inst || !seen.insert(inst).second) {
        return false;
    }
    if (IsSample(inst->GetOpcode())) {
        std::vector<IR::Value> coords;
        // the coordinate operands of this sample (same layout as SampleCoords)
        switch (inst->GetOpcode()) {
        case IR::Opcode::ImageSampleRaw:
            for (size_t i = 2; i < 5; ++i) {
                Flatten(inst->Arg(i), coords);
            }
            break;
        default:
            Flatten(inst->Arg(1), coords);
            break;
        }
        if (std::ranges::find(coords, coord) == coords.end()) {
            return false;
        }
        // a per-eye image: the handle names the image resource (set by the resource pass)
        const IR::Value handle = inst->Arg(0);
        if (!handle.IsImmediate()) {
            return false;
        }
        const u32 index = handle.U32() & 0xFFFF;
        if (index >= info.images.size()) {
            return false;
        }
        const auto& res = info.images[index];
        return Stereo::ImageSharp(res.GetSharp(info), res.is_array);
    }
    if (!CarriesValue(inst->GetOpcode())) {
        return false;
    }
    for (size_t i = 0; i < inst->NumArgs(); ++i) {
        if (FromDepthAt(inst->Arg(i), coord, info, seen, depth + 1)) {
            return true;
        }
    }
    return false;
}

/// Multiplies of `factor` by another value: (multiply, other operand).
std::vector<std::pair<IR::Inst*, IR::Value>> Products(const IR::Value& factor) {
    std::vector<std::pair<IR::Inst*, IR::Value>> out;
    if (factor.IsImmediate() || !factor.TryInst()) {
        return out;
    }
    for (const auto& use : factor.Inst()->Uses()) {
        IR::Inst* user = use.user;
        if (user->GetOpcode() != IR::Opcode::FPMul32 || use.operand > 1) {
            continue;
        }
        const IR::Value other = user->Arg(1 - use.operand);
        if (!other.IsImmediate()) {
            out.emplace_back(user, other);
        }
    }
    return out;
}
} // namespace

void StereoUnshiftPass(IR::Program& program) {
    const Info& info = program.info;
    std::vector<std::pair<IR::Inst*, IR::Value>> targets; // s * z, z
    for (IR::Block* const block : program.blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            const auto coords = SampleCoords(inst);
            for (size_t c = 0; c + 1 < coords.size(); ++c) {
                const auto s_products = Products(coords[c]);
                const auto t_products = Products(coords[c + 1]);
                for (const auto& [s_mul, z] : s_products) {
                    const bool paired = std::ranges::any_of(
                        t_products, [&](const auto& t) { return t.second == z; });
                    if (!paired || std::ranges::any_of(targets, [&](const auto& seen) {
                            return seen.first == s_mul;
                        })) {
                        continue;
                    }
                    std::unordered_set<const IR::Inst*> seen;
                    if (FromDepthAt(z, coords[c], info, seen, 0)) {
                        targets.emplace_back(s_mul, z);
                    }
                }
            }
        }
    }
    if (targets.empty()) {
        return;
    }
    for (auto& [s_mul, z] : targets) {
        IR::Block* block = s_mul->GetParent();
        auto next = IR::Block::InstructionList::s_iterator_to(*s_mul);
        ++next;
        IR::IREmitter ir{*block, next};
        const IR::F32 moved = ir.StereoUnshiftX(IR::F32{IR::Value{s_mul}}, IR::F32{z});
        const auto uses = std::vector(s_mul->Uses().begin(), s_mul->Uses().end());
        for (const auto& use : uses) {
            if (use.user != moved.Inst()) {
                use.user->SetArg(use.operand, moved);
            }
        }
    }
    static u32 logged = 0;
    if (logged < 32) {
        ++logged;
        std::printf("Stereo 3D: positions rebuilt from depth moved per eye in ps %08x (%zu)\n",
                    u32(program.info.pgm_hash), targets.size());
    }
}

} // namespace Shader::Optimization
