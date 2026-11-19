#include "vk_api.h"

#include <dlfcn.h>

#include <sstream>
#include <vector>

#include "adrenotools/driver.h"

namespace nx2a {

VkApi gVk;
bool gVkCustomDriver = false;

namespace {

void* gLibrary = nullptr;

template <typename Fn>
void loadFrom(PFN_vkGetInstanceProcAddr getInstance, VkInstance instance,
              PFN_vkGetDeviceProcAddr getDevice, VkDevice device, const char* name, Fn* out,
              std::vector<std::string>* missing) {
    Fn resolved = nullptr;
    if (device != VK_NULL_HANDLE && getDevice != nullptr) {
        resolved = reinterpret_cast<Fn>(getDevice(device, name));
    }
    if (resolved == nullptr && getInstance != nullptr) {
        resolved = reinterpret_cast<Fn>(getInstance(instance, name));
    }
    *out = resolved;
    if (resolved == nullptr && missing != nullptr) missing->push_back(name);
}

std::string joinMissing(const std::vector<std::string>& missing) {
    std::ostringstream s;
    for (std::size_t i = 0; i < missing.size(); ++i) {
        if (i) s << ", ";
        s << missing[i];
    }
    return s.str();
}

}  // namespace

bool vkApiOpen(const std::string& hookDir, const std::string& driverDir,
               const std::string& driverName, const std::string& tmpDir,
               std::string* error) {
    vkApiClose();
    gVkCustomDriver = false;

    if (!driverDir.empty() && !driverName.empty()) {
        void* adrenotools = dlopen("libadrenotools.so", RTLD_NOW | RTLD_LOCAL);
        if (adrenotools == nullptr) {
            if (error) *error = std::string("libadrenotools.so: ") + dlerror();
            return false;
        }
        using OpenFn = void* (*)(int, int, const char*, const char*, const char*, const char*,
                                 const char*, void**);
        auto open = reinterpret_cast<OpenFn>(dlsym(adrenotools, "adrenotools_open_libvulkan"));
        if (open == nullptr) {
            if (error) *error = "adrenotools_open_libvulkan is missing";
            return false;
        }
        std::string customDir = driverDir;
        if (customDir.back() != '/') customDir.push_back('/');
        gLibrary = open(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM,
                        tmpDir.empty() ? nullptr : tmpDir.c_str(), hookDir.c_str(),
                        customDir.c_str(), driverName.c_str(), nullptr, nullptr);
        if (gLibrary == nullptr) {
            if (error) *error = "adrenotools could not load " + driverName;
            return false;
        }
        gVkCustomDriver = true;
    } else {
        gLibrary = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (gLibrary == nullptr) {
            if (error) *error = std::string("libvulkan.so: ") + dlerror();
            return false;
        }
    }

    gVk.GetInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(gLibrary, "vkGetInstanceProcAddr"));
    if (gVk.GetInstanceProcAddr == nullptr) {
        if (error) *error = "vkGetInstanceProcAddr is missing";
        vkApiClose();
        return false;
    }
    gVk.CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
        gVk.GetInstanceProcAddr(nullptr, "vkCreateInstance"));
    if (gVk.CreateInstance == nullptr) {
        if (error) *error = "vkCreateInstance is missing";
        vkApiClose();
        return false;
    }
    return true;
}

bool vkApiLoadInstance(VkInstance instance, std::string* error) {
    std::vector<std::string> missing;
    const auto gi = gVk.GetInstanceProcAddr;
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkDestroyInstance", &gVk.DestroyInstance,
             &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkEnumeratePhysicalDevices",
             &gVk.EnumeratePhysicalDevices, &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkGetPhysicalDeviceQueueFamilyProperties",
             &gVk.GetPhysicalDeviceQueueFamilyProperties, &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkGetPhysicalDeviceSurfaceSupportKHR",
             &gVk.GetPhysicalDeviceSurfaceSupportKHR, &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
             &gVk.GetPhysicalDeviceSurfaceCapabilitiesKHR, &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkGetPhysicalDeviceSurfaceFormatsKHR",
             &gVk.GetPhysicalDeviceSurfaceFormatsKHR, &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkGetPhysicalDeviceProperties",
             &gVk.GetPhysicalDeviceProperties, &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkEnumerateDeviceExtensionProperties",
             &gVk.EnumerateDeviceExtensionProperties, &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkCreateDevice", &gVk.CreateDevice, &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkGetDeviceQueue", &gVk.GetDeviceQueue,
             &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkCreateAndroidSurfaceKHR",
             &gVk.CreateAndroidSurfaceKHR, &missing);
    loadFrom(gi, instance, nullptr, VK_NULL_HANDLE, "vkDestroySurfaceKHR",
             &gVk.DestroySurfaceKHR, &missing);
    gVk.GetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        gi(instance, "vkGetDeviceProcAddr"));
    if (gVk.GetDeviceProcAddr == nullptr) missing.push_back("vkGetDeviceProcAddr");
    if (!missing.empty()) {
        if (error) *error = "Vulkan instance is missing " + joinMissing(missing);
        return false;
    }
    return true;
}

bool vkApiLoadDevice(VkDevice device, std::string* error) {
    std::vector<std::string> missing;
    const auto gi = gVk.GetInstanceProcAddr;
    const auto gd = gVk.GetDeviceProcAddr;
    const VkInstance noInstance = VK_NULL_HANDLE;
#define LOAD_DEVICE(field, name) \
    loadFrom(gi, noInstance, gd, device, name, &gVk.field, &missing)
    LOAD_DEVICE(DestroyDevice, "vkDestroyDevice");
    LOAD_DEVICE(DeviceWaitIdle, "vkDeviceWaitIdle");
    LOAD_DEVICE(CreateSwapchainKHR, "vkCreateSwapchainKHR");
    LOAD_DEVICE(DestroySwapchainKHR, "vkDestroySwapchainKHR");
    LOAD_DEVICE(GetSwapchainImagesKHR, "vkGetSwapchainImagesKHR");
    LOAD_DEVICE(AcquireNextImageKHR, "vkAcquireNextImageKHR");
    LOAD_DEVICE(QueuePresentKHR, "vkQueuePresentKHR");
    LOAD_DEVICE(QueueSubmit, "vkQueueSubmit");
    LOAD_DEVICE(CreateRenderPass, "vkCreateRenderPass");
    LOAD_DEVICE(DestroyRenderPass, "vkDestroyRenderPass");
    LOAD_DEVICE(CreateImageView, "vkCreateImageView");
    LOAD_DEVICE(DestroyImageView, "vkDestroyImageView");
    LOAD_DEVICE(CreateFramebuffer, "vkCreateFramebuffer");
    LOAD_DEVICE(DestroyFramebuffer, "vkDestroyFramebuffer");
    LOAD_DEVICE(CreateCommandPool, "vkCreateCommandPool");
    LOAD_DEVICE(DestroyCommandPool, "vkDestroyCommandPool");
    LOAD_DEVICE(AllocateCommandBuffers, "vkAllocateCommandBuffers");
    LOAD_DEVICE(ResetCommandBuffer, "vkResetCommandBuffer");
    LOAD_DEVICE(BeginCommandBuffer, "vkBeginCommandBuffer");
    LOAD_DEVICE(EndCommandBuffer, "vkEndCommandBuffer");
    LOAD_DEVICE(CmdBeginRenderPass, "vkCmdBeginRenderPass");
    LOAD_DEVICE(CmdEndRenderPass, "vkCmdEndRenderPass");
    LOAD_DEVICE(CreateSemaphore, "vkCreateSemaphore");
    LOAD_DEVICE(DestroySemaphore, "vkDestroySemaphore");
    LOAD_DEVICE(CreateFence, "vkCreateFence");
    LOAD_DEVICE(DestroyFence, "vkDestroyFence");
    LOAD_DEVICE(WaitForFences, "vkWaitForFences");
    LOAD_DEVICE(ResetFences, "vkResetFences");
#undef LOAD_DEVICE
    if (!missing.empty()) {
        if (error) *error = "Vulkan device is missing " + joinMissing(missing);
        return false;
    }
    return true;
}

void vkApiClose() {
    gVk = VkApi{};
    if (gLibrary != nullptr) {
        dlclose(gLibrary);
        gLibrary = nullptr;
    }
    gVkCustomDriver = false;
}

}  // namespace nx2a