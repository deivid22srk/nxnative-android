// Minimal Vulkan presentation path for the launcher's player screen.
//
// This is NOT the game renderer: it creates a VK_KHR_android_surface
// swapchain, clears the surface and presents every vsync, and reports GPU
// facts for the diagnostics screen. It exists so the app has a real,
// testable Vulkan lifecycle on device (surface create/destroy, swapchain
// recreation, queue submit) before the NVN replay backend is linked in.
#pragma once

#include <string>

struct ANativeWindow;

namespace nx2a {

// driverDir/driverName empty: system Vulkan loader. Otherwise the named
// driver library is loaded from driverDir through libadrenotools, using the
// hook libraries in hookDir and tmpDir for writable scratch space.
bool rendererStart(ANativeWindow* window, int width, int height,
                   const std::string& hookDir, const std::string& driverDir,
                   const std::string& driverName, const std::string& tmpDir,
                   std::string* error);
void rendererStop();
std::string rendererStatsJson();
void rendererInput(int action, float x, float y, int code);

}  // namespace nx2a
