// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the stereo picture woven for an SR panel (Simulated Reality: Acer SpatialLabs, Leia
// SR) by the SR SDK's DX11 weaver, through ps2sr.dll beside the executable (the SDK is
// proprietary; the DLL comes from the Burnout 3 port's gpu/sr_bridge and is shared with it).
// The SDK has no Vulkan weaver: the presented image (the two eyes side by side) is copied into
// a D3D11 texture shared with Vulkan, woven into a second one and copied back before the
// present, in step on the GPU through a shared D3D11 fence imported as a Vulkan timeline
// semaphore. Without the DLL, the SR runtime or its service, the picture stays side by side.
//   BB_SR_LATENCY=n   the weaver predicts the eyes n frames ahead (1)

#pragma once

#include <memory>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;
class Scheduler;
struct SubmitInfo;

class SrOutput {
public:
    virtual ~SrOutput() = default;

    /// Ready to weave a `width` x `height` swapchain image of `format` shown in `hwnd`: the DLL
    /// and the SR runtime found, the service up (asked again every two seconds while it starts),
    /// the shared textures made at that size. False: present the picture as it is; `Problem()`
    /// says why.
    virtual bool Ready(void* hwnd, vk::Format format, u32 width, u32 height) = 0;

    /// The presenter's commands in `cmdbuf` (still open) drew the picture into `image`, left in
    /// `layout`: records the copy into the shared input. Then `Flush(copy_in)` is submitted by
    /// the caller with the waits it adds here (the previous frame's weave done).
    virtual void RecordCopyIn(vk::CommandBuffer cmdbuf, vk::Image image, vk::ImageLayout layout,
                              SubmitInfo& copy_in) = 0;
    /// After the copy-in submission: D3D11 weaves the input into the output (waiting for the copy
    /// on the GPU). False when the weaver failed: the picture is shown as it is from now on.
    virtual bool Weave() = 0;
    /// Records the woven output back into `image` (left in PresentSrcKHR) into a fresh `cmdbuf`,
    /// with the waits and signals `copy_out`'s submission needs (the weave done).
    virtual void RecordCopyOut(vk::CommandBuffer cmdbuf, vk::Image image, SubmitInfo& copy_out) = 0;

    /// The panel's lenses: on for 3D, off for 2D.
    virtual void Lens(bool on) = 0;
    /// No SR here, or it failed for good: side by side from now on.
    virtual bool Dead() const = 0;
    /// Why it is not weaving (while starting, or dead), or null.
    virtual const char* Problem() const = 0;
};

/// Null outside Windows.
std::unique_ptr<SrOutput> CreateSrOutput(const Instance& instance, Scheduler& scheduler);

} // namespace Vulkan
