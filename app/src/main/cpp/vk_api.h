// Vulkan entry points resolved at run time, so the renderer can use either
// the system loader or a custom driver loaded through libadrenotools (Turnip).
#pragma once

#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <string>

namespace nx2a {

struct VkApi {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
    PFN_vkCreateInstance CreateInstance = nullptr;
    PFN_vkDestroyInstance DestroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties = nullptr;
    PFN_vkCreateDevice CreateDevice = nullptr;
    PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
    PFN_vkCreateAndroidSurfaceKHR CreateAndroidSurfaceKHR = nullptr;
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR = nullptr;

    PFN_vkDestroyDevice DestroyDevice = nullptr;
    PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
    PFN_vkQueueSubmit QueueSubmit = nullptr;
    PFN_vkCreateRenderPass CreateRenderPass = nullptr;
    PFN_vkDestroyRenderPass DestroyRenderPass = nullptr;
    PFN_vkCreateImageView CreateImageView = nullptr;
    PFN_vkDestroyImageView DestroyImageView = nullptr;
    PFN_vkCreateFramebuffer CreateFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer DestroyFramebuffer = nullptr;
    PFN_vkCreateCommandPool CreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer ResetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass CmdEndRenderPass = nullptr;
    PFN_vkCreateSemaphore CreateSemaphore = nullptr;
    PFN_vkDestroySemaphore DestroySemaphore = nullptr;
    PFN_vkCreateFence CreateFence = nullptr;
    PFN_vkDestroyFence DestroyFence = nullptr;
    PFN_vkWaitForFences WaitForFences = nullptr;
    PFN_vkResetFences ResetFences = nullptr;
};

extern VkApi gVk;
extern bool gVkCustomDriver;

// Opens libvulkan or (driverDir + driverName, through libadrenotools) and
// resolves the global entry points. hookDir is the APK's nativeLibraryDir,
// tmpDir a writable directory (cacheDir).
bool vkApiOpen(const std::string& hookDir, const std::string& driverDir,
               const std::string& driverName, const std::string& tmpDir,
               std::string* error);

// Resolve the instance/device entry points. Call right after the matching
// object is created.
bool vkApiLoadInstance(VkInstance instance, std::string* error);
bool vkApiLoadDevice(VkDevice device, std::string* error);

void vkApiClose();

}  // namespace nx2a

// Keep the renderer source unchanged: every Vulkan call goes through the table.
#define vkCreateInstance (::nx2a::gVk.CreateInstance)
#define vkDestroyInstance (::nx2a::gVk.DestroyInstance)
#define vkEnumeratePhysicalDevices (::nx2a::gVk.EnumeratePhysicalDevices)
#define vkGetPhysicalDeviceQueueFamilyProperties \
    (::nx2a::gVk.GetPhysicalDeviceQueueFamilyProperties)
#define vkGetPhysicalDeviceSurfaceSupportKHR (::nx2a::gVk.GetPhysicalDeviceSurfaceSupportKHR)
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR \
    (::nx2a::gVk.GetPhysicalDeviceSurfaceCapabilitiesKHR)
#define vkGetPhysicalDeviceSurfaceFormatsKHR \
    (::nx2a::gVk.GetPhysicalDeviceSurfaceFormatsKHR)
#define vkGetPhysicalDeviceProperties (::nx2a::gVk.GetPhysicalDeviceProperties)
#define vkEnumerateDeviceExtensionProperties \
    (::nx2a::gVk.EnumerateDeviceExtensionProperties)
#define vkCreateDevice (::nx2a::gVk.CreateDevice)
#define vkGetDeviceQueue (::nx2a::gVk.GetDeviceQueue)
#define vkCreateAndroidSurfaceKHR (::nx2a::gVk.CreateAndroidSurfaceKHR)
#define vkDestroySurfaceKHR (::nx2a::gVk.DestroySurfaceKHR)
#define vkDestroyDevice (::nx2a::gVk.DestroyDevice)
#define vkDeviceWaitIdle (::nx2a::gVk.DeviceWaitIdle)
#define vkCreateSwapchainKHR (::nx2a::gVk.CreateSwapchainKHR)
#define vkDestroySwapchainKHR (::nx2a::gVk.DestroySwapchainKHR)
#define vkGetSwapchainImagesKHR (::nx2a::gVk.GetSwapchainImagesKHR)
#define vkAcquireNextImageKHR (::nx2a::gVk.AcquireNextImageKHR)
#define vkQueuePresentKHR (::nx2a::gVk.QueuePresentKHR)
#define vkQueueSubmit (::nx2a::gVk.QueueSubmit)
#define vkCreateRenderPass (::nx2a::gVk.CreateRenderPass)
#define vkDestroyRenderPass (::nx2a::gVk.DestroyRenderPass)
#define vkCreateImageView (::nx2a::gVk.CreateImageView)
#define vkDestroyImageView (::nx2a::gVk.DestroyImageView)
#define vkCreateFramebuffer (::nx2a::gVk.CreateFramebuffer)
#define vkDestroyFramebuffer (::nx2a::gVk.DestroyFramebuffer)
#define vkCreateCommandPool (::nx2a::gVk.CreateCommandPool)
#define vkDestroyCommandPool (::nx2a::gVk.DestroyCommandPool)
#define vkAllocateCommandBuffers (::nx2a::gVk.AllocateCommandBuffers)
#define vkResetCommandBuffer (::nx2a::gVk.ResetCommandBuffer)
#define vkBeginCommandBuffer (::nx2a::gVk.BeginCommandBuffer)
#define vkEndCommandBuffer (::nx2a::gVk.EndCommandBuffer)
#define vkCmdBeginRenderPass (::nx2a::gVk.CmdBeginRenderPass)
#define vkCmdEndRenderPass (::nx2a::gVk.CmdEndRenderPass)
#define vkCreateSemaphore (::nx2a::gVk.CreateSemaphore)
#define vkDestroySemaphore (::nx2a::gVk.DestroySemaphore)
#define vkCreateFence (::nx2a::gVk.CreateFence)
#define vkDestroyFence (::nx2a::gVk.DestroyFence)
#define vkWaitForFences (::nx2a::gVk.WaitForFences)
#define vkResetFences (::nx2a::gVk.ResetFences)