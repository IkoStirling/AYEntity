# Fixed Windows replay archive fixture

Generated once on Windows x64 MSVC Release using production AYReplay/AYIO adapters:

```text
AYEntity_ReplayArchiveChecks --generate NEW_DIRECTORY
AYEntity_ReplayArchiveChecks --check FIXTURE_DIRECTORY CRASH_MARKER
```

CTest runs `AYEntity_ReplayCrashWriter --interrupt CRASH_MARKER` first. That separate
process writes a different live archive and exits via `_Exit`, bypassing destructors.
The check reads the marker to recover only its independently sealed prefix.

`sample.rpi` is normal profile1; its five schema2 `.rpl` files record 128 ticks with
collision enabled, typed software Float32 state, owned input and RNG state. A trusted
epoch2 transition occurs at tick64. File boundaries end at32,64,95,127,128. Exact
checkpoint bytes are `expected.state`; ordered confirmed event identities and
payload bytes are `expected.events`. Neither oracle is calculated from the reader
being checked. All native CI platforms read these same checked-in bytes.

The harness validates full playback, silent seek across files/epochs, independently
readable segments, conservative recovery, malformed metadata and source preservation.
Do not regenerate this fixture on each CI platform; a deliberate format/profile
change requires a separately reviewed fixture update.
