// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: see vk_sr_output.h. The D3D11 and sharing code follows the Burnout 3 port's
// engine/gpu/src/sr_output.cpp (VK3D's path).
#include "video_core/renderer_vulkan/vk_sr_output.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

#ifndef _WIN32

namespace Vulkan {
std::unique_ptr<SrOutput> CreateSrOutput(const Instance&, Scheduler&) {
    return nullptr;
}
} // namespace Vulkan

#else

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vulkan/vulkan_win32.h>

namespace Vulkan {

namespace {

using Microsoft::WRL::ComPtr;

// ps2sr.dll's interface (the Burnout 3 port's gpu/sr_bridge/ps2sr.cpp).
using OpenFn = int(__cdecl*)(void* hwnd, void* context, int latency, char* error, int error_size);
using WeaveFn = int(__cdecl*)(void* input, int eye_width, int eye_height, int dxgi_format);
using LensFn = void(__cdecl*)(int on);
using CloseFn = void(__cdecl*)();

constexpr u32 External = VK_QUEUE_FAMILY_EXTERNAL;

class SrOutputWin final : public SrOutput {
public:
    SrOutputWin(const Instance& instance_, Scheduler& scheduler_)
        : instance{instance_}, scheduler{scheduler_} {
        const auto device = instance.GetDevice();
        import_semaphore = reinterpret_cast<PFN_vkImportSemaphoreWin32HandleKHR>(
            VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr(device, "vkImportSemaphoreWin32HandleKHR"));
        memory_properties = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
            VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandlePropertiesKHR"));
        // One frame ahead, as SRCapture3D's weaver; BB_SR_LATENCY=n for another.
        if (const char* env = std::getenv("BB_SR_LATENCY")) {
            latency = std::atoi(env);
        }
    }

    ~SrOutputWin() override {
        if (opened && close) {
            close();
        }
        scheduler.Finish();
        if (ctx) {
            ctx->ClearState();
            ctx->Flush();
        }
        DestroyShared();
        if (timeline) {
            instance.GetDevice().destroySemaphore(timeline);
        }
        if (fence_handle) {
            CloseHandle(fence_handle);
        }
        // (ps2sr.dll stays loaded: the SR SDK keeps its context for the process)
    }

    bool Dead() const override {
        return dead;
    }

    const char* Problem() const override {
        return problem;
    }

    bool Ready(void* hwnd, vk::Format format, u32 width, u32 height) override {
        if (dead || !width || !height) {
            return false;
        }
        if (!opened) {
            const ULONGLONG now = GetTickCount64();
            if (last_try && now - last_try < 2000) {
                return false; // the service starting: not every frame
            }
            last_try = now;
            if (!Open(hwnd)) {
                return false;
            }
        }
        if (width != shared_width || height != shared_height || format != shared_format) {
            if (!MakeShared(format, width, height)) {
                Die("the shared textures could not be made");
                return false;
            }
        }
        return true;
    }

    void RecordCopyIn(vk::CommandBuffer cmdbuf, vk::Image image, vk::ImageLayout layout,
                      SubmitInfo& copy_in) override {
        const u32 q = instance.GetGraphicsQueueFamilyIndex();
        // The picture into the shared input, which then belongs to D3D11.
        Barrier(cmdbuf, image, layout, vk::ImageLayout::eTransferSrcOptimal,
                vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eTransferRead,
                vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer);
        Barrier(cmdbuf, in.image, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal,
                {}, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTransfer, External, q);
        cmdbuf.copyImage(image, vk::ImageLayout::eTransferSrcOptimal, in.image,
                         vk::ImageLayout::eTransferDstOptimal, Whole());
        Barrier(cmdbuf, in.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
                vk::AccessFlagBits::eTransferWrite, {}, vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eBottomOfPipe, q, External);
        // After D3D11 and the last copy back are done with the shared textures (the previous
        // frame's b), signalling a.
        value_a = value + 1;
        value_woven = value + 2;
        value_b = value + 3;
        copy_in.AddWait(timeline, value);
        copy_in.AddSignal(timeline, value_a);
    }

    bool Weave() override {
        ctx4->Wait(fence.Get(), value_a);
        ID3D11RenderTargetView* target = rtv.Get();
        ctx->OMSetRenderTargets(1, &target, nullptr);
        const D3D11_VIEWPORT viewport{0, 0, float(shared_width), float(shared_height), 0, 1};
        ctx->RSSetViewports(1, &viewport);
        bool ok = true;
        if (!weave(srv.Get(), int(shared_width / 2), int(shared_height), int(dxgi_format))) {
            ctx->CopyResource(out.tex.Get(), in.tex.Get()); // this picture side by side, then no more
            Die("the weaver failed");
            ok = false;
        } else if (++woven == 1) {
            std::printf("Stereo 3D: SR: the first woven picture (each eye %ux%u)\n", shared_width / 2,
                        shared_height);
        }
        ctx4->Signal(fence.Get(), value_woven);
        ctx->Flush();
        return ok;
    }

    void RecordCopyOut(vk::CommandBuffer cmdbuf, vk::Image image, SubmitInfo& copy_out) override {
        const u32 q = instance.GetGraphicsQueueFamilyIndex();
        Barrier(cmdbuf, out.image, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal, {},
                vk::AccessFlagBits::eTransferRead, vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTransfer, External, q);
        Barrier(cmdbuf, image, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eTransferDstOptimal,
                vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eTransferWrite,
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer);
        cmdbuf.copyImage(out.image, vk::ImageLayout::eTransferSrcOptimal, image,
                         vk::ImageLayout::eTransferDstOptimal, Whole());
        Barrier(cmdbuf, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR,
                vk::AccessFlagBits::eTransferWrite, {}, vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eBottomOfPipe);
        Barrier(cmdbuf, out.image, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral,
                vk::AccessFlagBits::eTransferRead, {}, vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eBottomOfPipe, q, External);
        copy_out.AddWait(timeline, value_woven);
        copy_out.AddSignal(timeline, value_b);
        value = value_b;
    }

    void Lens(bool on) override {
        if (opened && !dead && lens && lens_on != on) {
            lens(on ? 1 : 0);
            lens_on = on;
        }
    }

private:
    struct Shared {
        ComPtr<ID3D11Texture2D> tex;
        HANDLE handle = nullptr;
        vk::Image image;
        vk::DeviceMemory memory;
    };

    vk::ImageCopy Whole() const {
        return vk::ImageCopy{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .extent = {shared_width, shared_height, 1},
        };
    }

    /// An image's layout, and its queue family's ownership when `from_family` and `to_family`
    /// differ (VK_QUEUE_FAMILY_EXTERNAL: D3D11's side).
    static void Barrier(vk::CommandBuffer cmdbuf, vk::Image image, vk::ImageLayout from,
                        vk::ImageLayout to, vk::AccessFlags src_access, vk::AccessFlags dst_access,
                        vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage,
                        u32 from_family = VK_QUEUE_FAMILY_IGNORED,
                        u32 to_family = VK_QUEUE_FAMILY_IGNORED) {
        const vk::ImageMemoryBarrier barrier{
            .srcAccessMask = src_access,
            .dstAccessMask = dst_access,
            .oldLayout = from,
            .newLayout = to,
            .srcQueueFamilyIndex = from_family,
            .dstQueueFamilyIndex = to_family,
            .image = image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        cmdbuf.pipelineBarrier(src_stage, dst_stage, {}, {}, {}, barrier);
    }

    void Die(const char* why) {
        if (!dead) {
            std::printf("Stereo 3D: SR: %s; side by side\n", why);
        }
        dead = true;
        problem = why;
    }

    /// ps2sr.dll beside the executable, D3D11 on the Vulkan device's adapter, the shared fence
    /// as a timeline semaphore, the weaver for `hwnd`.
    bool Open(void* hwnd) {
        if (!instance.IsExternalWin32Supported() || !import_semaphore || !memory_properties) {
            Die("this Vulkan device cannot share memory and semaphores with D3D11");
            return false;
        }
        if (!dll) {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            std::wstring dir(path);
            dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
            dll = LoadLibraryW((dir + L"ps2sr.dll").c_str());
            if (!dll) {
                Die("no ps2sr.dll beside the executable");
                return false;
            }
            open = reinterpret_cast<OpenFn>(GetProcAddress(dll, "PS2SR_Open"));
            weave = reinterpret_cast<WeaveFn>(GetProcAddress(dll, "PS2SR_Weave"));
            lens = reinterpret_cast<LensFn>(GetProcAddress(dll, "PS2SR_Lens"));
            close = reinterpret_cast<CloseFn>(GetProcAddress(dll, "PS2SR_Close"));
            if (!open || !weave || !lens || !close) {
                Die("ps2sr.dll lacks its functions");
                return false;
            }
        }
        if (!dev && !MakeDevice()) {
            return false;
        }
        if (!timeline && !MakeTimeline()) {
            return false;
        }
        char error[256] = {};
        const int result = open(hwnd, ctx.Get(), latency, error, int(sizeof(error)));
        if (result == 0) {
            if (++waits == 1 || waits % 10 == 0) {
                std::printf("Stereo 3D: SR: %s\n", error);
            }
            static char kept[256];
            std::snprintf(kept, sizeof(kept), "%s", error);
            problem = kept;
            if (waits >= 30) {
                Die("the SR service did not come up");
            }
            return false;
        }
        if (result < 0) {
            static char kept[256];
            std::snprintf(kept, sizeof(kept), "%s", error[0] ? error : "PS2SR_Open failed");
            Die(kept);
            return false;
        }
        opened = true;
        problem = nullptr;
        std::printf("Stereo 3D: SR: the weaver is ready (eyes predicted %d frames ahead)\n", latency);
        return true;
    }

    bool MakeDevice() {
        const auto props = instance.GetPhysicalDevice()
                               .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
        const auto& id = props.get<vk::PhysicalDeviceIDProperties>();
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            Die("no DXGI factory");
            return false;
        }
        ComPtr<IDXGIAdapter1> adapter, chosen;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (id.deviceLUIDValid && std::memcmp(&desc.AdapterLuid, id.deviceLUID.data(), sizeof(LUID)) == 0) {
                chosen = adapter;
                break;
            }
            adapter.Reset();
        }
        if (!chosen) {
            Die("no D3D11 adapter is the Vulkan device");
            return false;
        }
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        if (FAILED(D3D11CreateDevice(chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
                                     &dev, nullptr, &ctx)) ||
            FAILED(dev.As(&dev5)) || FAILED(ctx.As(&ctx4))) {
            Die("no D3D11.4 device");
            return false;
        }
        ComPtr<ID3D11Multithread> multithread;
        if (SUCCEEDED(ctx.As(&multithread))) {
            multithread->SetMultithreadProtected(TRUE);
        }
        return true;
    }

    bool MakeTimeline() {
        if (FAILED(dev5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence))) ||
            FAILED(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fence_handle))) {
            Die("no shared D3D11 fence");
            return false;
        }
        const auto device = instance.GetDevice();
        const vk::SemaphoreTypeCreateInfo type{.semaphoreType = vk::SemaphoreType::eTimeline};
        const vk::SemaphoreCreateInfo sci{.pNext = &type};
        if (device.createSemaphore(&sci, nullptr, &timeline) != vk::Result::eSuccess) {
            Die("no timeline semaphore");
            return false;
        }
        VkImportSemaphoreWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
        import.semaphore = timeline;
        import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE_BIT;
        import.handle = fence_handle;
        if (import_semaphore(device, &import) != VK_SUCCESS) {
            device.destroySemaphore(timeline);
            timeline = nullptr;
            Die("the D3D11 fence could not be imported");
            return false;
        }
        value = fence->GetCompletedValue();
        return true;
    }

    void DestroyShared() {
        srv.Reset();
        rtv.Reset();
        const auto device = instance.GetDevice();
        for (Shared* t : {&in, &out}) {
            if (t->image) {
                device.destroyImage(t->image);
            }
            if (t->memory) {
                device.freeMemory(t->memory);
            }
            if (t->handle) {
                CloseHandle(t->handle);
            }
            *t = Shared{};
        }
        shared_width = shared_height = 0;
    }

    bool MakeOne(Shared& t) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = shared_width;
        td.Height = shared_height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = dxgi_format;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        ComPtr<IDXGIResource1> resource;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &t.tex)) || FAILED(t.tex.As(&resource)) ||
            FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                nullptr, &t.handle))) {
            return false;
        }
        const auto device = instance.GetDevice();
        const vk::ExternalMemoryImageCreateInfo external{
            .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eD3D11Texture};
        const vk::ImageCreateInfo ici{
            .pNext = &external,
            .imageType = vk::ImageType::e2D,
            .format = shared_format,
            .extent = {shared_width, shared_height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        if (device.createImage(&ici, nullptr, &t.image) != vk::Result::eSuccess) {
            return false;
        }
        const auto req = device.getImageMemoryRequirements(t.image);
        VkMemoryWin32HandlePropertiesKHR handle_props{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
        u32 bits = req.memoryTypeBits;
        if (memory_properties(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, t.handle,
                              &handle_props) == VK_SUCCESS) {
            bits &= handle_props.memoryTypeBits;
        }
        const auto memory = instance.GetPhysicalDevice().getMemoryProperties();
        u32 type = UINT32_MAX;
        for (u32 i = 0; i < memory.memoryTypeCount && type == UINT32_MAX; ++i) {
            if ((bits & (1u << i)) &&
                (memory.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                type = i;
            }
        }
        for (u32 i = 0; i < memory.memoryTypeCount && type == UINT32_MAX; ++i) {
            if (bits & (1u << i)) {
                type = i;
            }
        }
        if (type == UINT32_MAX) {
            return false;
        }
        // (the Win32 structures are not in vulkan.hpp here: the C ones)
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.image = t.image;
        VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR, &dedicated};
        import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        import.handle = t.handle;
        const vk::MemoryAllocateInfo mai{
            .pNext = &import,
            .allocationSize = req.size,
            .memoryTypeIndex = type,
        };
        return device.allocateMemory(&mai, nullptr, &t.memory) == vk::Result::eSuccess &&
               device.bindImageMemory(t.image, t.memory, 0) == vk::Result::eSuccess;
    }

    /// The input and output at the swapchain's size, both in General and D3D11's (what an
    /// earlier picture still does with the old ones first).
    bool MakeShared(vk::Format format, u32 width, u32 height) {
        if (format == vk::Format::eB8G8R8A8Unorm) {
            dxgi_format = DXGI_FORMAT_B8G8R8A8_UNORM;
        } else if (format == vk::Format::eR8G8B8A8Unorm) {
            dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;
        } else {
            return false;
        }
        scheduler.Finish();
        DestroyShared();
        shared_format = format;
        shared_width = width;
        shared_height = height;
        if (!MakeOne(in) || !MakeOne(out) || FAILED(dev->CreateShaderResourceView(in.tex.Get(), nullptr, &srv)) ||
            FAILED(dev->CreateRenderTargetView(out.tex.Get(), nullptr, &rtv))) {
            DestroyShared();
            return false;
        }
        const auto cmdbuf = scheduler.CommandBuffer();
        for (vk::Image image : {in.image, out.image}) {
            Barrier(cmdbuf, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, {}, {},
                    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
                    instance.GetGraphicsQueueFamilyIndex(), External);
        }
        scheduler.Finish();
        std::printf("Stereo 3D: SR: weaving %ux%u (each eye %ux%u)\n", width, height, width / 2, height);
        return true;
    }

    const Instance& instance;
    Scheduler& scheduler;
    PFN_vkImportSemaphoreWin32HandleKHR import_semaphore = nullptr;
    PFN_vkGetMemoryWin32HandlePropertiesKHR memory_properties = nullptr;
    int latency = 1;
    HMODULE dll = nullptr;
    OpenFn open = nullptr;
    WeaveFn weave = nullptr;
    LensFn lens = nullptr;
    CloseFn close = nullptr;
    bool dead = false, opened = false, lens_on = false;
    const char* problem = "the SR weaver has not started";
    ULONGLONG last_try = 0;
    int waits = 0;
    u64 woven = 0;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11Device5> dev5;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11DeviceContext4> ctx4;
    ComPtr<ID3D11Fence> fence;
    HANDLE fence_handle = nullptr;
    vk::Semaphore timeline;
    u64 value = 0, value_a = 0, value_woven = 0, value_b = 0;
    Shared in, out;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    vk::Format shared_format = vk::Format::eUndefined;
    DXGI_FORMAT dxgi_format = DXGI_FORMAT_B8G8R8A8_UNORM;
    u32 shared_width = 0, shared_height = 0;
};

} // namespace

std::unique_ptr<SrOutput> CreateSrOutput(const Instance& instance, Scheduler& scheduler) {
    return std::make_unique<SrOutputWin>(instance, scheduler);
}

} // namespace Vulkan

#endif // _WIN32
