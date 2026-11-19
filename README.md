# nxnative-android

An Android **shell** for the [nxnative](https://github.com/seedds/nxnative) /
[nx2android](https://github.com/shirai91/nx2android) approach of running a
Switch game's own ARM64 code on the device: the game is not emulated, its
modules are rewritten to run natively and its NVN graphics are replayed on
Vulkan.

**Bring your own game. This repository contains no game, no key and no game
data, and the app never downloads anything.**

> **Status (read this first).** This repository is at **M1**: a working
> Android app that shows the mandatory Terms, imports `.nsp` + `prod.keys`
> through the system file picker, validates both in native C++, copies the
> game once into private storage, and offers a player screen with a real
> Vulkan presentation path and GPU diagnostics.
>
> The game runtime (NSO rewrite, Horizon HLE, NVN→Vulkan replay) is **not**
> integrated yet, so **no game runs**. See [`PROGRESS.md`](PROGRESS.md) for
> the evidence levels and [`docs/ANDROID_PORT_PLAN.md`](docs/ANDROID_PORT_PLAN.md)
> for the path to a playable port. The runtime itself lives in
> `shirai91/nx2android` (`nxnative/dm/android`, `nxnative/dm/host`) and is the
> realistic base for M4.

## What works today

- Mandatory, versioned Terms of Use (pt-BR + English), persisted in DataStore.
  Refusing closes the app; the dialog cannot be dismissed by tapping outside
  or with the back button.
- Import screen with three pickers: ROM (`.nsp`), `prod.keys`, optional
  `title.keys`. Persistable SAF grants.
- Native validation of the key file (`name = hex`, line-numbered errors) and
  of the NSP (`PFS0` header, entry table, ticket rights ID → **Title ID** and
  **key generation**, NCA inventory).
- Precise refusal when `prod.keys` does not cover the NSP's key generation:
  the app lists only the missing **names** (for example `titlekek_10`), never
  a value, and **nothing is copied**.
- Streaming copy of the NSP into `filesDir/games/<titleId>/base.nsp` with
  progress, cancellation and resume from a `.part` file.
- Library (Title ID, size, key generation, import date), delete per game,
  delete all, settings, diagnostics log with export.
- Player screen: `SurfaceView` + `VK_KHR_android_surface` swapchain,
  clear/present loop, GPU name, Vulkan API version, FPS/frame counter,
  touch joystick/buttons and gamepad key forwarding. This is diagnostics,
  not the game renderer.

## What is deliberately out of scope (yet)

`.nsz`, `.xcz` and `.xci` are rejected with a clear message (the PC porter can
convert them). NCA decryption, RomFS/ExeFS extraction, NSO rewriting and the
NVN→Vulkan backend are milestones M2–M4, not part of this version.

## Build

Requirements: JDK 17, Android SDK with **NDK 27.2.12479018** and CMake
3.22.1, or just push to GitHub — `.github/workflows/build.yml` installs them
and builds both APKs.

```bash
export ANDROID_HOME=$HOME/Android/Sdk
./gradlew assembleDebug
# app/build/outputs/apk/debug/app-debug.apk
```

Native host tests (no SDK, no device):

```bash
bash tools/test_native.sh
```

Audit before pushing (no game files, no keys, no huge blobs):

```bash
bash tools/audit_no_assets.sh
```

## Use

1. Install the APK (`arm64-v8a` only, Android 10+).
2. Read and accept the Terms.
3. **Importar**: pick your own `.nsp`, your `prod.keys` (and `title.keys` if
   you have one).
4. The app validates the container and the key generation. If the keys are
   enough, it copies the ROM once into private storage. If not, it names the
   missing keys and stops.
5. **Biblioteca**: the imported entry appears with its Title ID and size.

Nothing is uploaded, and the app has no `INTERNET` permission.

## Legal

This is an independent project, not affiliated with Nintendo, Warner Bros.
Games, Avalanche Software or any other company. It ships no game assets, no
keys and no publisher trademarks. You are responsible for using files you
legally own. The Terms inside the app say the same thing and must be accepted
before importing anything.

## Credits / licences

See [`THIRD_PARTY.md`](THIRD_PARTY.md). The runtime architecture this app is a
front end for is MIT-licensed work by the nxnative/nx2android authors, built
on Ryujinx (shader translator), LibHac, SPIRV-Cross, glslang and others.
