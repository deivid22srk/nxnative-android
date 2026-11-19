#include "vulkan_present.h"

#include <android/native_window.h>
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include "vk_api.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace nx2a {
namespace {

constexpr const char* kTag = "nxnative-android";

std::string versionString(std::uint32_t v) {
    std::ostringstream s;
    s << VK_VERSION_MAJOR(v) << '.' << VK_VERSION_MINOR(v) << '.' << VK_VERSION_PATCH(v);
    return s.str();
}

struct Stats {
    std::mutex lock;
    std::string deviceName;
    std::string apiVersion;
    std::string driverVersion;
    std::string error;
    std::uint64_t frames = 0;
    double fps = 0.0;
    int width = 0;
    int height = 0;
    std::string lastInput;

    void clear() {
        deviceName.clear();
        apiVersion.clear();
        driverVersion.clear();
        error.clear();
        frames = 0;
        fps = 0.0;
        width = 0;
        height = 0;
        lastInput.clear();
    }
};

struct Renderer {
    ANativeWindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D extent{};
    VkSurfaceCapabilitiesKHR caps{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> failed{false};
};

Renderer gRenderer;
Stats gStats;
std::mutex gLifecycle;

void destroyAll(Renderer& r);

void setError(const std::string& text) {
    std::lock_guard<std::mutex> lock(gStats.lock);
    gStats.error = text;
}

void destroySwapchain(Renderer& r) {
    if (r.device == VK_NULL_HANDLE) return;
    if (vkDestroyFramebuffer) {
        for (VkFramebuffer fb : r.framebuffers) vkDestroyFramebuffer(r.device, fb, nullptr);
    }
    r.framebuffers.clear();
    if (vkDestroyImageView) {
        for (VkImageView view : r.views) vkDestroyImageView(r.device, view, nullptr);
    }
    r.views.clear();
    if (r.renderPass != VK_NULL_HANDLE && vkDestroyRenderPass) {
        vkDestroyRenderPass(r.device, r.renderPass, nullptr);
        r.renderPass = VK_NULL_HANDLE;
    }
    if (r.swapchain != VK_NULL_HANDLE && vkDestroySwapchainKHR) {
        vkDestroySwapchainKHR(r.device, r.swapchain, nullptr);
        r.swapchain = VK_NULL_HANDLE;
    }
    r.images.clear();
}

bool recreateSwapchain(Renderer& r, int width, int height);

bool createSwapchain(Renderer& r, int width, int height, VkSwapchainKHR old) {
    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r.physical, r.surface, &caps) != VK_SUCCESS) {
        setError("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
        return false;
    }
    r.caps = caps;

    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(r.physical, r.surface, &formatCount, nullptr);
    if (formatCount == 0) {
        setError("surface reports no formats");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(r.physical, r.surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    r.format = chosen.format;
    r.colorSpace = chosen.colorSpace;

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = static_cast<std::uint32_t>(width > 0 ? width : 1080);
        extent.height = static_cast<std::uint32_t>(height > 0 ? height : 1920);
    }
    if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
    if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
    if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
    if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    r.extent = extent;

    std::uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
            composite = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }
    }

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = r.surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = r.format;
    ci.imageColorSpace = r.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = composite;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;

    if (vkCreateSwapchainKHR(r.device, &ci, nullptr, &r.swapchain) != VK_SUCCESS) {
        setError("vkCreateSwapchainKHR failed");
        return false;
    }

    std::uint32_t realCount = 0;
    vkGetSwapchainImagesKHR(r.device, r.swapchain, &realCount, nullptr);
    r.images.resize(realCount);
    vkGetSwapchainImagesKHR(r.device, r.swapchain, &realCount, r.images.data());

    VkAttachmentDescription color{};
    color.format = r.format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    if (vkCreateRenderPass(r.device, &rp, nullptr, &r.renderPass) != VK_SUCCESS) {
        setError("vkCreateRenderPass failed");
        return false;
    }

    r.views.resize(r.images.size(), VK_NULL_HANDLE);
    r.framebuffers.resize(r.images.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < r.images.size(); ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = r.images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = r.format;
        vi.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(r.device, &vi, nullptr, &r.views[i]) != VK_SUCCESS) {
            setError("vkCreateImageView failed");
            return false;
        }
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = r.renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &r.views[i];
        fb.width = r.extent.width;
        fb.height = r.extent.height;
        fb.layers = 1;
        if (vkCreateFramebuffer(r.device, &fb, nullptr, &r.framebuffers[i]) != VK_SUCCESS) {
            setError("vkCreateFramebuffer failed");
            return false;
        }
    }

    std::lock_guard<std::mutex> lock(gStats.lock);
    gStats.width = static_cast<int>(r.extent.width);
    gStats.height = static_cast<int>(r.extent.height);
    return true;
}

bool recreateSwapchain(Renderer& r, int width, int height) {
    vkDeviceWaitIdle(r.device);
    destroySwapchain(r);
    if (!createSwapchain(r, width, height, VK_NULL_HANDLE)) return false;
    return true;
}

bool pickPhysicalDevice(Renderer& r) {
    std::uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(r.instance, &count, nullptr) != VK_SUCCESS || count == 0) {
        setError("no Vulkan physical device");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(r.instance, &count, devices.data());

    for (VkPhysicalDevice dev : devices) {
        std::uint32_t queues = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queues, nullptr);
        std::vector<VkQueueFamilyProperties> props(queues);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queues, props.data());
        for (std::uint32_t i = 0; i < queues; ++i) {
            if (!(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, r.surface, &present) != VK_SUCCESS ||
                !present) {
                continue;
            }
            std::uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> exts(extCount);
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
            bool hasSwapchain = false;
            for (const auto& e : exts) {
                if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                    hasSwapchain = true;
                    break;
                }
            }
            if (!hasSwapchain) continue;

            r.physical = dev;
            r.queueFamily = i;
            VkPhysicalDeviceProperties p{};
            vkGetPhysicalDeviceProperties(dev, &p);
            std::lock_guard<std::mutex> lock(gStats.lock);
            gStats.deviceName = p.deviceName;
            gStats.apiVersion = versionString(p.apiVersion);
            gStats.driverVersion = versionString(p.driverVersion);
            return true;
        }
    }
    setError("no Vulkan device supports graphics + present + VK_KHR_swapchain");
    return false;
}

bool initVulkan(Renderer& r) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "nxnative-android";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "none";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = extensions;
    if (vkCreateInstance(&ci, nullptr, &r.instance) != VK_SUCCESS) {
        setError(gVkCustomDriver
                     ? "vkCreateInstance failed with the selected driver (Turnip)"
                     : "vkCreateInstance failed (no Vulkan loader?)");
        return false;
    }
    std::string loadError;
    if (!vkApiLoadInstance(r.instance, &loadError)) {
        setError(loadError);
        return false;
    }

    VkAndroidSurfaceCreateInfoKHR surface{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    surface.window = r.window;
    if (vkCreateAndroidSurfaceKHR(r.instance, &surface, nullptr, &r.surface) != VK_SUCCESS) {
        setError("vkCreateAndroidSurfaceKHR failed");
        return false;
    }

    if (!pickPhysicalDevice(r)) return false;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = r.queueFamily;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &q;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = deviceExtensions;
    if (vkCreateDevice(r.physical, &dci, nullptr, &r.device) != VK_SUCCESS) {
        setError("vkCreateDevice failed");
        return false;
    }
    std::string deviceError;
    if (!vkApiLoadDevice(r.device, &deviceError)) {
        setError(deviceError);
        return false;
    }
    vkGetDeviceQueue(r.device, r.queueFamily, 0, &r.queue);

    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = r.queueFamily;
    if (vkCreateCommandPool(r.device, &pool, nullptr, &r.commandPool) != VK_SUCCESS) {
        setError("vkCreateCommandPool failed");
        return false;
    }
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = r.commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(r.device, &alloc, &r.command) != VK_SUCCESS) {
        setError("vkAllocateCommandBuffers failed");
        return false;
    }

    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateSemaphore(r.device, &sem, nullptr, &r.imageAvailable) != VK_SUCCESS ||
        vkCreateSemaphore(r.device, &sem, nullptr, &r.renderFinished) != VK_SUCCESS ||
        vkCreateFence(r.device, &fence, nullptr, &r.inFlight) != VK_SUCCESS) {
        setError("sync object creation failed");
        return false;
    }
    return true;
}

void renderLoop(Renderer& r) {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    std::uint64_t framesInWindow = 0;
    float phase = 0.0f;

    while (r.running.load()) {
        const int width = ANativeWindow_getWidth(r.window);
        const int height = ANativeWindow_getHeight(r.window);
        if (width > 0 && height > 0 &&
            (static_cast<std::uint32_t>(width) != r.extent.width ||
             static_cast<std::uint32_t>(height) != r.extent.height)) {
            if (!recreateSwapchain(r, width, height)) {
                r.failed.store(true);
                break;
            }
        }

        std::uint32_t index = 0;
        const VkResult acquire = vkAcquireNextImageKHR(r.device, r.swapchain, UINT64_MAX,
                                                       r.imageAvailable, VK_NULL_HANDLE, &index);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!recreateSwapchain(r, width, height)) break;
            continue;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            setError("vkAcquireNextImageKHR failed");
            r.failed.store(true);
            break;
        }

        phase += 0.01f;
        const float r0 = 0.05f + 0.04f * std::sin(phase);
        const float g0 = 0.06f + 0.04f * std::sin(phase + 2.0f);
        const float b0 = 0.10f + 0.05f * std::sin(phase + 4.0f);

        vkResetCommandBuffer(r.command, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(r.command, &begin);
        VkClearValue clear{};
        clear.color = {{r0, g0, b0, 1.0f}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = r.renderPass;
        rp.framebuffer = r.framebuffers[index];
        rp.renderArea = {{0, 0}, r.extent};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(r.command, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(r.command);
        vkEndCommandBuffer(r.command);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &r.imageAvailable;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &r.command;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &r.renderFinished;
        if (vkQueueSubmit(r.queue, 1, &submit, r.inFlight) != VK_SUCCESS) {
            setError("vkQueueSubmit failed");
            r.failed.store(true);
            break;
        }
        vkWaitForFences(r.device, 1, &r.inFlight, VK_TRUE, UINT64_MAX);
        vkResetFences(r.device, 1, &r.inFlight);

        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &r.renderFinished;
        present.swapchainCount = 1;
        present.pSwapchains = &r.swapchain;
        present.pImageIndices = &index;
        const VkResult presented = vkQueuePresentKHR(r.queue, &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain(r, width, height);
        }

        ++framesInWindow;
        const auto now = clock::now();
        const auto elapsed = std::chrono::duration<double>(now - last).count();
            if (elapsed >= 1.0) {
            {
                std::lock_guard<std::mutex> lock(gStats.lock);
                gStats.fps = static_cast<double>(framesInWindow) / elapsed;
                gStats.frames += framesInWindow;
            }
            framesInWindow = 0;
            last = now;
        }
    }
}

void resetRenderer(Renderer& r) {
    r.instance = VK_NULL_HANDLE;
    r.surface = VK_NULL_HANDLE;
    r.physical = VK_NULL_HANDLE;
    r.device = VK_NULL_HANDLE;
    r.queueFamily = 0;
    r.queue = VK_NULL_HANDLE;
    r.swapchain = VK_NULL_HANDLE;
    r.format = VK_FORMAT_UNDEFINED;
    r.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    r.extent = VkExtent2D{0, 0};
    r.caps = VkSurfaceCapabilitiesKHR{};
    r.images.clear();
    r.views.clear();
    r.framebuffers.clear();
    r.renderPass = VK_NULL_HANDLE;
    r.commandPool = VK_NULL_HANDLE;
    r.command = VK_NULL_HANDLE;
    r.imageAvailable = VK_NULL_HANDLE;
    r.renderFinished = VK_NULL_HANDLE;
    r.inFlight = VK_NULL_HANDLE;
    r.running.store(false);
    r.failed.store(false);
}

void stopLocked() {
    gRenderer.running.store(false);
    if (gRenderer.thread.joinable()) gRenderer.thread.join();
    destroyAll(gRenderer);
    if (gRenderer.window != nullptr) ANativeWindow_release(gRenderer.window);
    gRenderer.window = nullptr;
    vkApiClose();
}

void destroyAll(Renderer& r) {
    if (r.device != VK_NULL_HANDLE && vkDeviceWaitIdle) vkDeviceWaitIdle(r.device);
    destroySwapchain(r);
    if (r.device != VK_NULL_HANDLE) {
        if (r.imageAvailable && vkDestroySemaphore) vkDestroySemaphore(r.device, r.imageAvailable, nullptr);
        if (r.renderFinished && vkDestroySemaphore) vkDestroySemaphore(r.device, r.renderFinished, nullptr);
        if (r.inFlight && vkDestroyFence) vkDestroyFence(r.device, r.inFlight, nullptr);
        if (r.commandPool && vkDestroyCommandPool) vkDestroyCommandPool(r.device, r.commandPool, nullptr);
        if (vkDestroyDevice) vkDestroyDevice(r.device, nullptr);
        r.device = VK_NULL_HANDLE;
    }
    if (r.surface != VK_NULL_HANDLE && r.instance != VK_NULL_HANDLE && vkDestroySurfaceKHR) {
        vkDestroySurfaceKHR(r.instance, r.surface, nullptr);
        r.surface = VK_NULL_HANDLE;
    }
    if (r.instance != VK_NULL_HANDLE && vkDestroyInstance) {
        vkDestroyInstance(r.instance, nullptr);
        r.instance = VK_NULL_HANDLE;
    }
}

std::string jsonEscape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out;
}

}  // namespace

bool rendererStart(ANativeWindow* window, int width, int height,
                   const std::string& hookDir, const std::string& driverDir,
                   const std::string& driverName, const std::string& tmpDir,
                   std::string* error) {
    std::lock_guard<std::mutex> lock(gLifecycle);
    stopLocked();
    resetRenderer(gRenderer);
    gRenderer.window = window;
    {
        std::lock_guard<std::mutex> stats(gStats.lock);
        gStats.clear();
    }
    if (window == nullptr) {
        if (error) *error = "no surface";
        return false;
    }
    ANativeWindow_acquire(window);
    std::string message;
    if (!vkApiOpen(hookDir, driverDir, driverName, tmpDir, &message)) {
        if (error) *error = message;
        ANativeWindow_release(window);
        gRenderer.window = nullptr;
        return false;
    }
    if (!initVulkan(gRenderer) || !createSwapchain(gRenderer, width, height, VK_NULL_HANDLE)) {
        {
            std::lock_guard<std::mutex> stats(gStats.lock);
            message = gStats.error;
        }
        if (error) *error = message.empty() ? "Vulkan initialisation failed" : message;
        destroyAll(gRenderer);
        vkApiClose();
        ANativeWindow_release(window);
        gRenderer.window = nullptr;
        return false;
    }
    gRenderer.running.store(true);
    gRenderer.thread = std::thread([&r = gRenderer] { renderLoop(r); });
    return true;
}

void rendererStop() {
    std::lock_guard<std::mutex> lock(gLifecycle);
    stopLocked();
}

std::string rendererStatsJson() {
    std::lock_guard<std::mutex> lock(gStats.lock);
    std::ostringstream j;
    j << "{\"device\":\"" << jsonEscape(gStats.deviceName) << "\","
      << "\"api\":\"" << jsonEscape(gStats.apiVersion) << "\","
      << "\"driver\":\"" << jsonEscape(gStats.driverVersion) << "\","
      << "\"frames\":" << gStats.frames << ","
      << "\"fps\":" << static_cast<int>(gStats.fps + 0.5) << ","
      << "\"width\":" << gStats.width << ",\"height\":" << gStats.height << ","
      << "\"lastInput\":\"" << jsonEscape(gStats.lastInput) << "\","
      << "\"customDriver\":" << (gVkCustomDriver ? "true" : "false") << ","
      << "\"error\":\"" << jsonEscape(gStats.error) << "\"}";
    return j.str();
}

void rendererInput(int action, float x, float y, int code) {
    std::lock_guard<std::mutex> lock(gStats.lock);
    std::ostringstream s;
    s << "action=" << action << " x=" << static_cast<int>(x) << " y=" << static_cast<int>(y)
      << " code=" << code;
    gStats.lastInput = s.str();
}

}  // namespace nx2a
