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
// value z. Only s * z changes; t, the sample itself and everything else stay.

#include <cstdio>
#include <vector>
#include "shader_recompiler/ir/basic_block.h"
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
                    if (paired && std::ranges::none_of(targets, [&](const auto& seen) {
                            return seen.first == s_mul;
                        })) {
                        targets.emplace_back(s_mul, z);
                    }
                }
            }
        }
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
    if (logged < 16) {
        ++logged;
        std::printf("Stereo 3D: positions rebuilt from depth moved per eye in ps %08x (%zu)\n",
                    u32(program.info.pgm_hash), targets.size());
    }
}

} // namespace Shader::Optimization
