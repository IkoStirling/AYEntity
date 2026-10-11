#pragma once
// design reference: AYEntity/design.md Stage51 finite registered attack windows.
#include <AYEntity/DeterministicVitality2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetAttack2DProfileVersion=1;
inline constexpr std::size_t kDetAttack2DMaxOwners=8,kDetAttack2DMaxTargets=8,
    kDetAttack2DMaxPolicyBytes=16384;
enum class DetAttackPhase2D : std::uint32_t {Idle=0,Warmup=1,Active=2,Recovery=3};
enum class DetAttackCancel2D : std::uint32_t {None=0,Dead=1,Recovered=2,Retired=3};
/// @brief One literal Hurt permission, target and amount, sorted by target actor ID.
/// @note Targets are bound Vitality actors, never arbitrary query results. Normal
/// HP denials consume this attack's target bit exactly like an Applied result.
struct DetAttackTarget2D {SimEntityId actor=0;std::uint32_t permission=0;std::uint64_t amount=1;};
/// @brief Finite attack shape, tick windows and target whitelist for one HP actor.
/// @note The world XY center is pose.xy + offset. Left-facing negates only offset.x;
/// there is no rotation or body-offset addition. Strict positive-area overlap,
/// reciprocal current body layer/mask, and the whitelist select candidate targets.
struct DetAttackOwner2D {
    SimEntityId actor=0;
    math::DetVec2 halfExtent{math::DetFloat32::fromInt(1),math::DetFloat32::fromInt(1)},offset{};
    std::uint32_t warmupTicks=0,activeTicks=1,recoveryTicks=0;
    std::vector<DetAttackTarget2D> targets;
};
/// @brief Persistent held input; an explicit false releases the registered latch.
struct DetAttackInput2D {bool held=false,facingRight=true;};
/** @brief Fixed finite melee policy over separately installed matching Vitality.
 * @note Install collision/Actions/Progress/Vitality first, then Attack, before
 * actors/seal; check false and discard a partial setup. Owners and per-owner
 * targets have strictly increasing nonzero actor IDs, at most8 each. The existing
 * shared Vitality limit of8 unique HP actors also applies, so a single owner can
 * currently name at most7 other actors. Owner/target
 * HP actors must exist at seal and be ordinary solid Kinematic bodies. Each Hurt
 * permission must name its literal target and allow the fixed positive amount.
 * Warmup/recovery are0..65535; active is1..65535. The full nested Vitality policy,
 * IDs, float bits, windows and target table enter literal typed policy defaults,
 * at most16384 bytes/17 chunks. Session's64-schema joint capacity still applies.
 * All mutable authority is registered global state; actor copies remain defaults.
 * The default order is input4, Attack5, Vitality6, RespawnFlow7, controller pre20.
 * Calls submitting attack input run at collision.priority with an actual smaller
 * system ID. All application movement/mask writes affecting this tick's attack
 * must precede Attack; later solver motion affects the next capture only.
 */
/// @brief Immutable bounded attack authoring and explicit registration namespaces.
struct DetAttack2DConfig {
    DetVitality2DConfig vitality;
    std::uint32_t stateSchema=240,ledgerSchema=241,policySchemaBase=242,
        systemId=5,validatorId=77,logicProfileId=77;
    bool cancelOnRecovery=true;
    std::vector<DetAttackOwner2D> owners;
};
/// Per sorted owner, fields are based at1000+200*(zero-based owner index).
enum DetAttackField2D : std::uint32_t {
    DetAttackHeld=10,DetAttackFacingRight=20,DetAttackLatchedHeld=30,
    DetAttackPhase=40,DetAttackLockedFacingRight=50,DetAttackCount=60,
    DetAttackStartTick=70,DetAttackActiveBegin=80,DetAttackActiveEnd=90,
    DetAttackRecoveryEnd=100,DetAttackHitMask=110,DetAttackCancelCount=120,
    DetAttackLastCancelTick=130,DetAttackRecoveryGeneration=140,
    DetAttackCycleAttemptBase=150
};
enum DetAttackGlobalField2D : std::uint32_t {DetAttackProcessedTick=1};
enum DetAttackLedgerField2D : std::uint32_t {
    DetAttackAttemptCount=10,DetAttackLastAttemptTick=20,DetAttackLastAttemptCycle=30,
    DetAttackLastTarget=40,DetAttackLastPermission=50,DetAttackLastCode=60,
    DetAttackLastRequested=70,DetAttackLastActual=80,DetAttackLastHPBefore=90,
    DetAttackLastHPAfter=100,DetAttackLastVitalitySequence=110,
    DetAttackLastCancelReason=120
};
enum DetAttackLedgerGlobalField2D : std::uint32_t {DetAttackTotalAttempts=1};
/** @brief Copied input, most recently processed phase and attack-local target bits.
 * @note After checkpoint nextTick N>0, phase describes execution tick N-1, not
 * prediction at N. A start at T has warmup[T,T+W), active[T+W,T+W+A) and
 * recovery[T+W+A,T+W+A+R). Zero-length windows are skipped. Dead/missing owners
 * and an explicit new recovery generation cancel a still-open cycle at its next
 * Attack callback. The current held edge is consumed on cancellation/busy/dead;
 * no input buffer, HP0 retry, automatic repeat or external effects exist.
 * Count0 has zero windows. startTick/lastCancelTick are copied zero-based and
 * meaningful with their counts; registered tick fields use checked tick+1 stamps.
 * cycleAttemptBase plus popcount(hitMask) equals the owner's cumulative attempts.
 * recoveryGeneration is the most recent Attack snapshot of restoreCount (or0
 * without recovery); a later same-tick accepted post may advance that count.
 */
/// @brief Copied registered input, processed phase and exact per-cycle hit ledger.
struct DetAttackState2D {
    DetAttackInput2D input;
    bool latchedHeld=false,lockedFacingRight=true;
    DetAttackPhase2D phase=DetAttackPhase2D::Idle;
    std::uint64_t attackCount=0,startTick=0,activeBegin=0,activeEnd=0,recoveryEnd=0,
        cancelCount=0,lastCancelTick=0,recoveryGeneration=0,cycleAttemptBase=0;
    std::uint32_t hitMask=0;
};
/// @brief Copied last target attempt plus cumulative attempts and last cancellation.
/// @note HP values are historical actual Vitality results, not current HP mirrors.
/// target/code/sequence are zero before the first attempt; cancellation is separate
/// and does not create an HP attempt. Neither result is a historical authentication.
struct DetAttackResult2D {
    std::uint64_t attemptCount=0,tick=0,attackCount=0,vitalitySequence=0;
    SimEntityId target=0;std::uint32_t permission=0;
    DetVitalityCode2D code=DetVitalityCode2D::None;
    std::uint64_t requested=0,actual=0,hpBefore=0,hpAfter=0;
    DetAttackCancel2D lastCancelReason=DetAttackCancel2D::None;
};
/** @details Attack acquires one immutable owned scene query and initial alive
 * snapshot for all owners/targets before any HP changes. Owners then target IDs
 * execute in stable order. An initially alive owner can finish its prepared tick
 * even if an earlier owner kills it, enabling same-tick trades. The next Attack
 * cancels dead owners. Each target can be attempted once per attack; normal Dead,
 * Invulnerable or BudgetExceeded also consumes its bit, without retry. Geometric
 * misses may be tested again in later active ticks. This is sampled overlap, not
 * a sword sweep, wall-occlusion test or collision/pose owner. Query capture uses
 * registered pre-solver geometry, including true wedges at their frozen positions.
 * Overflow or invalid access/config/geometry faults through Session; earlier valid
 * HP writes require explicit restore. Pure validators reject inconsistent restored
 * bytes atomically and bind full policy/actual system identity, not run history.
 */
/// @brief Register finite held-edge attacks and pure state/policy validators.
/// @note Matching Vitality is a separate required installation; no dependencies are installed twice.
bool installDetAttack2D(DeterministicSession& session,DetAttack2DConfig config);
/// @brief Submit registered input before the actual Attack system; invalid or late calls throw.
/// @note Escaping facade exceptions fault the tick; DetTickContext access errors remain sticky.
void detSetAttackInput2D(DetTickContext& context,const DetAttack2DConfig& config,
    SimEntityId owner,DetAttackInput2D input);
/// @brief Copy one owner's registered attack state without changing authority.
DetAttackState2D detAttackState2D(DetTickContext& context,const DetAttack2DConfig& config,SimEntityId owner);
DetAttackState2D detAttackState2D(const DetSessionCheckpoint& checkpoint,const DetAttack2DConfig& config,SimEntityId owner);
/// @brief Copy one owner's historical target result and cancellation metadata.
DetAttackResult2D detAttackResult2D(DetTickContext& context,const DetAttack2DConfig& config,SimEntityId owner);
DetAttackResult2D detAttackResult2D(const DetSessionCheckpoint& checkpoint,const DetAttack2DConfig& config,SimEntityId owner);
/// @brief Complete mutable descriptors: at most121 phase/input and97 ledger fields.
std::vector<DetTypedStateSchema> detAttackStateSchemas2D(const DetAttack2DConfig& config);
/// @brief Resolve an owner field in stateSchema from its stable owner actor ID.
std::uint32_t detAttackField2D(const DetAttack2DConfig& config,SimEntityId owner,DetAttackField2D field);
/// @brief Resolve an owner field in ledgerSchema from its stable owner actor ID.
std::uint32_t detAttackLedgerField2D(const DetAttack2DConfig& config,SimEntityId owner,DetAttackLedgerField2D field);
/// @brief Complete literal policy bytes/descriptors; not a file codec or authenticity proof.
std::vector<std::uint8_t> encodeDetAttackConfig2D(const DetAttack2DConfig& config);
std::vector<DetTypedStateSchema> detAttackPolicySchemas2D(const DetAttack2DConfig& config);
} // namespace ayt::entity
