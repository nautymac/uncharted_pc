// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: stereo 3D in one pass (stereo.h). Full-screen passes that rebuild positions from the
// scene depth (Uncharted's sun shadow pass, its fog passes) read the depth at their texture
// coordinate (s, t) and form the position as z * (a*s + b*t + c) for the game's matrices, which
// are the centre camera's: the sun shadow pass as (s*z, t*z, z), the fog passes with the
// coordinate first mapped to NDC, (2*s - 1) * c0 * z. Drawn once per eye, s is where the eye's
// picture shows the point, not where the centre camera does: the shadows of each eye were looked
// up a parallax away, and on surfaces at an angle the right eye's points fell behind the surface
// and were shadowed (a crate lit in the left eye, dark in the right). Each such product gets
// moved back by the eye's shift at depth z (emit_spirv_special.cpp): with the eye's shift of s
// being ds = 1/2 * eye_sep * (1 - conv / z), the product z * E(s) with dE/ds = a changes by
// a * z * ds = a * 1/2 * eye_sep * (z - conv), which is subtracted.
//
// The pattern, in every fragment shader of the geo mode (no per-game list, so a variant of the
// shadow pass or another deferred pass with the same construction is corrected too): a multiply
// of Z by E where Z is computed from a sample at (s, t) of a per-eye image (the depth: in a
// shader that merely scales a coordinate the multiplier is a constant or a uniform, never a
// value read at the coordinate) and E is linear in s with a non-zero coefficient (built from the
// shader's own constants and uniforms). Only that product changes; t, the sample and everything
// else stay.

#include <cstdio>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/program.h"
#include "shader_recompiler/stereo.h"

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
        const auto coords = SampleCoords(*inst);
        if (std::ranges::find(coords, coord) == coords.end()) {
            return false;
        }
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

/// Whether `value` depends on the coordinate `s` through arithmetic. A sample (its result is a
/// texel, not the coordinate), a buffer load and an attribute other than s are boundaries.
bool DependsOn(const IR::Value& value, const IR::Value& s,
               std::unordered_map<const IR::Inst*, bool>& memo, u32 depth) {
    if (value == s) {
        return true;
    }
    if (value.IsImmediate() || depth > 24) {
        return false;
    }
    const IR::Inst* inst = value.TryInst();
    if (!inst) {
        return false;
    }
    if (const auto it = memo.find(inst); it != memo.end()) {
        return it->second;
    }
    memo[inst] = false; // cycles (phis) count as independent
    bool depends = false;
    if (!IsSample(inst->GetOpcode()) && inst->GetOpcode() != IR::Opcode::Phi) {
        for (size_t i = 0; i < inst->NumArgs() && !depends; ++i) {
            depends = DependsOn(inst->Arg(i), s, memo, depth + 1);
        }
    }
    memo[inst] = depends;
    return depends;
}

/// d(value)/ds as an IR value (emitted by `ir`), when `value` is linear in s through
/// multiplies by s-independent values, adds, subtracts, negations and fmas; nullopt otherwise.
/// A zero coefficient is an immediate 0.
std::optional<IR::F32> Coefficient(const IR::Value& value, const IR::Value& s, IR::IREmitter& ir,
                                   std::unordered_map<const IR::Inst*, bool>& memo, u32 depth) {
    if (value == s) {
        return IR::F32{ir.Imm32(1.0f)};
    }
    if (!DependsOn(value, s, memo, 0)) {
        return IR::F32{ir.Imm32(0.0f)};
    }
    if (depth > 24) {
        return std::nullopt;
    }
    const IR::Inst* inst = value.Inst();
    const auto is_zero = [](const IR::F32& v) { return v.IsImmediate() && v.F32() == 0.0f; };
    const auto add = [&](const IR::F32& a, const IR::F32& b) -> IR::F32 {
        if (is_zero(a)) {
            return b;
        }
        if (is_zero(b)) {
            return a;
        }
        return ir.FPAdd(a, b);
    };
    const auto mul = [&](const IR::F32& coeff, const IR::Value& factor) -> IR::F32 {
        if (is_zero(coeff)) {
            return coeff;
        }
        if (coeff.IsImmediate() && coeff.F32() == 1.0f) {
            return IR::F32{factor};
        }
        return ir.FPMul(coeff, IR::F32{factor});
    };
    switch (inst->GetOpcode()) {
    case IR::Opcode::FPAdd32: {
        const auto a = Coefficient(inst->Arg(0), s, ir, memo, depth + 1);
        const auto b = Coefficient(inst->Arg(1), s, ir, memo, depth + 1);
        if (!a || !b) {
            return std::nullopt;
        }
        return add(*a, *b);
    }
    case IR::Opcode::FPSub32: {
        const auto a = Coefficient(inst->Arg(0), s, ir, memo, depth + 1);
        const auto b = Coefficient(inst->Arg(1), s, ir, memo, depth + 1);
        if (!a || !b) {
            return std::nullopt;
        }
        return add(*a, is_zero(*b) ? *b : IR::F32{ir.FPNeg(*b)});
    }
    case IR::Opcode::FPNeg32: {
        const auto a = Coefficient(inst->Arg(0), s, ir, memo, depth + 1);
        if (!a) {
            return std::nullopt;
        }
        return is_zero(*a) ? *a : IR::F32{ir.FPNeg(*a)};
    }
    case IR::Opcode::FPMul32:
    case IR::Opcode::FPFma32: {
        const IR::Value x = inst->Arg(0), y = inst->Arg(1);
        const bool dx = DependsOn(x, s, memo, 0), dy = DependsOn(y, s, memo, 0);
        std::optional<IR::F32> product;
        if (dx && dy) {
            return std::nullopt; // not linear in s
        } else if (dx) {
            const auto a = Coefficient(x, s, ir, memo, depth + 1);
            if (!a) {
                return std::nullopt;
            }
            product = mul(*a, y);
        } else if (dy) {
            const auto b = Coefficient(y, s, ir, memo, depth + 1);
            if (!b) {
                return std::nullopt;
            }
            product = mul(*b, x);
        } else {
            product = IR::F32{ir.Imm32(0.0f)};
        }
        if (inst->GetOpcode() == IR::Opcode::FPMul32) {
            return product;
        }
        const auto c = Coefficient(inst->Arg(2), s, ir, memo, depth + 1);
        if (!c) {
            return std::nullopt;
        }
        return add(*product, *c);
    }
    default:
        return std::nullopt;
    }
}

struct Target {
    IR::Inst* product; ///< Z * E
    IR::Value z;       ///< the depth-derived operand
    IR::Value e;       ///< the operand that depends on s
    IR::Value s;       ///< the sample's first coordinate
};
} // namespace

void StereoUnshiftPass(IR::Program& program) {
    const Info& info = program.info;
    std::vector<Target> targets;
    std::unordered_set<const IR::Inst*> taken;
    for (IR::Block* const block : program.blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            const auto coords = SampleCoords(inst);
            if (coords.size() < 2 || coords[0].IsImmediate() || !coords[0].TryInst()) {
                continue;
            }
            // the sample's result must be a per-eye image read at these coordinates: every
            // multiply by a value derived from it is a candidate
            const IR::Value s = coords[0];
            std::vector<IR::Inst*> derived;
            std::vector<const IR::Inst*> frontier{&inst};
            std::unordered_set<const IR::Inst*> visited{&inst};
            while (!frontier.empty()) { // values computed from the sample within CarriesValue ops
                const IR::Inst* cur = frontier.back();
                frontier.pop_back();
                for (const auto& use : cur->Uses()) {
                    IR::Inst* user = use.user;
                    if (!visited.insert(user).second) {
                        continue;
                    }
                    if (user->GetOpcode() == IR::Opcode::FPMul32) {
                        derived.push_back(user);
                    }
                    if (CarriesValue(user->GetOpcode()) && visited.size() < 256) {
                        frontier.push_back(user);
                    }
                }
            }
            for (IR::Inst* mul : derived) {
                if (taken.contains(mul)) {
                    continue;
                }
                for (u32 side = 0; side < 2; ++side) {
                    const IR::Value z = mul->Arg(side), e = mul->Arg(1 - side);
                    std::unordered_set<const IR::Inst*> seen;
                    if (e.IsImmediate() || !FromDepthAt(z, s, info, seen, 0)) {
                        continue;
                    }
                    std::unordered_map<const IR::Inst*, bool> memo;
                    if (!DependsOn(e, s, memo, 0)) {
                        continue;
                    }
                    taken.insert(mul);
                    targets.push_back({mul, z, e, s});
                    break;
                }
            }
        }
    }
    u32 patched = 0;
    for (const auto& target : targets) {
        IR::Block* block = target.product->GetParent();
        auto next = IR::Block::InstructionList::s_iterator_to(*target.product);
        ++next;
        IR::IREmitter ir{*block, next};
        std::unordered_map<const IR::Inst*, bool> memo;
        const auto coeff = Coefficient(target.e, target.s, ir, memo, 0);
        if (!coeff || (coeff->IsImmediate() && coeff->F32() == 0.0f)) {
            continue; // not linear in s: left alone (the coefficient instructions, if any, are dead)
        }
        // shift = -1/2 * eye_sep * (z - conv); product' = product + coeff * shift
        const IR::F32 shift = ir.StereoUnshiftX(IR::F32{ir.Imm32(0.0f)}, IR::F32{target.z});
        const IR::F32 moved = ir.FPFma(*coeff, shift, IR::F32{IR::Value{target.product}});
        const auto uses = std::vector(target.product->Uses().begin(), target.product->Uses().end());
        for (const auto& use : uses) {
            if (use.user != moved.Inst() && use.user != coeff->TryInst()) {
                use.user->SetArg(use.operand, moved);
            }
        }
        ++patched;
    }
    if (!patched) {
        return;
    }
    static u32 logged = 0;
    if (logged < 32) {
        ++logged;
        std::printf("Stereo 3D: positions rebuilt from depth moved per eye in ps %08x (%u)\n",
                    u32(program.info.pgm_hash), patched);
    }
}

} // namespace Shader::Optimization
