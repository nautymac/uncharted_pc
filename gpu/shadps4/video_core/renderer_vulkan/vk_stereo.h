// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: stereo 3D (BbSettings::stereo, BB_STEREO=sbs). The finished frame becomes two eyes
// side by side, each squeezed to half the width, for SBS 3D displays and weavers (SRCapture3D
// --mode sbs, ReShade Game Bridge). The eyes are made from the game's own scene depth and the
// camera of its velocity pass, the same captures frame generation takes (FrameGenerator::Scene);
// the UI stays at screen depth. Frames without the scene (menus, movies) are flat in both halves.
//   BB_STEREO_DEPTH        separation of the far distance, % of the width per eye (1.0)
//   BB_STEREO_CONVERGENCE  view distance shown at screen depth, game units (3.0)
//   BB_STEREO_POPOUT       largest shift of near surfaces out of the screen, % (1.0)
//   BB_STEREO_SWAP=1       left and right halves exchanged (cross-eyed viewing)

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_frame_generation.h"
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

    /// Rewrites `frame` (FrameGenerator::ColorFormat, General, `width` x `height`, the finished
    /// frame with its UI) as the two eyes side by side, left in General layout. `hudless`: the
    /// same frame composed without UI (General), or null; `scene`: this frame's depth and
    /// camera, empty for a flat frame.
    void Compose(vk::CommandBuffer cmdbuf, vk::Image frame, vk::ImageView frame_view, u32 width,
                 u32 height, vk::Image hudless, vk::ImageView hudless_view,
                 const FrameGenerator::StereoScene& scene);

    /// Stereo on alternate frames (BbSettings::StereoAlt): `frame` (General) is the game's
    /// frame rendered for `eye` (1 left, 2 right, from the camera hook; 0: no eye, a flat frame).
    /// It is kept as that eye's last picture, and the frame is rewritten as the last left and
    /// right pictures side by side (a flat frame on both sides).
    void ComposeAlternate(vk::CommandBuffer cmdbuf, vk::Image frame, vk::ImageView frame_view,
                          u32 width, u32 height, int eye);

    /// Stereo in one pass (BbSettings::StereoGeo, shader_recompiler/stereo.h): `frame`
    /// (General) holds the left eye and `right` / `right_view` (General, the same size) the
    /// right one, or null for a flat frame; the frame is rewritten as the pair side by side.
    /// The eye swap is already in the drawing (the sign of the separation), not applied here.
    void ComposePair(vk::CommandBuffer cmdbuf, vk::Image frame, vk::ImageView frame_view,
                     vk::Image right, vk::ImageView right_view, u32 width, u32 height);

private:
    void CreatePipeline();
    void CreatePackPipeline();
    void EnsureOutput(u32 width, u32 height);
    void EnsureEyes(u32 width, u32 height);

    vk::UniqueDescriptorSetLayout pack_desc_layout;
    vk::UniquePipelineLayout pack_pipeline_layout;
    vk::UniquePipeline pack_pipeline;
    /// The last picture of each eye (alternate frames), ColorFormat, kept in General layout.
    VideoCore::UniqueImage eyes[2];
    vk::UniqueImageView eye_views[2];
    bool eye_valid[2]{};
    bool pair_valid = false; ///< `output` holds the last pair (both eyes), shown again on left frames
    u32 eye_width = 0, eye_height = 0;

    const Instance& instance;
    Scheduler& scheduler;
    vk::UniqueSampler sampler;
    vk::UniqueDescriptorSetLayout desc_layout;
    vk::UniquePipelineLayout pipeline_layout;
    vk::UniquePipeline pipeline;
    VideoCore::UniqueImage output;
    vk::UniqueImageView output_view;
    u32 output_width = 0, output_height = 0;
};

} // namespace Vulkan
