# Progress

Updated 2026-11-19.

## Level achieved

| Level | Meaning | Status |
|---|---|---|
| **L0** | CI builds the APK | **done** — run [44558132809](https://github.com/deivid22srk/nxnative-android/actions/runs/44558132809): native tests, debug APK, CI-signed release APK, APK content verification. Downloaded and checked locally: `arm64-v8a` only, minSdk 29, targetSdk 35, no game/key entries. |
| **L1** | App opens, shows the Terms, imports and validates NSP + keys | implemented; the validation core is verified by the host test in CI and was run against the user's real `hl.nsp` (title id `0100F7E00C70E000`, generation 16, three missing keys named). **Not exercised on a physical device**: no Android device is attached to this machine. |
| L2 | Extraction + NSO rewrite completes on the device | **not verified / not implemented** |
| L3 | Runtime reaches the game's first `main()` | **not verified / not implemented** |
| L4 | First game frame via Vulkan | **not verified**. A standalone Vulkan present loop (clear + present) is implemented in the player shell; it is not the game's renderer. |
| L5 | Menu / playable | **not verified** |

The honest summary: this repository is a working shell with real
import/validation and a real Vulkan presentation path, plus the plan and the
survey needed to continue. It is **not** a running Hogwarts Legacy port, and
the supplied key set (15.0.0, generation `0x0E`) cannot open this game's
NCAs (generation `0x10`).

## Milestones

- [x] **M0** — repository skeleton, docs, MIT licence + attribution,
      `.gitignore`, `tools/audit_no_assets.sh`, CI workflow.
- [x] **M1** — Compose app: mandatory Terms gate (pt-BR + en, versioned in
      DataStore), SAF import of `.nsp` + `prod.keys` (+ optional
      `title.keys`), native key parsing and NSP inspection in C++, missing-key
      report, private-storage copy with progress/cancel, library, settings,
      delete, diagnostics/log export, player shell with Vulkan.
- [ ] **M2** — resumable copy across process death; NACP name/icon; update/DLC
      layering.
- [ ] **M3** — on-device NCA/RomFS/ExeFS/LZ4 extraction and NSO rewrite
      (needs a supported title and keys that cover it).
- [ ] **M4** — vendor the nx2android runtime (`dm/android`, `dm/host`,
      shader translator products) and load the rewritten NSOs.
- [ ] **M5** — audio/input/saves/performance.

## Evidence log

- 2026-11-19 — `hl.nsp` probed: PFS0, 7 entries, title `0100F7E00C70E000`,
  ticket key generation `0x10`; supplied `prod.keys` covers `0x00..0x0E`.
  Reported to the user as a blocking condition instead of bypassing
  validation. No game data or key value left the machine.
- 2026-11-19 — CI run 44558132809 green: `app-debug.apk` (25.7 MB) and
  CI-signed `app-release.apk` (20.9 MB) uploaded; `tools/verify_apk.sh`
  confirmed `arm64-v8a` only and no `.nsp`/`.keys`/`.nca` entries.
- 2026-11-19 — a second key set (`prod2.keys`, 233 keys, generations up to
  `0x12`) covers the NSP's generation 16. `porter doctor` passed and the
  experimental port ran on the PC: survey (Unreal Engine 4, NVN, version 1.0.0,
  SDK 16.2.3), NCA/RomFS extraction (190 files, 6.8 GB), provisional profile.
  It stopped at the Oodle-compressed Unreal shader archives, which the porter
  decodes with the game's own ARM64 code **on an Android device** over adb;
  no device is attached. L2 remains unverified (the work was PC-side).
- 2026-11-19 — host test (`tests/native/test_nsp.cpp`) builds a synthetic
  PFS0 in memory with a generation-`0x0A` ticket and checks: title ID, key
  generation, missing-key list, and that a key value never appears in the
  result string.
