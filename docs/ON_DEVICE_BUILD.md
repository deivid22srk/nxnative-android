# On-device preparation, export and Turnip drivers

This document describes exactly what the Android app does on the phone today,
what it does not do, and how to read the exported bundle. It is intentionally
blunt: a stage is marked `DONE` only when its work really happened.

## Pipeline (library → "Preparar")

The build runs in a **foreground service** (`BuildService`,
`foregroundServiceType="dataSync"`) so it survives the app going to the
background. The notification shows the current stage and percentage and has a
**Cancel** action; a partial wake lock keeps the CPU running. The UI observes
the same state through `BuildManager` (a `StateFlow`), so closing and reopening
the screen does not lose the progress.

| Stage | What really happens | Where |
|---|---|---|
| `VERIFY` | PFS0 header/table, ticket rights ID → title ID + key generation, key coverage, free-space check | `nsp.cpp`, `import_check.cpp` |
| `KEYS` | `titlekek_<gen>` decrypts the ticket title-key block (AES-128-ECB); `header_key` decrypts the program NCA header (AES-128-XTS, big-endian sector units) and the magic is checked (`NCA3`) | `crypto.cpp`, `content_check.cpp` |
| `EXTRACT` | every entry is copied out of the NSP into `files/build/<titleId>/extracted/`, in 16 MB chunks, resumable and cancellable, with a manifest | `PortPipeline.kt` + `Native.copyRange` |
| `REPORT` | `build-report.json` with device, container, key-check results, stage states and limitations | `PortPipeline.kt` |
| `RUNTIME` | **`SKIPPED`** — shader decode, SPIR-V translation and the nx2android host are not embedded in this build | — |

The `RUNTIME` row is never reported as done. `build-report.json` carries
`"runtimeIncluded": false` and a `limitations` array for the same reason.

## What is still missing for a playable port

1. **NCA section decryption** and IVFC/RomFS/ExeFS parsing. The app verifies
   the key material and copies the container, but does not yet expose the
   RomFS tree.
2. **Oodle shader decode.** This one is *easier* on the phone than on a PC,
   because the app itself is an ARM64 Android process: the game's own
   `OodleLZ_Decompress` can run in-process. It still needs the module loader
   and HLE job runner from nx2android (`dm/host/hle_oodle.c`, `DM_OODLE_JOB`).
3. **SPIR-V translation.** nx2android's translator is a .NET/Ryujinx component
   (`dmsh_xlate`); embedding it means porting/hosting that toolchain for
   Android. This is the largest missing piece.
4. **Host + profile.** The rewritten NSOs, `libdmhost.so` and the game profile
   are what actually execute the game. None of that is in this APK.

So "Preparar" means *validate + verify keys + extract + report*, not "the game
is now playable". The UI and the exported report say the same thing.

## Export bundle

**Exportar pacote** writes a small `.zip` (system file picker, no storage
permission) containing:

- `build-report.json` — schema 1: title ID, device, container, key-check
  booleans, stage list, limitations;
- `manifest.tsv` — `name`, `kind`, `size`, absolute `offset` of every extracted
  entry;
- `build.log` — human-readable timings and errors;
- `diagnostics.txt` — app log plus device/ABI/native version.

The extracted game data (many GB) is **not** included: it stays in the app's
private storage, and the report states that. No key value and no decrypted
content is ever written to these files.

## Turnip driver packs

Settings → **Driver Vulkan (Turnip)**:

- import a `.zip` with a driver library (`vulkan*.so`, `libvulkan_*.so`); the
  app rejects traversal, oversized entries and packs without a driver;
- the driver is stored under `files/drivers/<id>/` (never shared storage:
  libadrenotools refuses to `dlopen` a driver from there);
- select it, or **Sistema (padrão)**;
- the player screen passes the directory/soname to `vkApiOpen`, which calls
  `adrenotools_open_libvulkan` with the hooks in `nativeLibraryDir`. The
  reported `customDriver` flag shows whether the driver path was used.

The app bundles **no driver**. Turnip must match the Adreno generation; an
incompatible driver fails to create a Vulkan 1.1 instance/device and the player
shows the error. Switch back to the system driver to recover.

## Useful commands (PC, no game data committed)

```bash
# host unit test: AES vectors, synthetic NSP/ticket/NCA header, JSON hygiene
bash tools/test_native.sh

# against the user's own files (never in CI, never prints key values)
/tmp/nx2a-native-test/probe_content /path/to/game.nsp /path/to/prod.keys
```