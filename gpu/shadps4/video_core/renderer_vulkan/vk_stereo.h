// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: stereo 3D (BbSettings::stereo, BB_STEREO=geo). The game draws both eyes in one pass
// (Vulkan multiview, shader_recompiler/stereo.h) into the two layers of its scene targets; the
// presenter runs its scaling and post passes once per layer of the display buffer, and this
// packs the two pictures side by side, each squeezed to half the width, for the SR panel
// weaver (vk_sr_output.h) or a side-by-side 3D display.
//   BB_STEREO_DEPTH        eye separation, % of the width per eye at infinity (1.0)
//   BB_STEREO_CONVERGENCE  view distance shown at screen depth, game units (3.0)
//   BB_STEREO_SWAP=1       left and right exchanged (the sign of the separation in the shaders)

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan {

class Instance;
class Scheduler;

class Stereo {
public:
    Stereo(const Instance& instance, Scheduler& scheduler);
    ~Stereo();

    /// Switched on in the settings.
    [[nodiscard]] static bool Wanted();

    /// `frame` (FrameGenerator::ColorFormat, General, `width` x `height`) holds the left eye,
    /// `right` / `right_view` (General, the same size) the right one, or null for a flat frame
    /// (both halves the same); the frame is rewritten as the pair side by side, left in General
    /// layout. The eye swap is already in the drawing (the sign of the separation), not here.
    void ComposePair(vk::CommandBuffer cmdbuf, vk::Image frame, vk::ImageView frame_view,
                     vk::Image right, vk::ImageView right_view, u32 width, u32 height);

private:
    void CreatePackPipeline();
    void EnsureOutput(u32 width, u32 height);

    const Instance& instance;
    Scheduler& scheduler;
    vk::UniqueSampler sampler;
    vk::UniqueDescriptorSetLayout pack_desc_layout;
    vk::UniquePipelineLayout pack_pipeline_layout;
    vk::UniquePipeline pack_pipeline;
    VideoCore::UniqueImage output; ///< the pair (ColorFormat), storage and transfer source
    vk::UniqueImageView output_view;
    u32 output_width = 0, output_height = 0;
};

} // namespace Vulkan
