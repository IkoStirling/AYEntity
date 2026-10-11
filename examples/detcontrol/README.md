# Authored deterministic Character Control Host example

**Version:** 0.1.0
**Date:** 2026-10-12
**Status:** Active
**Owner:** AYEntity deterministic content maintainers
**Related:** [Control header](../../include/AYEntity/DeterministicCharacterControl2D.h) · [Level header](../../include/AYEntity/DeterministicLevel2D.h) · [Module design](../../design.md)

## §1 Build and acquisition

Enable default-OFF `AY_ENTITY_BUILD_DET_CONTROL_EXAMPLE=ON` in a supported standard
engine configuration and build `AYEntity_DetControlExample`. The example uses real
EngineModuleRuntime, Entity runtime, standard Host and Scene, not a portable
Unitfixture or separate tick loop. Load immutable [Level10](content.dlt), derive
session config and configure its public pipeline. Level installs matching
Character/HP/Attack/Brain/Control; resolve exact getters rather than duplicate
facades. Brain and Control owner intersection is rejected at acquisition.
Necessary actual Level10/standard Host/CLI runtime LocalPassed; final native VM
remains Deferred. Installation/OUT freeze/static initial seal alone is not runtime proof.

## §2 Input and presentation authority

App56/inputVersion7 at step1/64, application logic81/inputsystem4 owns Hero10.
Packet type7 is exactly eight bytes: speed+2, jump, crouch, drop, HeroAttackHeld,
HeroFacingRight, dashHeld, cancel. Enemy20 inputs remain Brain3 decisions; Hero
movement samples use Control15 before Character pre20. Do not send Manual or
Enemy-held samples, and do not bypass Control with direct Character writes.
External console/script acquisition occurs before Host input callback; it only
encodes already acquired values. Sim callbacks capture immutable policies only.
Committed getters provide presentation/status, with no pose, HP or tick writer.

Finite dash is a target-speed override with locked facing and explicit duration/
cooldown. Actual acceleration, gravity, normal jump, carrier and walls remain
Character policy; active request does not imply successful velocity/displacement.
groundOnly uses previous ground; held requires explicit release. Rejected busy/
cooldown/airborne/cancel edges are consumed, no retry. Cancellation retains the
original ready deadline; HP/death is not automatically bound to this facade.

## §3 CLI and file boundaries

From repository root, with an existing fresh-output parent and built executable:

```text
AYEntity_DetControlExample --level AYRuntime/AYEntity/examples/detcontrol/content.dlt --ticks 64 --require-features --output OUT/live --save-at 4 --save OUT/boundary4.dsv
AYEntity_DetControlExample --level AYRuntime/AYEntity/examples/detcontrol/content.dlt --ticks 64 --load OUT/boundary4.dsv --output OUT/load
AYEntity_DetControlExample --level AYRuntime/AYEntity/examples/detcontrol/content.dlt --ticks 64 --record OUT/control.rpl --output OUT/record
AYEntity_DetControlExample --level AYRuntime/AYEntity/examples/detcontrol/content.dlt --ticks 64 --replay ACTUAL_RECORDED_FILE --seek 4 --output OUT/seek
AYEntity_DetControlExample --level AYRuntime/AYEntity/examples/detcontrol/content.dlt --ticks 64 --interactive --output OUT/interactive
```

`OUT` is an explicitly chosen ordinary directory; every output/save/record target
must be fresh. Recorder may produce a numbered physical file: use actual summary
`recordingPath`, not a guessed basename. Prefix emits complete `.state`, per-tick
hash `.trace` and `.summary.json`. Hash is diagnostic, complete actual bytes are
the comparison evidence. Current Windows path recipe uses printable ASCII.

Console commands: right/left/idle [ticks], jump, drop, crouch/stand [ticks],
attackright/attackleft, dashright/dashleft, canceldash, faceleft/faceright, status,
save FRESH.dsv, load FILE.dsv, help, quit. Movement/facing persist; console attack/
jump/drop/dash/cancel requests explicitly release on following tick. File operations
are outside Sim. Live load uses public save decode then actual Host restore/resume
and reacquires full checkpoint; Record cannot rewind, Replay does not acquire Live
input. A failed restore is a real failure, not assumed compatible continuation.

## §4 Finite evidence scope

Necessary local MSVC Release7 unique cases passed: LevelCharacterControl10Checks
0.223617s, old9 1.17518s/old7 .654778s/Cooked .116577s, standard ControlHost
13.2032s, CLI1.37562s and existing Resource1.00207s. New Level10 actual16ticks/
17CP,16selected hand rows/46advances/33CP comparisons/25authoring rejects,
real file/text/cooked roundtrip and active restore. Optional Respawn checks initial
seal/getter/restore binding only, not a new Accepted-motion exercise.
Actual64tick active-save4 six-path full state/trace/suffix/save equality passed;
load/seek60rows, stdin68rows with suffix8 of60, Replay0input calls. Control has
4starts/11requests/7denials/1cancel, ends Active/ready73; HeroHP96/hurts2/attacks2/
attempts2 and EnemyHP94/alive/hurts2/attacks2/attempts2/cancel0/64decisions2pulses.
FinalHash5877846431418660777/content15814538279326814389 are diagnostics, not
independent state goldens or authentication. Existing Resource27CP/14negatives
uses old samples, not new10 combat Pak execution. Selected hand fields and whole
actual CP execution equality are separate scopes; final native VM deferred.
No new matrix/Debug/ASan/long/native run or numeric/Character/Collision/checkpoint/
save/replay wire change is claimed; explicit Level profile10 is separate.

Receipts stage56-levels2/levels3/host2 and stage56-actual-freeze.json bind
per-run unchanged sources. levels2 remains overall Failed, with three genuine old
case passes retained without rerun; only new10 was retried. First new fixture
C2677/C2661 came from quoted-helper ADL (renamed jsonQuote); later CRLF source1007
was wrongly equated with canonical LF980, corrected using independent previously
accepted Stage54 canonical bytes/FNV3155852323544543593 while retaining original
source/content fingerprints. Production behavior/hand oracle unchanged. First
standard build exit1 had no compiler diagnostic; cause remains unconfirmed.
All three failed receipts remain. The new test changed across batches, so whole
source snapshots are not claimed identical between them. Stage55 standalone
core evidence remains separate historical scope.

Actual Host artifacts: `out/detcharacter23-20261011/standard-release/control-host-example/
run-20261011T213452-552953f8baca`. Six final complete checkpoint files are57568bytes
and byte-equal; the two actual save4 files are57584bytes and byte-equal. The real
stdin stream has68rows, including4revisited ticks after load; suffix index8 matches
60baseline rows. Live/Record/Replay full64rows match, fresh load/seek60rows match.
Summary hashes are diagnostic indexes, not the equality proof or authentication.
Default hand checks passed actual wall clipping, ordinary jump, consumed denied
edges, facing lock, cancel retainingready33 and finalactive ready73. No new Level10
Accepted-motion/recovery combination, actual new content Pak, long/native matrix
or independent whole-CP golden is claimed. Ten-hour content task remains Active.
