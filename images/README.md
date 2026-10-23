# Mac ROM and system disk images

This directory is intentionally git-ignored. Put two files here:

| File | What it is |
| --- | --- |
| `mac.rom` | A 68K Mac ROM image from a machine you own (512 KB or 1 MB) |
| `system.img` | A bootable HFS disk containing System 7.0 through 7.5.5 |

The emulator profile assumes a Quadra-class machine (model ID 14, 68040) by
default because the included setup was tested with a Quadra 950 ROM. If your
ROM is for a different model, override `MAC_MODEL_ID` and `MAC_CPU` when
running; see the Basilisk II documentation for the numeric IDs.

## Install files

```sh
./scripts/setup-images.sh \
  --rom "/path/to/Quadra 950.ROM" \
  --system "/path/to/System7.img"
```

Or, for a quick test system, download the Internet Archive's System 7.5.3
disk image (ROM still required):

```sh
./scripts/setup-images.sh --fetch-system --rom "/path/to/ROM"
```

That downloads `System7_5_3.img` from
<https://archive.org/details/AppleMacintoshSystem753>.

These files are proprietary Apple software. They are not distributed with
this project. Use copies you are legally entitled to use, and do not commit
them to the repository.
