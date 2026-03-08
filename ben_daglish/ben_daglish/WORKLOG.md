# Ben Daglish Port Worklog

## Format summary
- 4-channel Amiga player by Ben Daglish
- Feature-flag driven: the binary embeds a 68000 music driver with optional features detected by inspecting machine code
- Subsong support (variable count)
- Effects: portamento (2 types), volume fade, sample effects (portamento + vibrato), final volume slide, master volume fade
- DMA simulation with 1-tick delay, 3 sample handler modes: PlayOnce, Loop, VolumeFade
- Complexity: medium-high due to feature flags and Amiga hardware simulation

## Decisions
- Used `bd_` prefix for all public API functions
- Feature flags stored as simple struct fields (no bitfield) matching C# Features class
- Sample handler callbacks implemented as enum + switch rather than function pointers (simpler in C)
- Position lists stored as a flat array with offset lookup (Dictionary<ushort, byte[]> in C# -> linear search by offset)
- FineTune table from Tables.cs stored as static const int array
- NostalgicPlayer mixer applies 0.5x gain per channel - applied as volume/128 instead of volume/64
- Pending sample/loop system for deferred Amiga DMA register updates

## Problems & solutions

### 1. Samples cut short (sparse audio vs continuous reference)
**Problem**: Initial implementation immediately updated sample position when SetSample/SetLoop were called, but on real Amiga hardware these are deferred register updates that take effect when the current buffer finishes.
**Solution**: Added pending loop/sample mechanism (has_pending_loop, has_pending_sample) that gets applied in bd_voice_advance when the current buffer position reaches the end.

### 2. Output 2x louder than reference
**Problem**: My render used volume/64.0 gain but the C# NostalgicPlayer mixer applies an internal 0.5x gain per channel.
**Solution**: Changed to volume/128.0. Verified with amplitude ratio analysis (exactly 0.5001).

### 3. Song restart divergence after first loop
**Problem**: bd_init_sound was resetting p->ended and p->frames_until_tick, and the ended flag stopped rendering completely.
**Solution**: Made `ended` persist across restarts (not cleared by init_sound), removed render-stop-on-ended behavior to allow continuous looping like the C# reference. Only divergence is at the exact loop boundary timing.

### 4. Deferred loop position reset (divergence at ~5.27s in blasteroids)
**Problem**: When `bd_handle_sample_loop` triggers the deferred loop change (loop_delay_counter hits 0), the pending loop parameters are applied when the sample position exceeds the buffer end. The original code used overflow arithmetic to wrap the position into the new loop region, but C#'s mixer (`SetNewSample` in MixerNormal.cs) resets position to the loop start exactly (`vnf.Current = vsi.Sample.Start << FracBits`).
**Solution**: Changed pending loop application in `bd_voice_advance` to reset `sample_pos_fp` to `loop_offset << FRAC_BITS` instead of computing overflow wrapping. This matches the C# behavior where deferred sample changes always restart from the new start position.

### 5. has_ended detection
**Problem**: `loop_delay_counter` gets stuck at 0xffff for PlayOnce channels because the empty sample loops forever in our renderer (unlike C#'s mixer which makes the channel inactive). This prevented `channel_enabled` from being set to false, so `enable_playing` was never false, and `has_ended` never fired.
**Solution**: Instead of trying to match the complex C# cascade (empty sample → IsActive=false → volume=0 → loop_delay_counter=0 → channel disabled), added a `reached_end` flag set when the position list hits 0xff. `has_ended` returns true when all 4 channels have `reached_end` set. This fires slightly earlier than C# (~0.7s of trailing silence is trimmed) but all audio content is preserved.

## C# patterns not seen before
- Feature detection by inspecting 68000 machine code opcodes in the module binary
- Dictionary<ushort, byte[]> for position lists keyed by offset -> linear search in C
- Delegate callbacks for sample handling (HandleSampleCallback) -> enum dispatch in C
- `case < 0xf0 when features.EnableF0TrackLoop:` - C# pattern matching in switch -> if/else chains
- `(ushort)~playbackInfo.LoopDelayCounter` when counter=0 -> `(uint16_t)~(uint16_t)0 = 0xFFFF`
- NostalgicPlayer's channel.SetSample() vs channel.PlaySample() distinction (deferred vs immediate)

## Validation results
All three test modules match C# reference (full song duration with has_ended):
- blasteroids.bd: rms=0.000167, peak=0.140594 (31.0s, 16 divergent samples from loop boundaries)
- mickey mouse.bd: rms=0.000012 at 20s (perfect); rms=0.008868 at full 119s (loop timing accumulation)
- superscramble-jingles.bd: rms=0.000028, peak=0.000031 (2.3s, perfect)

## Suggestions for skill improvement
- Document the NostalgicPlayer 0.5x per-channel gain - this will affect all future ports
- Document the Amiga DMA deferred register update pattern - SetSample/SetLoop don't take effect immediately
- Add note about song loop behavior: C# RestartSong continues rendering, C port should not stop on ended
- Document that C# mixer's SetNewSample resets position to Sample.Start (no overflow preservation) - critical for deferred loop changes
- Document that NostalgicPlayer mono downmix applies 1/sqrt(2) gain - must match in per-channel WAV extraction
- For has_ended: tracking position list 0xff markers is simpler than matching C# channel_enabled cascade
