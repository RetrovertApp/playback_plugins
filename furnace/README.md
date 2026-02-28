# Furnace Playback Plugin

Plays Furnace (.fur), Deflemask (.dmf), and FamiTracker (.ftm) chiptune files
using the [Furnace](https://github.com/tildearrow/furnace) tracker engine by
[tildearrow](https://github.com/tildearrow). Furnace emulates 50+ sound chips
covering most classic gaming and computer platforms.

## Supported Formats

| Extension | Format |
|-----------|--------|
| `.fur` | Furnace native |
| `.dmf` | Deflemask |
| `.ftm` | FamiTracker |
| `.0cc` | 0CC-FamiTracker |
| `.dnm` | DefleMask Preset |
| `.eft` | Effect Plugin |
| `.tfm` | TFM Music Maker |
| `.tfe` | TFM Effect |

## Library Sources

| Library | Source | Version/Commit |
|---------|--------|----------------|
| furnace | https://github.com/tildearrow/furnace | v0.6.8.3 |
| adpcm | https://github.com/superctr/adpcm | ef7a217154badc3b99978ac481b268c8aab67bd8 |
| fmt | https://github.com/fmtlib/fmt | e57ca2e3685b160617d3d95fcd9e789c4e06ca88 |

The adpcm and fmt libraries are git submodules in the Furnace repository that
are not included in release tarballs. They are downloaded separately and
integrated during the build.

## Patches

- `engine-cpp.patch` - Comments out `initConfDir()` in engine.cpp to prevent
  Furnace from creating config directories on disk.
- `engine-h.patch` - Moves `playing`, `freelance`, and `endOfSong` fields from
  private to public in engine.h so the plugin can check playback state.
- `config-cpp.patch` - Comments out four `reportError()` calls in config.cpp.
  These call a GUI function defined in Furnace's main.cpp which we don't compile.
- `log-cpp.patch` - Disables log file writing in log.cpp. The plugin uses the
  host logging system instead.
- `adpcm-xq-c.patch` - Renames `main()` to `disabled_main()` in
  `extern/adpcm-xq-s/adpcm-xq.c` so it can be compiled as a library.

## Extension Conflict with OpenMPT

OpenMPT also handles `.dmf` (X-Tracker) and `.ftm` (Face The Music).
The probe system disambiguates via magic bytes:
- Deflemask `.dmf` starts with `.DelekDefleMask.`
- FamiTracker `.ftm` starts with `FamiTracker Module`
- Furnace `.fur` starts with `-Furnace module-`

## License

Furnace is licensed under GPL-2.0-or-later. See [LICENSE](LICENSE).
