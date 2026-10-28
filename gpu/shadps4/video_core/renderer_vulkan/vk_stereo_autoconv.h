// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: stereo 3D automatic convergence (BbSettings::stereo_auto_convergence, Ctrl+F4; the
// control loop of the dynamic3d skill). At each frame's display pass a small compute pass reads
// the scene depth (CameraMotion::Depth()) in a grid of 64x36 tiles and keeps each tile's nearest
// view depth, linearized with the game's own depth-to-view constants (CameraMotion::DepthToView,
// the velocity pass's); the grid goes to a host-visible buffer that is read a few frames later,
// once the GPU has signalled the tick it was recorded under (never waited for; four buffers in
// a ring, a frame is skipped when all are pending). The nearest significant depth is a low
// percentile of the tiles (2 %: the nearest thing covering about 2 % of the picture, so stray
// texels and thin edges do not pull the convergence in), filtered in time: the median of the
// last five samples, an EMA that follows an approach faster than a recession, and a snap when
// two samples in a row are much nearer than believed (a camera cut). The convergence moves
// toward that depth times the user's bias in log space, with a dead band of 4 % and a rate
// limit (0.8 per second, twice that nearer), and the next frame is drawn with it
// (BbSettings::stereo_convergence_live, read by Rasterizer::SetStereoPushData).
//   BB_STEREO_AUTO_CONVERGENCE=1  on from the start (the ini key stereo_auto_convergence)
//   BB_STEREO_AUTO_BIAS           the bias (stereo_auto_bias; 1 puts the nearest thing at the screen)
//   BB_STEREO_AUTO_PERCENTILE     the percentile of the tiles (2)
//   BB_STEREO_AUTO_LOG=1          every measurement in the log

#pragma once

#include <array>
#include <chrono>
#include <vector>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace VideoCore {
class TextureCache;
}

namespace Vulkan {

class Instance;
class Scheduler;
class Runtime;
class CameraMotion;

class StereoAutoConvergence {
public:
    StereoAutoConvergence(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
                          VideoCore::TextureCache& texture_cache);
    ~StereoAutoConvergence();

    /// The display pass (this frame's scene depth and camera are complete; the next frame's
    /// draws follow): reads the finished measurements and moves the convergence, records this
    /// frame's measurement, and sets the convergence the next frame is drawn with.
    void OnDisplayPass(const CameraMotion& camera);

private:
    static constexpr u32 GridWidth = 64, GridHeight = 36, Tiles = GridWidth * GridHeight;
    static constexpr u32 Slots = 4;
    struct Slot {
        VkBuffer buffer{};
        VmaAllocation allocation{};
        void* mapped = nullptr;
        u64 tick = 0;
        bool pending = false;
    };
    struct View {
        vk::Image image{};
        u64 uid = 0;
        vk::ImageView view{};
    };

    void CreatePipeline();
    bool CreateSlots();
    vk::ImageView DepthView(const VideoCore::Image& image);
    void Record(VideoCore::Image& depth, std::array<float, 2> depth_to_view, Slot& slot);
    /// A finished measurement: the nearest significant depth of its tiles, if any.
    void Consume(Slot& slot);
    /// The control loop with one nearest-depth sample (game units).
    void Measure(float z_near);
    void Reset(float convergence);

    const Instance& instance;
    Scheduler& scheduler;
    Runtime& runtime;
    VideoCore::TextureCache& texture_cache;
    vk::UniqueDescriptorSetLayout desc_layout;
    vk::UniquePipelineLayout pipeline_layout;
    vk::UniquePipeline pipeline;
    std::array<Slot, Slots> slots{};
    u32 next_slot = 0;
    bool slots_failed = false;
    View view{};
    std::vector<float> tiles; ///< scratch for the percentile

    // The control loop (the display pass's thread only).
    bool active = false;        ///< on at the last measurement (off: starts afresh next time)
    std::array<float, 5> raw{}; ///< the last nearest-depth samples (a ring)
    u32 raw_count = 0, raw_next = 0;
    float z_ema = 0.0f;         ///< the believed nearest depth
    u32 approach_frames = 0;    ///< samples in a row much nearer than believed (a cut)
    float convergence = 0.0f;   ///< applied
    float logged = 0.0f;
    std::chrono::steady_clock::time_point last_step{}, last_log{};
    bool reported = false;
    u32 measurements = 0;
};

} // namespace Vulkan
