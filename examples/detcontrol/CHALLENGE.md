# Authored collectible challenge (OUT draft)
**Version:** 0.1.0

**Date:** 2026-10-12

**Status:** Active

**Acceptance:** LocalPassed / NativeDeferred

**Owner:** AYEntity maintainers

**Related:** [Engine architecture](../../../../AYDocs/ARCHITECTURE.md)

Status: LocalPassed / NativeDeferred. The single necessary standard Host gate has actually passed; the original OUT drafts and failed Stage66 prototypes remain preserved.

## 搂1 Authored content

The optional challenge keeps the workshop's moving carrier, filled ramp, finite dash, gate and sentry. Collect trigger310 on the upper landing: its real Enter reaches existing ProgressFlow on the following tick, adds ordinary counter1, derives objective1 and disables gate101 through authorized Actions. All of these tables live in `workshop-challenge.dlt`; HP remains in its separate dedicated Progress policy.

## 搂2 Standard Host recipe

```powershell
AYEntity_DetControlExample --scenario challenge --level workshop-challenge.dlt --ticks 128 --require-features --verify-restore --output fresh/live
AYEntity_DetControlExample --scenario challenge --level workshop-challenge.dlt --ticks 128 --require-features --record fresh/challenge.rpl --output fresh/record
```

Same canonical nine-byte input/type9, new application69. The optional app policy401/logic87 explicitly forbids manual door input. Interactive right/left/idle/dash remain available, but `open`/`close` reject before advancing; attempting a nonzero door byte also fails the actual input callback. Original default and workshop policy400/logic86 remain unchanged.

## 搂3 Registered transition

Selected hand checks executed by the actual Host: CP96 is endpoint touch only, CP97 includes a real pending Enter and keeps counter0/claimedfalse/gatemask9/pickupmask1, and tick97 Flow produces CP98 with counter1, objective true, claim stamp97 and both masks0. Pickup-disable is actual Action sequence1; gate-disable is sequence2. Copied Flow records show one claim and one once objective attempt; no direct counter/mask writes or application collection cache are used. Claimed pickup must retain mask0 under the existing pure validator.

## 搂4 Necessary acceptance and evidence limits

The sole optional gate `AYEntity_ChallengeHostExample` uses Live128 plus actual CP97 restore and31-tick suffix, then Record128. Actual complete CP97 bytes, final checkpoint bytes and original per-tick traces compare between these execution paths; restored suffix compares all canonical checkpoints in the Host. These are path comparisons, not independent all-byte goldens or external inventory authentication. Record existence does not claim playback. No six-path matrix, campaign or foreign VM is added.

Artifacts include original `.state`, `.trace`, `.summary.json`, real `.boundary97.state`, and Live `.restore.state`/`.restore.trace`. Hash/content identifiers stay strings; actor HP/Attack observations are actual results. The hand checks assert only the selected motion and collectible transition, not copied future workshop Attack counts. A restored branch rewinds registered authority; collection is once within that branch, not an external persistent ledger.

### §3.1 Final66 content and motion scope

The rebased floor is ordinary Static500, center(.75,-.5), half(2.25,.5), interval[-1.5,3]/top0; actor rows are stable-ID sorted. All original collision flags remain unchanged. The independent dyadic position table ends at CP120. Thereafter the two Kinematic actors both retain Y1.5, separationX=.5 and nondecreasing X against their actual previousPosition. Zero sentry intent does not make it a static wall. The CP97/98 pickup/Flow hand and actual complete restored suffix remain as above. Original failed/frozen prototypes are preserved outside this child.

## §5 Actual necessary local acceptance

Stage69 LocalPassed / NativeDeferred: one actual standard Host gate AYEntity_ChallengeHostExample in 14.3985s. Live128 plus actual CP97 restore31 (159 advances), and Record128. Real collision Enter is queued at CP97; processing tick97 applies collectible-disable Action sequence1, counter1/objective1 and gate-disable sequence2. CP98 retains the pre-Flow Brain sight observation. Final claimed stamp97, counter1, objective true and both masks0 were read from registered authority. Complete actual CP97/final checkpoint/trace compare between Live and Record; 31 complete restored suffix checkpoints compare inside Host. Independent motion bits stop at CP120; thereafter actual two Kinematic actors maintain Y1.5, X separation.5 and nondecreasing previousPosition. HP/Attack use actual observations, not copied future counts. Record existence is not playback or authentication; no new numeric/backend matrix, runtime profile, native VM or long campaign. Evidence: D:\Aliyat\AliyatEngine\out\detcontent36-40-20261011\stage69-challenge1\result.json.
