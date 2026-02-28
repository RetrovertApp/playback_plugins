# xSF Playback Plugin

Unified plugin for Portable Sound Format (PSF) files. Dispatches to the
correct emulator based on the PSF version byte in the file header.

## Supported Formats

| Format | PSF Version | Platform | Emulator Library |
|--------|-------------|----------|------------------|
| PSF/PSF2 | 0x01, 0x02 | PlayStation 1/2 | highly_experimental |
| 2SF | 0x24 | Nintendo DS | vio2sf |
| USF | 0x21 | Nintendo 64 | lazyusf2 |
| GSF | 0x22 | Game Boy Advance | viogsf |
| SNSF | 0x23 | Super Nintendo | snsf9x |
| SSF/DSF | 0x11, 0x12 | Sega Saturn/Dreamcast | highly_theoretical |
| QSF | 0x41 | Capcom QSound | highly_quixotic |

## Library Sources

| Library | Source | Commit |
|---------|--------|--------|
| psflib | https://gitlab.com/kode54/psflib | 3bea757c8b45c5e68da1b5a7b736ad960a06a124 |
| highly_experimental | https://gitlab.com/kode54/highly_experimental | 0fa96d186e3c0437951732d0c50bf1da4e32970e |
| vio2sf | https://gitlab.com/kode54/vio2sf | 1d68801f5fd370c3275affd51505353e2b366a7e |
| lazyusf2 | https://gitlab.com/kode54/lazyusf2 | 421f00bcaa1988b8e1825e91780129f24fbd1aa0 |
| highly_theoretical | https://gitlab.com/kode54/highly_theoretical | 0e4c18c5b757b04dbcb68c572c5a4f6fd803283c |
| highly_quixotic | https://gitlab.com/kode54/highly_quixotic | d730174d436e8cabca90fabdb3ad4ddf614d31cc |
| viogsf | https://github.com/kode54/viogsf | 6c43a9926a6a85fbb736ea8f5f7f6c4f59ed3d64 |
| snsf9x | https://github.com/loveemu/snsf9x | 128cb0d500c98caa815da117eb0339873cf157ae |

Most libraries by [kode54](https://gitlab.com/kode54). snsf9x by
[loveemu](https://github.com/loveemu). psflib is the common PSF container
parser used by all emulator backends.

## Patches

- `highly_theoretical-arm64-fastcall.patch` - Guards x86 `regparm` calling
  convention behind architecture check so the library compiles on ARM64.
- `viogsf-arm64-regparm.patch` - Guards x86 `regparm(2)` calling convention
  behind architecture check so viogsf compiles on ARM64.
