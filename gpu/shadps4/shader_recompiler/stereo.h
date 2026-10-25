// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: stereo 3D in one pass (BbSettings::StereoGeo, vk_stereo.h). The scene targets get a
// second array layer and the scene passes draw both eyes with multiview: the vertex stage moves
// Position by the eye (emit_spirv_special.cpp), fragment and compute shaders read a target at
// the eye's layer (ImageSharp below), the presenter packs the display buffer's two layers side
// by side. Whether an image is such a target is this pure function of its guest description, so
// that the texture cache (creating the image), the pipeline key (from the render target
// registers, before any image is looked up, also on the draw-preparation workers) and the
// shader specialization (from the T# a shader reads) agree without sharing state.

#pragma once

#include <algorithm>

#include "common/types.h"
#include "video_core/amdgpu/pixel_format.h"
#include "video_core/amdgpu/resource.h"

namespace Shader::Stereo {

/// Set once at start (vk_rasterizer.cpp): the scene targets have two layers (the geo mode was
/// chosen at start and the device draws multiview).
inline bool geo_layers = false;

/// A scene-sized surface: 16:9 at a size a console game renders at (1920x1080, 960x540, ...).
/// The render target registers round the pitch and the height up (Uncharted: 1920x1080 color
/// as 1920x1088, 960x540 depth as 1024x640), while the extent hints and the T#s carry the
/// exact size: any 16:9 size within that rounding (up to 127 more in each) counts, so that
/// the texture cache, the pipeline key (registers only, also on the workers) and the shader
/// specialization agree.
inline bool SceneSize(u32 width, u32 height) {
    if (height < 135 || height > 4320) {
        return false;
    }
    // The 16:9 heights (multiples of 9) in (height - 128, height].
    for (u32 h = (height - std::min(height, 127u) + 8) / 9 * 9; h <= height; h += 9) {
        const u32 w = h / 9 * 16;
        if (h >= 135 && width >= w && width < w + 128) {
            return true;
        }
    }
    return false;
}

/// A T# of a two-layer scene target: the shader samples it as a 2D array at the eye's layer.
inline bool ImageSharp(const AmdGpu::Image& image, bool /*is_array*/) {
    return geo_layers && image.GetType() == AmdGpu::ImageType::Color2D &&
           image.NumLayers() == 1 && image.NumSamples() == 1 &&
           !AmdGpu::IsBlockCoded(image.GetDataFmt()) &&
           SceneSize(image.width + 1, image.height + 1);
}

} // namespace Shader::Stereo
