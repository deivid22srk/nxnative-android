# Android port plan — nxnative → Android

Status: living document. Started 2026-11-19.
Companion: [`PROGRESS.md`](../PROGRESS.md), [`HOGWARTS_LEGACY_SURVEY.md`](HOGWARTS_LEGACY_SURVEY.md).

## 0. What is actually on this machine (and a correction to the brief)

The brief assumed the base project is Apple-only (`seedds/nxnative`) and that
the Android port has to be written from scratch. The checkout that is
available here is **`shirai91/nx2android`**, which is *already* the Android
port of nxnative:

- `nxnative/dm/android/` — the Android runtime: Vulkan backend
  (`gfx_vk_*.c`), native front end (`front_android.c`), Java/Kotlin bridge,
  AAudio, haptics, LAN, saves (`dm/android/`).
- `nxnative/dm/games/<title id>/android.tsv` — one data-only profile per
  supported game (about 40 games).
- `porter/` — the host-side pipeline: NSP/NSZ/XCI extraction, NSO rewrite,
  shader translation, APK + data-pack assembly, install over adb.
- `nxnative/scripts/build_ipa_from_nsp.py`, `nxnative/dm/tools/nx_extract`
  and `nxnative/dm/games/profile_guess.py` — the survey flow used for a game
  that has no profile yet.

This changes the plan in a useful way: the hard parts (NSO rewriter, host
HLE for Horizon, NVN→Vulkan) already exist and are tested on Android. The
work in *this* repository is therefore:

1. a **standalone on-device app** (the brief's deliverable: terms gate,
   SAF import, key/NSP validation, library, player shell), and
2. an honest, evidence-based path to get a specific title (Hogwarts Legacy)
   supported by the existing nx2android runtime — i.e. a profile plus the
   fixes it needs — instead of pretending a green CI build equals a running
   game.

The two tracks are connected: the validator in this app uses the same
rules the porter uses (PFS0 → ticket → title id + key generation → required
`prod.keys` entries), and the app's player shell is where the nx2android
runtime would be linked once its build is integrated into an app module.

## 1. Inventory of the nxnative/nx2android components

Legend: **reuse** (link as is) · **adapt** (small changes) · **rewrite**
(new implementation for this app) · **discard** (Apple/desktop only).

| Component (nx2android path) | What it does | Android decision |
|---|---|---|
| `nxnative/dm/android/gfx_vk_*.c/.inc` | NVN replay on Vulkan 1.1/1.3, pipeline cache, copies, descriptors, geometry split, texture upload | **reuse** — this is the port target |
| `nxnative/dm/android/gfx_common.c`, `gfx_null.c` | backend selection / null backend | **reuse** |
| `nxnative/dm/android/front_android.c`, `gfx_vk_present.c` | `ANativeWindow`, swapchain, present | **reuse**; mirrored by this repo's `vulkan_present.cpp` for the shell |
| `nxnative/dm/android/movie_avc.c` | VP9/H.264 movie decode via MediaCodec | **reuse** |
| `nxnative/dm/android/keyboard_android.inc`, `haptics_android.c`, `lan_android.c`, `perf_android.c`, `saves_android.c` | Android system integration | **reuse** |
| `nxnative/dm/android/java/` | Activity + JNI + input bridge | **adapt** — the shell in this repo replaces the launcher |
| `nxnative/dm/host/` | Horizon HLE (kernel objects, IPC services, futex/condition variables, TLS, `svc` handlers) | **reuse** on bionic (already POSIX/Linux; the Apple build is the second port) |
| NSO rewriter (in `nxnative/dm/tools`, used by `porter` and `nx_extract`) | rewrites arm64 NSOs to run in the host process, patches Horizon calls | **reuse** on the PC side; on-device re-implementation is M3 (see §4) |
| `nxnative/dm/gfx/nvn/` (iOS/Metal) | NVN→Metal | **discard** on Android (kept as reference for semantics only) |
| Ryujinx shader translator (`dm/third_party/ryujinx`, MIT) | Maxwell → SPIR-V | **reuse** — SPIR-V feeds Vulkan directly, no SPIRV-Cross/MSL |
| SPIRV-Cross / glslang | SPIR-V→MSL and GLSL→SPIR-V for OpenGL titles | SPIRV-Cross **discard** (MSL), glslang **keep** (OpenGL titles) |
| `dm/third_party` (libvpx, stb_vorbis, deko3d texture layout, Vulkan headers, FLIP) | support libraries | **reuse** (check licences, keep them in `THIRD_PARTY.md`) |
| `porter/` (Python: extraction, profile, APK build, adb install) | host-side player tool | **reuse as the supported path** for producing an APK; **rewrite** the parts needed on-device (§4) |
| `gui/` (Tk desktop app) | player UI on PC | **discard** for Android; the on-device app replaces it |
| `scripts/build_ipa_from_nsp.py`, `packaging/`, XcodeGen, `build_ipa.sh`, `.github/workflows/release-*` | iOS/macOS packaging | **discard** |
| tests (`tests/`, C harnesses, Android fixtures) | validation | **reuse/adapt** — run the native harnesses in CI |

### Runtime model (why native execution is possible at all)

The game's own AArch64 code executes in the app process. The rewriter
patches the imported NSOs (SVC → host call stubs, relocation fixes), the host
implements the Horizon services the game calls (fs, nvdrv, hid, am, …), and
the NVN command stream is replayed on Vulkan. There is no guest CPU and no
JIT, so the Android W^X rules are respected as long as the code is mapped from
a file or anonymous memory with `PROT_READ|PROT_WRITE`, then `mprotect`ed to
`PROT_READ|PROT_EXEC` before the first call (Android 10+ allows this for app
code; SELinux blocks `execmod` only for file-backed writable mappings of
files with an `exec` label, so the host must use anonymous memory or a file in
the app's private data dir).

## 2. Analysis of the game supplied for this task

Executed locally, read-only, with the user's own files (`hl.nsp`, keys zip
extracted outside the repository). No game data or key value is reproduced
in this repository; the probe only reads the PFS0 header and the ticket that
every NSP carries.

| Fact | Value | How it was obtained |
|---|---|---|
| Container | PFS0, 7 entries, 7,335,666,576 bytes | `PFS0` header |
| Title ID | `0100F7E00C70E000` | rights ID of `0100f7e00c70e0000000000000000010.tik` |
| Ticket key generation | **16 (`0x10`)** | last 4 bytes of the rights ID |
| Content | 1 program NCA (7,334,330,368 B), 1 control NCA (203,776 B), 1 meta/CNMT NCA (3,584 B), 2 small NCAs, ticket + cert | NSP table |
| Engine | Unreal Engine 4 (public information) | not verifiable without decrypting the NCA (missing keys, see below) |
| Profile in nx2android | **none** (`nxnative/dm/games/0100F7E00C70E000/` does not exist) | `ls dm/games` |
| Keys supplied (1st set) | `prod.keys` for 15.0.0: `master_key_00..0e`, `titlekek_00..0e`, `key_area_key_application_00..0e`, `header_key` present | key *names* only were listed |
| Keys supplied (2nd set) | `prod2.keys`: 233 keys, generations up to `0x12` — **covers generation 16** | names/generations only |
| Keys required | key generation 16: `titlekek_10`, `key_area_key_application_10`, `master_key_10` | rights ID generation |

**Consequence:** the first key set stopped at generation `0x0E` (14) and could
not open the NCAs. The second set covers generation 16, which unblocked the
port: the experimental pipeline surveyed the title and extracted its NCAs and
RomFS on the PC (see `docs/HOGWARTS_LEGACY_SURVEY.md`). The next blocker is not
keys but the Oodle-compressed Unreal shader archives, which require a physical
Android device to decode with the game's own ARM64 code.

The runtime milestones below are still **not** claimed as done for this
title.

## 3. Architecture decisions for this repository

### D1 — Two tracks, not one

- **Track A (this app, on-device):** terms gate, import (`SAF`), key/NSP
  validation, private-storage copy with progress/cancel/resume, library,
  player shell with a working Vulkan present loop, diagnostics. This is what
  the brief's screens describe and what the CI proves.
- **Track B (upstream runtime):** a Hogwarts Legacy profile for
  `shirai91/nx2android` plus the shared fixes the title needs. This is the
  only realistic route to L2–L5: the runtime is ~190 C sources plus a C#
  shader translator, maintained and tested against real games.

Trying to re-implement Track B inside a fresh repo would produce an
unmaintainable fork that is worse at both jobs. The plan keeps the
boundary explicit and documents what has to be vendored once the app is
ready to run a game (M4).

### D2 — Native validation in C++, not Python

`porter` uses Python + `nsz` + LibHac (.NET) + `capstone`. None of that is
shippable on Android. The app therefore has a small C++ core
(`app/src/main/cpp/`):

- `keys.cpp` — parse `prod.keys`/`title.keys` (`name = hex`), report only
  key *names*; a misspelled/invalid line yields a line number, never a value.
- `nsp.cpp` — PFS0 header, entries, string table, ticket rights ID, title ID,
  key generation, NCA inventory; rejects `.xci`/`.nsz`/`.xcz` with a clear
  message (v1 supports `.nsp` only).
- `import_check.cpp` — cross-checks `header_key`, `master_key_XX`,
  `titlekek_XX`, `key_area_key_application_XX` for the NSP's generation and
  returns a structured result that the UI renders.

AES-XTS/CTR, RomFS/ExeFS and LZ4-NSO handling are deliberately **not**
implemented yet: they are M2/M3 work and cannot even be tested with the
supplied keys. The C++ core is compiled for both Android and the host; the
host build runs in CI as a unit test (`tests/native/test_nsp.cpp`).

### D3 — Vulkan present loop in the shell

`vulkan_present.cpp` creates a `VK_KHR_android_surface` swapchain, clears the
surface and presents at display rate, reporting GPU name/API version/frame
counter. It is not the game's renderer; it proves the presentation path,
driver selection and lifecycle handling on device, and gives the player a
diagnostics screen. The NVN replay backend (`gfx_vk_*`) is the M4 import.

### D4 — Storage

- SAF `OpenDocument` + `takePersistableUriPermission` for the NSP and keys.
- The NSP is copied **once** into `filesDir/games/<titleId>/base.nsp`
  (streamed, resumable via a `.part` file and a recorded offset).
- Keys are copied to `filesDir/keys/` (mode 0600) and never leave the app.
- Free-space check: `1.1 ×` NSP size for the copy, and the plan requires
  `≈ 2 ×` the game size in total for the eventual extraction
  (ExeFS + RomFS + rewritten NSOs), so the UI refuses to start with less.
- Deletion removes the whole `games/<titleId>/` tree and the library entry.

### D5 — Android platform rules

- `minSdk 29`, `targetSdk 35`, `arm64-v8a` only, `largeHeap`, no
  `extractNativeLibs` games data.
- Executable memory: anonymous `mmap` + `mprotect(PROT_EXEC)`; never a
  writable `dlopen` of a file. Documented as M3 acceptance criterion
  (a self-test mapping that patches one `ret` instruction and calls it).
- Services: foreground activity, `FLAG_KEEP_SCREEN_ON`; no background
  execution, no `INTERNET` permission (the app never downloads anything).
- Input: touch overlay (left stick, A/B/X/Y, L/R/ZL/ZR, +/−, home) and
  gamepad through `onKeyDown`/`onGenericMotionEvent` forwarded to a native
  input queue.
- Audio: AAudio low-latency stream (M5), 48 kHz stereo float, underrun
  counter in the diagnostics screen.

## 4. Risks and mitigations

| Risk | Why it matters for this title | Mitigation |
|---|---|---|
| **Keys** | NSP generation 16 vs keys 15.0.0 (max `0x0E`) | app validates and refuses with the exact missing names; nothing downloaded (rule 6). Real port blocked until the user supplies 16.x keys. |
| **GPU: BC textures** | UE4 titles are BC1/BC3/BC4/BC5/BC7-heavy; Mali/PowerVR do not sample BC | nx2android already requires BC-capable GPUs (Adreno/Xclipse). For non-BC devices, plan an offline BC→ASTC/ETC2 transcode pass at import (M3); ETC2 is mandatory in Vulkan, ASTC is optional and Adreno/Mali both expose it. Detection via `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT` for `VK_FORMAT_BC*_UNORM_BLOCK`. |
| **GPU features** | UE4 uses geometry shaders, multiple render targets, sparse-ish large buffers, compute | nx2android's Vulkan backend already implements geometry splitting for Adreno (`gfx_vk_gs_split.c`) and queries limits at startup; add an early capability report to the diagnostics screen. |
| **Memory** | 7.3 GB program NCA, RomFS several GB; Android `lowmemorykiller`, 32-bit VA not an issue on arm64 but RAM is | stream RomFS from private storage instead of loading it; `largeHeap` for Java; native allocations via `mmap`; refuse import below free-space threshold; document 8 GB RAM as the practical floor. |
| **Executable memory / W^X** | NSO rewriter needs RX pages | anonymous mapping + `mprotect`; self-test in M3; verified on Android 10+ (API 29). |
| **Host HLE portability** | Apple-only primitives: `os_unfair_lock`, `mach_absolute_time`, `ucontext` differences, TLS layout | nx2android's `dm/host` is already the Linux/bionic build; keep futex-based waits (`futex(FUTEX_WAIT_PRIVATE)`), `pthread` TLS keys, and `getcontext`-free unwinding. This repo only needs the ABI boundary. |
| **On-device extraction** | Python/.NET/LibHac unavailable on Android | C++ re-implementation: PFS0 → NCA (AES-XTS with `key_area_key_application_XX`, AES-CTR for sections) → RomFS IVFC/HAC → ExeFS → LZ4 NSO. M3; unit-testable against a synthetic container and, later, real files on a PC. |
| **Copy cost** | 7.3 GB NSP must not be copied twice | import copies once (`base.nsp`), extraction reads it in place; delete the NSP only after extraction succeeds (user choice). |
| **Unreal Engine 4 shaders** | HL ships thousands of shader permutations; cold shader translation at first run can take minutes | use nx2android's shader corpus/bundle flow: translate on the PC during the port (Track B) and ship the bundle in the data pack, not on the phone. On-device path would need a progress UI and a persistent cache. |
| **Legal** | app must not ship anything from the game | `.gitignore`, `tools/audit_no_assets.sh`, CI APK scanner, no `INTERNET` permission, no bundled assets. |
| **CI cannot test a device** | APK builds ≠ game runs | the report uses the L0–L5 scale and marks everything below L1 as unverified. |

## 5. Milestones

| # | Deliverable | Done when |
|---|---|---|
| **M0** | Repository, docs, licence, `.gitignore`, audit script, CI skeleton | `tools/audit_no_assets.sh` is clean and the CI workflow exists |
| **M1** | Shell app: terms gate (pt-BR + en), SAF import, key/NSP validation, library, settings, delete, logs | CI builds the APK; native host test passes on a synthetic NSP; a real NSP yields the correct title ID/key generation and a precise missing-key list (verified locally against `hl.nsp`, no game data committed) |
| **M2** | Import hardening: resumable copy, free-space guard, cancellation, NACP-based name/icon from user data | copy resumes after a kill; UI shows name from NACP when the keys allow decryption, filename otherwise |
| **M3** | On-device preparation: NCA/XTS/CTR, RomFS, ExeFS, LZ4 NSO, rewriter port, W^X self-test | preparation of a supported title completes on the device, verified by hash manifest |
| **M4** | Runtime integration: vendor `dm/android`, `dm/host`, Ryujinx SPIR-V translator output; Vulkan device init and NSO load | app reaches the game's first `main()` (L3), then first frame (L4) |
| **M5** | Playability: audio, input mapping, saves, performance options | reaches the game menu (L5) and survives a 30-minute session |

Current state: **M0 done, M1 done in code (CI pending)**. M2–M5 not started;
see `PROGRESS.md` for the evidence level.

## 6. Decisions log

- 2026-11-19 — Base project is `shirai91/nx2android`, not a from-scratch
  port; Track A/B split (D1).
- 2026-11-19 — `.nsp` only in v1; `.nsz`/`.xcz`/`.xci` rejected with an
  explicit message (the porter can decompress them on a PC).
- 2026-11-19 — No `INTERNET` permission; no network code at all.
- 2026-11-19 — Import stops before copying when required keys are missing
  (saves 7.3 GB of pointless I/O).
- 2026-11-19 — Hogwarts Legacy cannot be prepared locally with the supplied
  keys; report the blocker instead of weakening validation.
