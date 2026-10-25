// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_stereo.h"

#include <array>
#include <cstdio>

#include "bbport_settings.h"
#include "video_core/host_shaders/stereo_pack_comp.h"
#include "video_core/renderer_vulkan/vk_frame_generation.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

Stereo::Stereo(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_} {}

Stereo::~Stereo() = default;

bool Stereo::Wanted() {
    return BbSettings::Get().stereo != BbSettings::StereoOff;
}

void Stereo::CreatePackPipeline() {
    if (pack_pipeline) {
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

void Stereo::ComposePair(vk::CommandBuffer cmdbuf, vk::Image frame, vk::ImageView frame_view,
                         vk::Image right, vk::ImageView right_view, u32 width, u32 height) {
    if (width < 2 || !height) {
        return;
    }
    CreatePackPipeline();
    EnsureOutput(width, height);
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
    const bool pair = right && right_view;
    // The post passes wrote the eyes; the output's last contents (copied out already) are not
    // needed.
    const std::array<vk::ImageMemoryBarrier2, 3> before{
        barrier(vk::Image(output), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eTransferRead,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite),
        barrier(frame, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
                vk::PipelineStageFlagBits2::eAllCommands, written,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead),
        barrier(right, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
                vk::PipelineStageFlagBits2::eAllCommands, written,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead),
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = pair ? 3u : 2u,
                             .pImageMemoryBarriers = before.data()});
    const vk::DescriptorImageInfo left_info{.sampler = *sampler,
                                            .imageView = frame_view,
                                            .imageLayout = vk::ImageLayout::eGeneral};
    const vk::DescriptorImageInfo right_info{.sampler = *sampler,
                                             .imageView = pair ? right_view : frame_view,
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
    const u32 constants[3] = {width, height, 0u};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *pack_pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *pack_pipeline_layout, 0, writes);
    cmdbuf.pushConstants(*pack_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                         sizeof(constants), constants);
    cmdbuf.dispatch((width + 7) / 8, (height + 7) / 8, 1);

    // The pair replaces the frame.
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

    static bool reported_pair = false, reported_flat = false;
    if (pair && !reported_pair) {
        reported_pair = true;
        std::printf("Stereo 3D: one pass (multiview), the two layers of the display buffer side "
                    "by side, %ux%u per eye\n", width, height);
    } else if (!pair && !reported_flat) {
        reported_flat = true;
        std::printf("Stereo 3D: a display buffer with one layer, shown flat\n");
    }
}

} // namespace Vulkan
