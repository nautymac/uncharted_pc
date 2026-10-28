// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_stereo_autoconv.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <vk_mem_alloc.h>

#include "bbport_settings.h"
#include "video_core/host_shaders/stereo_depth_probe_comp.h"
#include "video_core/renderer_vulkan/vk_camera_motion.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/texture_cache.h"

namespace Vulkan {

namespace {

struct PushConstants {
    std::array<s32, 2> origin;
    std::array<s32, 2> size;
    std::array<s32, 2> grid;
    float depth_a, depth_b;
    float z_min, z_max;
};
static_assert(sizeof(PushConstants) == 40);

// The percentile of the tiles' nearest depths taken as the nearest significant content
// (dynamic3d: a low percentile, never the minimum). BB_STEREO_AUTO_PERCENTILE overrides it (%).
float Percentile() {
    static const float value = [] {
        const char* env = std::getenv("BB_STEREO_AUTO_PERCENTILE");
        return std::clamp(env ? float(std::atof(env)) : 2.0f, 0.0f, 50.0f);
    }();
    return value;
}

bool Verbose() {
    static const bool on = [] {
        const char* env = std::getenv("BB_STEREO_AUTO_LOG");
        return env && env[0] == '1';
    }();
    return on;
}

float Median(const float* values, u32 count) {
    std::array<float, 8> sorted{};
    count = std::min<u32>(count, u32(sorted.size()));
    std::copy_n(values, count, sorted.begin());
    std::sort(sorted.begin(), sorted.begin() + count);
    return count ? sorted[(count - 1) / 2] : 0.0f;
}

constexpr float MinConvergence = 0.1f, MaxConvergence = 1000.0f;

} // namespace

StereoAutoConvergence::StereoAutoConvergence(const Instance& instance_, Scheduler& scheduler_,
                                             Runtime& runtime_,
                                             VideoCore::TextureCache& texture_cache_)
    : instance{instance_}, scheduler{scheduler_}, runtime{runtime_},
      texture_cache{texture_cache_} {}

StereoAutoConvergence::~StereoAutoConvergence() {
    if (view.view) {
        instance.GetDevice().destroyImageView(view.view);
    }
    for (auto& slot : slots) {
        if (slot.buffer) {
            vmaDestroyBuffer(instance.GetAllocator(), slot.buffer, slot.allocation);
        }
    }
}

void StereoAutoConvergence::CreatePipeline() {
    if (pipeline) {
        return;
    }
    const auto device = instance.GetDevice();
    const std::array<vk::DescriptorSetLayoutBinding, 2> bindings{{
        {0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
    }};
    desc_layout = Check(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = u32(bindings.size()),
        .pBindings = bindings.data(),
    }));
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstants)};
    pipeline_layout = Check(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &*desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    }));
    const auto module = CompileSPV(STEREO_DEPTH_PROBE_COMP, device);
    pipeline = Check(device.createComputePipelineUnique(
        {}, vk::ComputePipelineCreateInfo{
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                          .module = module,
                          .pName = "main"},
                .layout = *pipeline_layout,
            }));
    device.destroyShaderModule(module);
}

bool StereoAutoConvergence::CreateSlots() {
    if (slots[0].buffer) {
        return true;
    }
    if (slots_failed) {
        return false;
    }
    for (auto& slot : slots) {
        const VkBufferCreateInfo buffer_ci{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = Tiles * sizeof(float),
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        };
        const VmaAllocationCreateInfo alloc_ci{
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        };
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(instance.GetAllocator(), &buffer_ci, &alloc_ci, &slot.buffer,
                            &slot.allocation, &info) != VK_SUCCESS) {
            slots_failed = true;
            std::printf("Stereo 3D: auto convergence: no host buffer for the depth grid; off\n");
            return false;
        }
        slot.mapped = info.pMappedData;
    }
    return true;
}

vk::ImageView StereoAutoConvergence::DepthView(const VideoCore::Image& image) {
    const vk::Image handle = image.GetImage();
    if (view.view && view.image == handle && view.uid == image.image_uid) {
        return view.view;
    }
    const auto device = instance.GetDevice();
    if (view.view) {
        scheduler.DeferOperation([device, old = view.view] { device.destroyImageView(old); });
    }
    view = {
        .image = handle,
        .uid = image.image_uid,
        .view = Check(device.createImageView({
            .image = handle,
            .viewType = vk::ImageViewType::e2D,
            .format = image.info.pixel_format,
            .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
        })),
    };
    return view.view;
}

void StereoAutoConvergence::OnDisplayPass(const CameraMotion& camera) {
    auto& s = BbSettings::Get();
    const bool on = s.stereo_auto_convergence && s.stereo != BbSettings::StereoOff;
    // The finished measurements, oldest first.
    for (u32 i = 0; i < Slots; ++i) {
        Slot& slot = slots[(next_slot + i) % Slots];
        if (slot.pending && scheduler.IsFree(slot.tick)) {
            Consume(slot);
        }
    }
    if (on && camera.Depth()) {
        const auto mapping = camera.DepthToView();
        Slot& slot = slots[next_slot];
        if (mapping[1] != 0.0f && !slot.pending) {
            Record(texture_cache.GetImage(camera.Depth()), mapping, slot);
            if (slot.pending) {
                next_slot = (next_slot + 1) % Slots;
            }
        }
    }
    if (!on) {
        active = false;
    }
    s.stereo_convergence_live = on && active ? convergence : s.stereo_convergence.load();
}

void StereoAutoConvergence::Record(VideoCore::Image& depth, std::array<float, 2> depth_to_view,
                                   Slot& slot) {
    const auto& info = depth.info;
    if (!info.props.is_depth || info.num_samples > 1 || depth.backing->num_samples != 1 ||
        !info.size.width || !info.size.height) {
        return;
    }
    CreatePipeline();
    if (!CreateSlots()) {
        return;
    }
    const vk::ImageView depth_view = DepthView(depth);
    scheduler.EndRendering();
    runtime.Transit(&depth, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderRead);
    runtime.FlushBarriers();
    // The near and far planes: the view depths of device depth 0 and 1, whichever is which
    // (a reversed projection has the near plane at 1; an infinite far plane gives no finite
    // depth). A loading screen draws its picture at the near plane, a cleared buffer or the
    // sky sits at the far plane: neither is content, so both are left out.
    float near_z = 1e30f, far_z = 0.0f;
    for (const float d : {0.0f, 1.0f}) {
        const float z = depth_to_view[1] / (d - depth_to_view[0]);
        if (std::isfinite(z) && z > 0.0f) {
            near_z = std::min(near_z, z);
            far_z = std::max(far_z, z);
        }
    }
    const PushConstants push{
        .origin = {0, 0},
        .size = {s32(info.size.width), s32(info.size.height)},
        .grid = {s32(GridWidth), s32(GridHeight)},
        .depth_a = depth_to_view[0],
        .depth_b = depth_to_view[1],
        .z_min = near_z < 1e29f ? near_z * 1.001f : 0.0f,
        .z_max = far_z > near_z ? far_z * 0.999f : 1e30f,
    };
    scheduler.Record([pipeline = *pipeline, layout = *pipeline_layout, depth_view,
                      buffer = vk::Buffer(slot.buffer), push](vk::CommandBuffer cmdbuf) {
        const vk::DescriptorImageInfo depth_info{.imageView = depth_view,
                                                 .imageLayout = vk::ImageLayout::eGeneral};
        const vk::DescriptorBufferInfo tiles_info{
            .buffer = buffer, .offset = 0, .range = Tiles * sizeof(float)};
        const std::array<vk::WriteDescriptorSet, 2> writes{{
            {.dstBinding = 0, .descriptorCount = 1,
             .descriptorType = vk::DescriptorType::eSampledImage, .pImageInfo = &depth_info},
            {.dstBinding = 1, .descriptorCount = 1,
             .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &tiles_info},
        }};
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
        cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, layout, 0, writes);
        cmdbuf.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
        cmdbuf.dispatch(GridWidth, GridHeight, 1);
        const vk::MemoryBarrier2 to_host{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eHost,
            .dstAccessMask = vk::AccessFlagBits2::eHostRead,
        };
        cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &to_host});
    });
    slot.tick = scheduler.CurrentTick();
    slot.pending = true;
    if (!reported) {
        reported = true;
        std::printf("Stereo 3D: auto convergence from the scene depth %ux%u, z = %g / (depth - %g), "
                    "content between %g and %g\n",
                    info.size.width, info.size.height, depth_to_view[1], depth_to_view[0],
                    push.z_min, push.z_max);
    }
}

void StereoAutoConvergence::Consume(Slot& slot) {
    slot.pending = false;
    vmaInvalidateAllocation(instance.GetAllocator(), slot.allocation, 0, VK_WHOLE_SIZE);
    tiles.resize(Tiles);
    std::memcpy(tiles.data(), slot.mapped, Tiles * sizeof(float));
    // The tiles with a depth, nearest first up to the percentile of all tiles: the nearest
    // thing covering that much of the picture. Fewer valid tiles than that (a loading screen,
    // a menu, nearly all sky): no measurement, the convergence holds.
    const auto valid_end =
        std::partition(tiles.begin(), tiles.end(), [](float z) { return z < 1e29f; });
    const u32 valid = u32(valid_end - tiles.begin());
    const u32 index = u32(float(Tiles) * Percentile() / 100.0f);
    if (index >= valid) {
        return;
    }
    std::nth_element(tiles.begin(), tiles.begin() + index, valid_end);
    Measure(tiles[index]);
}

void StereoAutoConvergence::Reset(float value) {
    raw_count = raw_next = 0;
    z_ema = 0.0f;
    approach_frames = 0;
    convergence = std::clamp(value, MinConvergence, MaxConvergence);
    last_step = std::chrono::steady_clock::now();
}

void StereoAutoConvergence::Measure(float z_near) {
    auto& s = BbSettings::Get();
    if (!(s.stereo_auto_convergence && s.stereo != BbSettings::StereoOff)) {
        active = false;
        return;
    }
    if (!std::isfinite(z_near) || z_near <= 0.0f) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!active) {
        Reset(s.stereo_convergence);
        active = true;
    }
    raw[raw_next] = z_near;
    raw_next = (raw_next + 1) % u32(raw.size());
    raw_count = std::min<u32>(raw_count + 1, u32(raw.size()));
    const float z_median = Median(raw.data(), raw_count);
    bool snap = false;
    if (z_ema <= 0.0f) {
        z_ema = z_median;
        snap = true;
    } else {
        // A camera cut to something much nearer: two raw samples in a row (dynamic3d: a ratio,
        // approach only; a recession eases, as there is nothing to protect against).
        approach_frames = z_near < z_ema && z_ema / z_near > 2.5f ? approach_frames + 1 : 0;
        if (approach_frames >= 2) {
            std::array<float, 3> last{};
            const u32 count = std::min<u32>(3, raw_count);
            for (u32 k = 0; k < count; ++k) {
                last[k] = raw[(raw_next + u32(raw.size()) - 1 - k) % u32(raw.size())];
            }
            z_ema = Median(last.data(), count);
            approach_frames = 0;
            snap = true;
        } else if (std::fabs(z_median - z_ema) / z_ema > 0.005f) {
            z_ema += (z_median - z_ema) * (z_median < z_ema ? 0.20f : 0.05f);
        }
    }
    const float target =
        std::clamp(z_ema * s.stereo_auto_bias.load(), MinConvergence, MaxConvergence);
    const float dt = std::clamp(std::chrono::duration<float>(now - last_step).count(), 0.0f, 0.1f);
    last_step = now;
    const float d = std::log(target / convergence);
    if (snap) {
        convergence = target;
    } else if (std::fabs(d) >= 0.04f) { // the dead band: no pumping around the target
        const float gain = std::fabs(d) > 0.3f ? 0.45f : 0.2f;
        const float limit = (d < 0.0f ? 1.6f : 0.8f) * dt; // nearer twice as fast
        convergence *= std::exp(std::clamp(d * gain, -limit, limit));
    }
    convergence = std::clamp(convergence, MinConvergence, MaxConvergence);
    ++measurements;
    if (Verbose()) {
        std::printf("Stereo 3D: auto convergence: nearest %.3f (median %.3f, believed %.3f) -> "
                    "%.3f%s\n", z_near, z_median, z_ema, convergence, snap ? " (snap)" : "");
    } else if (logged <= 0.0f || (std::fabs(std::log(convergence / logged)) > 0.1f &&
                                  now - last_log > std::chrono::seconds(1))) {
        std::printf("Stereo 3D: auto convergence %.2f (nearest %.2f, bias %.2f, %u measurements)\n",
                    convergence, z_ema, s.stereo_auto_bias.load(), measurements);
        logged = convergence;
        last_log = now;
    }
}

} // namespace Vulkan
