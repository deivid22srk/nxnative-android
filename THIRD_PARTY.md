# Third-party software and licences

This repository contains **no game data, no keys and no third-party runtime
binaries**. The Android app links only the AndroidX/Compose/Kotlin libraries
resolved by Gradle. The table records the projects the runtime work builds on
(and will vendor at milestone M4), with their licences, so attribution is not
lost when that happens.

| Project | Licence | Used for | Present in this repo? |
|---|---|---|---|
| [nxnative](https://github.com/seedds/nxnative) | MIT (© 2026 seedds) | architecture: NSO rewriter, Horizon host HLE, NVN replay, app shell | no (upstream) |
| [nx2android](https://github.com/shirai91/nx2android) | MIT | the Android port of nxnative: `dm/android` Vulkan backend, Android front end, game profiles | no (upstream) |
| [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) | MIT | original native-execution approach that nxnative derives from | no |
| [Ryujinx](https://github.com/Ryujinx/Ryujinx) (`Ryujinx.Graphics.Shader`, `Ryujinx.Common`, `Spv.Generator`) | MIT | Maxwell shaders → SPIR-V | no |
| [LibHac](https://github.com/Thealexbarney/LibHac) | MIT | NSP/NCA/RomFS reading in the PC porter (`nx_extract`) | no |
| [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) | MIT | reference for Horizon kernel synchronisation semantics | no |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | Apache-2.0 | SPIR-V → MSL (iOS path; not needed on Android) | no |
| [glslang](https://github.com/KhronosGroup/glslang) | BSD-3-Clause / Khronos | GLSL → SPIR-V for OpenGL titles | no |
| deko3d | zlib | texture layout size algorithm (ported into nxnative) | no |
| libvpx | BSD-3-Clause | VP9 movie decoding | no |
| stb_vorbis | MIT / public domain | movie audio | no |
| NVIDIA FLIP | MIT | image comparison in diagnostics | no |
| Vulkan headers / loader | Apache-2.0 | graphics API | headers come from the NDK |
| AndroidX, Jetpack Compose, Kotlin, kotlinx.coroutines | Apache-2.0 | this app's UI and coroutines | via Gradle |

The MIT licence text that applies to this repository (and preserves the
nxnative copyright notice) is in [`LICENSE`](LICENSE).

## Trademarks

Nintendo Switch is a trademark of Nintendo. No publisher or platform logo is
used. The app icon is a neutral geometric mark drawn for this project.
