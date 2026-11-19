# Hogwarts Legacy — Android bring-up runbook

Status: **experimental, blocked on a physical device**. This is the exact
state after the 2026-11-19 attempt with the user's own files and keys. No game
data, no key and no extracted asset is in this repository.

## What already succeeded (on the PC, in `/tmp/hlport`, outside the repo)

| Step | Result |
|---|---|
| Key validation (`porter doctor`, `inspect`) | `prod2.keys`: 233 keys, max generation `0x12`; the NSP needs generation 16 → **no missing keys** |
| Survey (`porter port --experimental`) | display version **1.0.0**, SDK 16.2.3 |
| Engine / API markers | **Unreal Engine 4**, **NVN**, Wwise; modules `main` (93.5 MB text), `subsdk0` (NintendoSDK_movie), `sdk` (nnSdk) |
| NCA / RomFS extraction | **190 files, 6.8 GB**; RomFS 185 files / 6.75 GB, `Phoenix/Content` 6.74 GB, `.ucas`/`.utoc`/`.pak` (IoStore + paks) |
| Provisional profile | generated (`game.tsv` + `android.tsv` with `status experimental`), `mem_reserve 0x1028000000`, `mem_alias 0x1008000000` |
| Rewrite-site census | `main`: 24.5 M instructions decoded, 48,962 `x18` uses; imports: 825 nn::* functions not used by the older reference title |

Extraction and census are **PC-side**: they do not make L2 (on-device
preparation) true.

## The blocker

Hogwarts Legacy keeps its shaders in **Oodle-compressed Unreal shader code
archives** (9 paks). The only decoder available is the game's own ARM64
`OodleLZ_Decompress` code, which the porter runs **on an Android device**
(`porter/pipeline.py`, `device_ue`, `DM_OODLE_JOB`). Therefore:

```
[RED] Hogwarts Legacy's shaders are in Oodle-compressed archives (...):
      the port decodes them with the game's own decoder on the Android
      device, so connect it (adb) and port again (no device connected)
```

There is no device attached to this machine, and a cloud container cannot see
the user's USB device. The port stops before NSO rewriting, shader
translation, host build and APK assembly.

## Device requirements

- **GPU that samples BC textures**: Adreno or Xclipse (not Mali/PowerVR).
  Hogwarts Legacy is BC-heavy (BC1/BC3/BC4/BC5/BC7).
- Android 10+ (`minSdk 29`), **arm64-v8a**.
- **12 GB RAM or more** recommended: the provisional profile reserves a 62 GB
  virtual region and UE4's `FMallocBinned3` takes 37 GB of it; the address
  space is fine on arm64 but a 37 GB allocator reservation needs a cut-down
  build like the Octopath Traveler 0 profile (`game_args`).
- ~20 GB free on the device for the data pack (RomFS 6.75 GB + rewritten
  modules; the NSP is not copied to the device).
- USB debugging, or `adb connect <ip>:<port>` (wireless debugging).

## Run it (on a PC with the device attached)

```bash
git clone https://github.com/shirai91/nx2android && cd nx2android
python3 -m pip install -r requirements.txt
python3 -m porter config keys /path/to/prod2.keys
python3 -m porter config ndk  /path/to/android-ndk-r27c
python3 -m porter config dotnet /path/to/dotnet
python3 -m porter config adb /path/to/platform-tools/adb

# game folder: only the base NSP (an update, if you have one, goes beside it)
python3 -m porter inspect /path/to/HogwartsFolder
python3 -m porter port /path/to/HogwartsFolder --experimental --install
```

Expected sequence after the device is detected:

1. `device_ue`: build the Android host as an executable, push it plus the
   rewritten modules to `/data/local/tmp/nx2a-oodle`, run the game's Oodle on
   the 9 shader archives, pull the decoded streams.
2. NSO rewrite (`DM_TARGET=android`), SPIR-V translation of the shader corpus
   (thousands of variants — this is the long step), Vulkan shader bundle.
3. Host (`libdmhost.so`) built with the NDK, APK, data pack, install.

The extracted `..._build` and survey folders from this attempt can be copied
to that PC to skip extraction (same version, same build IDs).

## Honest expectations

- This is a bring-up of an unsupported UE4 title, not a playable port. The
  provisional profile guesses every value (memory layout, shader groups,
  modules, `zip_store`).
- First likely failures: the 37 GB allocator reservation on a phone, IoStore
  shader archive parsing, missing NVN/Vulkan features used by UE4, and remote
  (non-pipeline) shader permutations compiled at run time.
- Any runtime fix must go into the shared `dm/android` / `dm/host` code, not a
  title-specific branch, following the project's rules.
- Report with: the porter log, `adb logcat` (filter `dmhost`, `nxnative`),
  and the crash address if it reaches the game's `main`.
