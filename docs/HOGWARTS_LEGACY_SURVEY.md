# Survey — Hogwarts Legacy (Switch), user-supplied copy

Read-only probe of the user's own `hl.nsp` (never copied into this
repository). Only the container header and the ticket are read; **no key
value is printed or stored**.

## Container

- PFS0, 7 entries, file size 7,335,666,576 bytes (6.83 GiB).
- Entries:

| Entry | Size (bytes) | Kind |
|---|---:|---|
| `ee8fed561b6ace5cc38c13699a3e9664.nca` | 7,334,330,368 | program |
| `7a6e38968fa396619e25cb0d1a28493b.nca` | 1,011,200 | meta (CNMT) |
| `862e6c9c619ec16f464b187eb4a08903.nca` | 203,776 | control (NACP/icon) |
| `bb6dbcb7878d118c895cc99e63e69a08.nca` | 114,688 | legalinfo / manual |
| `4583fc70a972141ddb656311c780935a.cnmt.nca` | 3,584 | content meta |
| `0100f7e00c70e00000000000000000010.tik` | 704 | ticket |
| `0100f7e00c70e00000000000000000010.cert` | 1,792 | certificate |

The exact entry sizes are recorded because they are the only numbers the
app needs for its inventory and progress bars; they contain no game content.

## Identity

- Rights ID: `0100F7E00C70E0000000000000000010`
- Title ID (base game): **`0100F7E00C70E000`**
- Key generation: **16 (`0x10`)** — i.e. firmware 16.0.0 or newer keys.
- Title version: not readable without decrypting the CNMT NCA. The dump has
  no update NSP alongside it (only one program NCA, no `...0800` ticket), so
  this is the base game, version 0 unless the container was rebuilt.
- Engine: Unreal Engine 4 (public product information; cannot be confirmed
  from the encrypted NCA with the supplied keys).

## Keys

The first archive supplied with the task was a 15.0.0 set (`master_key_00..0e`,
`titlekek_00..0e`, `key_area_key_application_00..0e`), which cannot open a
generation-16 NSP. A second set (`prod2.keys`, 233 keys, generations up to
`0x12`) **does** cover generation 16: the app's validator returns
`missingKeys: []`, `supported: true` for this container. Key values were never
printed, logged or committed.

## Verified survey (2026-11-19, PC side)

`python -m porter port <folder> --experimental` ran with the second key set:

- Display version **1.0.0**, SDK **16.2.3**.
- Engine and API: **Unreal Engine 4**, **NVN**, Wwise.
- Modules: `main` (93.5 MB text, 7.7 MB rodata, 21.1 MB data, 32.3 MB bss,
  build id `70658a5c238d69c24698fded6a34efa6`), `subsdk0`
  (NintendoSDK_movie), `sdk` (nnSdk), `rtld`.
- RomFS: **185 files, 6.75 GB**, 174 under `Phoenix/Content` (`.ucas`/`.utoc`
  IoStore + `.pak`, 134 `.bk2` movies, 9 PNG).
- Rewrite census: `main` 24,503,320 instructions decoded; 48,962 `x18` uses,
  328 `mrs tpidr_el0`; imports 825 `nn::*`/libc functions that the reference
  title does not import.
- Extraction: 190 files / 6.8 GB into the build folder.
- Provisional profile generated; `android.tsv` has `status experimental`.

**Where it stopped:** the title keeps its shaders in Oodle-compressed Unreal
shader code archives, which only the game's own ARM64 decoder can read, and
the Android porter runs that decoder on a physical device over adb. No device
was attached, so the pipeline aborted before NSO rewriting, shader
translation, host build and APK assembly. See
[`HOGWARTS_LEGACY_PORT_RUNBOOK.md`](HOGWARTS_LEGACY_PORT_RUNBOOK.md).

## What the porter would do next (with a device)

1. Decode the 9 shader archives on the device with `DM_OODLE_JOB`.
2. Rewrite the NSOs for `DM_TARGET=android`, translate the shader corpus to
   SPIR-V, build the Vulkan shader bundle and `libdmhost.so`, assemble the APK
   and data pack.
3. Install and bring it up, iterating on the shared NVN/Vulkan and host code —
   the process documented for NieR:Automata in the upstream repository.

Until a device produces frames, this title's status is **not supported**.
