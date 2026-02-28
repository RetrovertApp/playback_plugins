# Klystrack Playback Plugin

Plays Klystrack music files (.kt) using the klystron sound engine
via the KSND wrapper API. Klystrack is a chiptune tracker by kometbomb.

## Supported Formats

| Format | Extension | Description |
|--------|-----------|-------------|
| Klystrack | .kt | Klystrack song file (no known magic bytes, extension-based probe) |

## Library Source

| Library | Source | Commit |
|---------|--------|--------|
| klystron | https://github.com/kometbomb/klystron | 989fafc4fffb1bb881ab677fe52eb34527e08129 |

klystron is the sound engine extracted from the
[klystrack](https://github.com/kometbomb/klystrack) tracker.

## SDL Shim

klystron depends on SDL for types and audio output. Since this plugin
uses the KSND unregistered player API (no SDL audio callbacks), a
minimal SDL shim is provided in `sdl_shim/` that supplies type definitions
and stub functions. No actual SDL library is linked.

## License

See [LICENSE](LICENSE) (MIT License, from the parent klystrack project).

The klystron repository itself does not contain a LICENSE file, but the
parent klystrack project by the same author (kometbomb) is MIT-licensed.
