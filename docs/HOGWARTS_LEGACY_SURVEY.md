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

`prod.keys` in the supplied archive is a 15.0.0 set: `master_key_00..0e`,
`titlekek_00..0e`, `key_area_key_application_00..0e`, `header_key`.
Key generation 16 needs `titlekek_10`, `key_area_key_application_10` and
`master_key_10`, none of which are present.

Therefore:

- the ticket title key cannot be decrypted;
- the program NCA cannot be opened;
- NSO rewriting, shader survey and any local run are impossible **with these
  files**.

Nothing was downloaded to work around this. The app's validator detects this
case before copying the 7.3 GB file and reports only the missing *names*.

## What the porter would do next (once 16.x keys exist)

1. `python -m porter inspect <folder>` — confirms the family (base only).
2. `python -m porter port <folder> --experimental -k prod.keys` — surveys the
   title, writes a provisional profile from `dm/games/profile_guess.py`,
   extracts ExeFS/RomFS with `nx_extract`, rewrites the NSOs and translates
   the shaders. An unsupported UE4 title is the hardest class of port: the
   survey must classify the engine, the shader corpus is large, and the
   `android.tsv` profile needs the right `cpu`/`gpu`/`env` values.
3. Run on a BC-capable device (Adreno/Xclipse), collect logs, iterate on the
   shared Vulkan/HLE fixes — exactly the process documented for NieR:Automata
   in `docs/NIER_AUTOMATA_ANDROID_PORT.md` of the upstream repository.

Until step 3 produces frames, this title's status is **not supported**.
