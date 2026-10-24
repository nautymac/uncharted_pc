// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_stereo.h"

#include <array>
#include <cstdio>

#include "bbport_settings.h"
#include "video_core/host_shaders/stereo_pack_comp.h"
#include "video_core/host_shaders/stereo_sbs_comp.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

namespace {

/// stereo_sbs.comp's push constants.
struct Constants {
    s32 out_size[2];
    s32 depth_size[2];
    float depth_to_z[2];
    float separation;
    float convergence;
    float popout;
    u32 flags;
};
static_assert(sizeof(Constants) == 40);

constexpr u32 FlagScene = 1, FlagHudless = 2, FlagSwap = 4;

} // namespace

Stereo::Stereo(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_} {}

Stereo::~Stereo() = default;

bool Stereo::Wanted() {
    return BbSettings::Get().stereo != BbSettings::StereoOff;
}

void Stereo::CreatePipeline() {
    if (pipeline) {
        return;
    }
    const auto device = instance.GetDevice();
    sampler = Check(device.createSamplerUnique({
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
    }));
    const std::array<vk::DescriptorSetLayoutBinding, 4> bindings{{
        {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        {2, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
        {3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
    }};
    desc_layout = Check(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = u32(bindings.size()),
        .pBindings = bindings.data(),
    }));
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute, 0, sizeof(Constants)};
    pipeline_layout = Check(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &*desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    }));
    const auto module = CompileSPV(STEREO_SBS_COMP, device);
    pipeline = Check(device.createComputePipelineUnique(
        {}, vk::ComputePipelineCreateInfo{
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                          .module = module,
                          .pName = "main"},
                .layout = *pipeline_layout,
            }));
    device.destroyShaderModule(module);
}

void Stereo::EnsureOutput(u32 width, u32 height) {
    if (output && output_width == width && output_height == height) {
        return;
    }
    // A previous frame's copy may still read the old image.
    scheduler.WaitSubmitted();
    const auto device = instance.GetDevice();
    output_view.reset();
    output = VideoCore::UniqueImage(device, instance.GetAllocator());
    output.Create(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = FrameGenerator::ColorFormat,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    output_view = Check(device.createImageViewUnique({
        .image = vk::Image(output),
        .viewType = vk::ImageViewType::e2D,
        .format = FrameGenerator::ColorFormat,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    }));
    output_width = width;
    output_height = height;
    std::printf("Stereo 3D: side by side, %ux%u (two eyes of %ux%u)\n", width, height, width / 2,
                height);
}

void Stereo::Compose(vk::CommandBuffer cmdbuf, vk::Image frame, vk::ImageView frame_view,
                     u32 width, u32 height, vk::Image hudless, vk::ImageView hudless_view,
                     const FrameGenerator::StereoScene& scene) {
    if (width < 2 || !height) {
        return;
    }
    CreatePipeline();
    EnsureOutput(width, height);
    const auto& settings = BbSettings::Get();
    const bool with_hudless = hudless && hudless_view;
    const auto barrier = [](vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
                            vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
                            vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access) {
        return vk::ImageMemoryBarrier2{
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = from,
            .newLayout = to,
            .image = image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
    };
    // The post pass wrote the frame and the picture without UI; the output's last contents
    // (copied out already) are not needed.
    const vk::AccessFlags2 written = vk::AccessFlagBits2::eColorAttachmentWrite |
                                     vk::AccessFlagBits2::eShaderWrite |
                                     vk::AccessFlagBits2::eTransferWrite;
    std::array<vk::ImageMemoryBarrier2, 3> before{
        barrier(vk::Image(output), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eTransferRead,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite),
        barrier(frame, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
                vk::PipelineStageFlagBits2::eAllCommands, written,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead),
        barrier(hudless, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
                vk::PipelineStageFlagBits2::eAllCommands, written,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead),
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = with_hudless ? 3u : 2u,
                             .pImageMemoryBarriers = before.data()});

    const vk::DescriptorImageInfo color_info{.sampler = *sampler,
                                             .imageView = frame_view,
                                             .imageLayout = vk::ImageLayout::eGeneral};
    const vk::DescriptorImageInfo hudless_info{.sampler = *sampler,
                                               .imageView = with_hudless ? hudless_view : frame_view,
                                               .imageLayout = vk::ImageLayout::eGeneral};
    // Without the scene the depth is not read; any sampled view of the right type will do.
    const vk::DescriptorImageInfo depth_info{.imageView = scene.depth ? scene.depth : frame_view,
                                             .imageLayout = vk::ImageLayout::eGeneral};
    const vk::DescriptorImageInfo output_info{.imageView = *output_view,
                                              .imageLayout = vk::ImageLayout::eGeneral};
    const std::array<vk::WriteDescriptorSet, 4> writes{{
        {.dstBinding = 0, .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &color_info},
        {.dstBinding = 1, .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &hudless_info},
        {.dstBinding = 2, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage,
         .pImageInfo = &depth_info},
        {.dstBinding = 3, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage,
         .pImageInfo = &output_info},
    }};
    const Constants constants{
        .out_size = {s32(width), s32(height)},
        .depth_size = {s32(scene.width), s32(scene.height)},
        .depth_to_z = {scene.depth_to_z[0], scene.depth_to_z[1]},
        .separation = settings.stereo_depth.load() / 100.0f,
        .convergence = settings.stereo_convergence.load(),
        .popout = settings.stereo_popout.load() / 100.0f,
        .flags = (scene.depth ? FlagScene : 0u) | (with_hudless ? FlagHudless : 0u) |
                 (settings.stereo_swap ? FlagSwap : 0u),
    };
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *pipeline_layout, 0, writes);
    cmdbuf.pushConstants(*pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants),
                         &constants);
    cmdbuf.dispatch((width + 7) / 8, (height + 7) / 8, 1);

    // The eyes replace the frame.
    const std::array<vk::ImageMemoryBarrier2, 2> to_copy{
        barrier(vk::Image(output), vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead),
        barrier(frame, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite),
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = u32(to_copy.size()),
                             .pImageMemoryBarriers = to_copy.data()});
    cmdbuf.copyImage(vk::Image(output), vk::ImageLayout::eTransferSrcOptimal, frame,
                     vk::ImageLayout::eTransferDstOptimal,
                     vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                   .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                   .extent = {width, height, 1}});
    const auto done = barrier(frame, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
                              vk::PipelineStageFlagBits2::eTransfer,
                              vk::AccessFlagBits2::eTransferWrite,
                              vk::PipelineStageFlagBits2::eAllCommands,
                              vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead);
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &done});

    static bool reported_scene = false, reported_flat = false;
    if (scene.depth && !reported_scene) {
        reported_scene = true;
        std::printf("Stereo 3D: eyes from the scene depth %ux%u (z = %g / (depth - %g)), %s UI\n",
                    scene.width, scene.height, scene.depth_to_z[1], scene.depth_to_z[0],
                    with_hudless ? "separate" : "no separate");
    } else if (!scene.depth && !reported_flat) {
        reported_flat = true;
        std::printf("Stereo 3D: a frame without the scene, shown flat\n");
    }
}

void Stereo::CreatePackPipeline() {
    if (pack_pipeline) {
        return;
    }
    CreatePipeline(); // the sampler
    const auto device = instance.GetDevice();
    const std::array<vk::DescriptorSetLayoutBinding, 3> bindings{{
        {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
    }};
    pack_desc_layout = Check(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = u32(bindings.size()),
        .pBindings = bindings.data(),
    }));
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute, 0, 3 * sizeof(u32)};
    pack_pipeline_layout = Check(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &*pack_desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    }));
    const auto module = CompileSPV(STEREO_PACK_COMP, device);
    pack_pipeline = Check(device.createComputePipelineUnique(
        {}, vk::ComputePipelineCreateInfo{
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                          .module = module,
                          .pName = "main"},
                .layout = *pack_pipeline_layout,
            }));
    device.destroyShaderModule(module);
}

void Stereo::EnsureEyes(u32 width, u32 height) {
    if (eyes[0] && eye_width == width && eye_height == height) {
        return;
    }
    scheduler.WaitSubmitted();
    const auto device = instance.GetDevice();
    for (int e = 0; e < 2; ++e) {
        eye_views[e].reset();
        eyes[e] = VideoCore::UniqueImage(device, instance.GetAllocator());
        eyes[e].Create(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = FrameGenerator::ColorFormat,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
        eye_views[e] = Check(device.createImageViewUnique({
            .image = vk::Image(eyes[e]),
            .viewType = vk::ImageViewType::e2D,
            .format = FrameGenerator::ColorFormat,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        }));
        eye_valid[e] = false;
    }
    pair_valid = false;
    eye_width = width;
    eye_height = height;
    std::printf("Stereo 3D: alternate frames, %ux%u per eye, shown side by side\n", width, height);
}

void Stereo::ComposeAlternate(vk::CommandBuffer cmdbuf, vk::Image frame, vk::ImageView frame_view,
                              u32 width, u32 height, int eye) {
    if (width < 2 || !height) {
        return;
    }
    CreatePackPipeline();
    EnsureOutput(width, height);
    EnsureEyes(width, height);
    const auto barrier = [](vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
                            vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
                            vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access) {
        return vk::ImageMemoryBarrier2{
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = from,
            .newLayout = to,
            .image = image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
    };
    const vk::AccessFlags2 written = vk::AccessFlagBits2::eColorAttachmentWrite |
                                     vk::AccessFlagBits2::eShaderWrite |
                                     vk::AccessFlagBits2::eTransferWrite;
    // A frame with an eye is copied into that eye's picture. Without one (menus, movies, or no
    // record from the hook), both pictures are dropped: the frame is shown flat on both sides.
    static bool reported_flat = false;
    if (eye == 1 || eye == 2) {
        const int e = eye - 1;
        const std::array<vk::ImageMemoryBarrier2, 2> to_copy{
            barrier(frame, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
                    vk::PipelineStageFlagBits2::eAllCommands, written,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead),
            barrier(vk::Image(eyes[e]), vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                    vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite),
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = u32(to_copy.size()),
                                 .pImageMemoryBarriers = to_copy.data()});
        cmdbuf.copyImage(frame, vk::ImageLayout::eTransferSrcOptimal, vk::Image(eyes[e]),
                         vk::ImageLayout::eTransferDstOptimal,
                         vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                       .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                       .extent = {width, height, 1}});
        const std::array<vk::ImageMemoryBarrier2, 2> copied{
            barrier(frame, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead,
                    vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead),
            barrier(vk::Image(eyes[e]), vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead),
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = u32(copied.size()),
                                 .pImageMemoryBarriers = copied.data()});
        eye_valid[e] = true;
        reported_flat = false;
    } else {
        eye_valid[0] = eye_valid[1] = false;
        pair_valid = false;
        if (!reported_flat) {
            reported_flat = true;
            std::printf("Stereo 3D: a frame without an eye, shown flat\n");
        }
    }
    // The pair is made when the right eye arrives, so that it is always the newer picture of the
    // two (one game frame after the left). A left-eye frame shows the previous pair again:
    // packing the newest left with the older right made the eyes' time order alternate every
    // frame, and with it the disparity of anything moving (a visible wobble). A flat frame
    // (no eye) is shown on both sides at once.
    const bool both = eye_valid[0] && eye_valid[1];
    const bool repack = eye != 1 || !pair_valid;
    if (repack) {
        const auto before = barrier(vk::Image(output), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                                    vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eTransferRead,
                                    vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite);
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &before});
        const vk::DescriptorImageInfo left_info{.sampler = *sampler,
                                                .imageView = both ? *eye_views[0] : frame_view,
                                                .imageLayout = vk::ImageLayout::eGeneral};
        const vk::DescriptorImageInfo right_info{.sampler = *sampler,
                                                 .imageView = both ? *eye_views[1] : frame_view,
                                                 .imageLayout = vk::ImageLayout::eGeneral};
        const vk::DescriptorImageInfo output_info{.imageView = *output_view,
                                                  .imageLayout = vk::ImageLayout::eGeneral};
        const std::array<vk::WriteDescriptorSet, 3> writes{{
            {.dstBinding = 0, .descriptorCount = 1,
             .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &left_info},
            {.dstBinding = 1, .descriptorCount = 1,
             .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &right_info},
            {.dstBinding = 2, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage,
             .pImageInfo = &output_info},
        }};
        const u32 constants[3] = {width, height, BbSettings::Get().stereo_swap ? 1u : 0u};
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *pack_pipeline);
        cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *pack_pipeline_layout, 0, writes);
        cmdbuf.pushConstants(*pack_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                             sizeof(constants), constants);
        cmdbuf.dispatch((width + 7) / 8, (height + 7) / 8, 1);
        pair_valid = both;
    }

    const std::array<vk::ImageMemoryBarrier2, 2> to_copy{
        repack ? barrier(vk::Image(output), vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
                         vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                         vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead)
               : barrier(vk::Image(output), vk::ImageLayout::eTransferSrcOptimal,
                         vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eTransfer,
                         vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eTransfer,
                         vk::AccessFlagBits2::eTransferRead),
        barrier(frame, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal,
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead,
                vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite),
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = u32(to_copy.size()),
                             .pImageMemoryBarriers = to_copy.data()});
    cmdbuf.copyImage(vk::Image(output), vk::ImageLayout::eTransferSrcOptimal, frame,
                     vk::ImageLayout::eTransferDstOptimal,
                     vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                   .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                   .extent = {width, height, 1}});
    const auto done = barrier(frame, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
                              vk::PipelineStageFlagBits2::eTransfer,
                              vk::AccessFlagBits2::eTransferWrite,
                              vk::PipelineStageFlagBits2::eAllCommands,
                              vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead);
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &done});
}

} // namespace Vulkan
