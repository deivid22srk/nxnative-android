# Progress

Updated 2026-11-19.

## Level achieved

| Level | Meaning | Status |
|---|---|---|
| **L0** | CI builds the APK | **done** — earlier green run [44563384561](https://github.com/deivid22srk/nxnative-android/actions/runs/44563384561); the new build is re-verified by CI on push. Native tests, debug APK, CI-signed release APK, APK content verification (`arm64-v8a` only, minSdk 29, targetSdk 35, no game/key entries). |
| **L1** | App opens, shows the Terms, imports and validates NSP + keys | implemented and host-tested; run against the user's real `hl.nsp` (title id `0100F7E00C70E000`, generation 16, content keys verified: ticket title key with `titlekek_10`, NCA header `NCA3`, content type 0). **Not exercised on a physical device**: no Android device is attached to this machine. |
| **L2** | Extraction + NSO rewrite completes on the device | **partially implemented, not verified**. The app has an on-device foreground preparation service that extracts the NSP entries (PFS0) with resume/progress and verifies the content keys. It does **not** decrypt NCA sections, extract RomFS/ExeFS or rewrite NSOs, and no run was made on a phone. |
| L3 | Runtime reaches the game's first `main()` | **not verified / not implemented** |
| L4 | First game frame via Vulkan | **not verified**. A standalone Vulkan present loop (clear + present) is implemented in the player shell; it is not the game's renderer. The screen can load a user-supplied Turnip driver through libadrenotools, but no driver was tested here. |
| L5 | Menu / playable | **not verified** |

The honest summary: this repository is a working shell with real
import/validation, a real Vulkan presentation path, content-key decryption
(ticket + NCA header), on-device container extraction with a foreground
service and export, and a Turnip driver picker. It is **not** a running
Hogwarts Legacy port: shader translation and the runtime are still missing,
and the pipeline says so explicitly (the `RUNTIME` stage is `SKIPPED`).

Key coverage is no longer the blocker: `prod2.keys` (233 keys, up to
generation `0x12`) covers this NSP's generation 16, and the native probe
verified the ticket and the program NCA header on the PC.

## Milestones

- [x] **M0** — repository skeleton, docs, MIT licence + attribution,
      `.gitignore`, `tools/audit_no_assets.sh`, CI workflow.
- [x] **M1** — Compose app: mandatory Terms gate (pt-BR + en, versioned in
      DataStore), SAF import of `.nsp` + `prod.keys` (+ optional
      `title.keys`), native key parsing and NSP inspection in C++, missing-key
      report, private-storage copy with progress/cancel, library, settings,
      delete, diagnostics/log export, player shell with Vulkan.
- [x] **M2 (partial)** — foreground build service + notification progress and
      cancel, wake lock, resumable PFS0 extraction driven chunk by chunk,
      content-key verification (AES-128 ECB ticket / XTS NCA header, verified
      against real NCA headers), manifest/report export, libadrenotools +
      Turnip `.zip` import/selection. **Still missing:** NCA section
      decryption, RomFS/ExeFS parsing, NSO rewrite.
- [ ] **M3** — on-device NCA/RomFS/ExeFS/LZ4 extraction and NSO rewrite.
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
  result string. It now also checks AES-128 FIPS-197, ECB/CTR/XTS
  round-trips, and a synthetic encrypted ticket + NCA header.
- 2026-11-19 — real-data content probe (`tests/native/probe_content.cpp`) on
  `hl.nsp` + `prod2.keys`: `titleKeyOk:true` (`titlekek_10`), `headerOk:true`,
  `headerMagic:"NCA3"`, `contentType:0`, title id found in the header. Only
  booleans/derived metadata were printed; no key value or decrypted content.
- 2026-11-19 — on-device build pipeline added: foreground service, notification
  progress/cancel, resumable extraction, report/manifest export, and
  libadrenotools (BSD-2) with Turnip `.zip` import/selection. **Not yet run on
  a phone**; the `RUNTIME` stage is explicitly `SKIPPED`.
- 2026-11-19 — the user ran it on a Motorola Edge 30 Fusion (Android 14):
  6.83 GB extracted in 94 s, content keys verified, Turnip driver imported.
  The exported report exposed a serialisation bug (it was written while
  `REPORT` was still `ACTIVE` and `RUNTIME` still `PENDING`). Fixed: all stages
  are finalised before the JSON is written; `SHADERS` and `RUNTIME` are now
  always `SKIPPED`, and the pipeline status is `CONTAINER_EXTRACTED`, never
  a claim of a playable port.
