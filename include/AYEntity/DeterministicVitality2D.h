#pragma once
// design reference: Stage47 finite deterministic vitality; no automatic respawn.
#include <AYEntity/DeterministicProgress2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetVitality2DProfileVersion=1;
inline constexpr std::size_t kDetVitality2DMaxActors=8,kDetVitality2DMaxPermissions=16,
    kDetVitality2DMaxRegions=16,kDetVitality2DMaxPolicyBytes=4096;
enum class DetVitalityKind2D : std::uint32_t {None=0,Hurt=1,Heal=2};
enum class DetVitalityCode2D : std::uint32_t {
    None=0,Applied=1,Unchanged=2,UnknownPermission=3,AmountExceeded=4,
    MissingActor=5,InactiveRegion=6,Dead=7,Invulnerable=8,BudgetExceeded=9
};
/// @brief A stable Kinematic actor owns one dedicated Progress HP counter.
struct DetVitalityActor2D {SimEntityId actor=0;std::uint32_t counter=0,invulnerabilityTicks=0;};
/// @brief Immutable direct permission, never an arbitrary target or callback.
struct DetVitalityPermission2D {
    std::uint32_t id=0;SimEntityId actor=0;
    DetVitalityKind2D kind=DetVitalityKind2D::Hurt;std::uint64_t maxAmount=1;
};
/// @brief Fixed Hurt/Heal on a real configured collision Enter; Stay/Exit do nothing.
struct DetVitalityRegion2D {
    std::uint32_t id=0;SimEntityId trigger=0,actor=0;
    DetVitalityKind2D kind=DetVitalityKind2D::Hurt;std::uint64_t amount=1;
};
/** @brief Sole owner of dedicated bounded HP counters and registered vitality results.
 * @note Install matching Actions, dedicated Progress, then Vitality before actors/seal.
 * Progress contains exactly one min0/max>0 counter per actor, no objectives/claims;
 * its complete Actions authority may be shared with the rest of the scene. Tables
 * use strictly ascending nonzero IDs (actors use actor IDs), with no shared counters.
 * Direct calls run before this system at collision.priority; events then run in
 * canonical (source,sequence), region ID order, at most once per binding per tick.
 * No pose, mask, controller, lifecycle or respawn writes occur. All callbacks own
 * immutable configuration, and all mutable authority is typed global state.
 */
struct DetVitality2DConfig {
    DetProgress2DConfig progress;
    std::uint32_t stateSchema=192,policySchemaBase=193,systemId=6,validatorId=75,logicProfileId=75;
    std::uint32_t maxOperationsPerTick=32;
    std::vector<DetVitalityActor2D> actors;
    std::vector<DetVitalityPermission2D> permissions;
    std::vector<DetVitalityRegion2D> regions;
};
/// @brief Copied actor state; dead is derived from HP0, not a hidden latch.
struct DetVitalityState2D {
    std::uint64_t hp=0,invulnerableUntil=0,attemptCount=0,hurtCount=0,healCount=0,deathCount=0,lastTick=0,lastHurtTick=0;
    bool dead=true;
};
/// @brief Last global attempt, including normal denials; not an event log.
struct DetVitalityResult2D {
    DetVitalityCode2D code=DetVitalityCode2D::None;
    DetVitalityKind2D kind=DetVitalityKind2D::None;
    std::uint64_t tick=0,sequence=0;SimEntityId actor=0;
    std::uint32_t permission=0,region=0;
    std::uint64_t requested=0,actual=0,hpBefore=0,hpAfter=0;
};
enum DetVitalityField2D : std::uint32_t {
    DetVitalityTotalCount=10,DetVitalityBudgetTick=20,DetVitalityTickCount=30,
    DetVitalityProcessedTick=40,DetVitalityOwnedProgressCount=50,
    DetVitalityWriteCounter=60,DetVitalityWriteKind=70,DetVitalityWriteAmount=80,DetVitalityWriteTick=90,
    DetVitalityLastCode=100,DetVitalityLastKind=110,DetVitalityLastActor=120,
    DetVitalityLastPermission=130,DetVitalityLastRegion=140,DetVitalityLastRequested=150,
    DetVitalityLastActual=160,DetVitalityLastBefore=170,DetVitalityLastAfter=180,DetVitalityLastTick=190,
    DetVitalityUnknownCount=200
};
enum DetVitalityActorField2D : std::uint32_t {
    DetVitalityHP=1,DetVitalityUntil=2,DetVitalityAttempts=3,DetVitalityHurts=4,
    DetVitalityHeals=5,DetVitalityDeaths=6,DetVitalityActorLastTick=7,DetVitalityLastHurtTick=8
};
/** @brief Register complete immutable policy and pure checkpoint/restore closure.
 * @note Check false and abandon partial setup. Live actors must be solid Kinematic;
 * region targets must be active ordinary AABB triggers at initial seal. References
 * may retire later, but never-issued identities are invalid. Actor copies remain
 * defaults. Dedicated Progress operations outside this facade invalidate closure,
 * even an unchanged/denied operation. Invalid restore is atomic. Install system ID
 * before every bound path pre and character pre, sharing collision.priority.
 */
/// @brief Register dedicated HP authority, deterministic Enter routing and validators.
bool installDetVitality2D(DeterministicSession& session,DetVitality2DConfig config);
/** @brief Execute one fixed direct permission before the Vitality routing system.
 * @note Hurt/Heal saturate at0/max. Dead actors cannot heal or revive. Zero amount
 * is Unchanged for an alive actor; no Progress write or protection refresh occurs.
 * Successful nonlethal Hurt at tick t with N>0 protects [t,t+N); N1 protects later
 * attempts in that tick. Heal ignores protection but neither clears nor refreshes
 * it; lethal Hurt clears it. Normal permission/amount/missing/dead/protection/budget
 * denials preserve HP/protection and record an attempt. Preflight all counters and
 * deadline overflow before HP changes. Invalid policy, late authority writes and
 * overflow throw and require explicit Session restore; prior valid writes are not
 * undone. Death changes numerical state only, never immobilizes or respawns.
 */
DetVitalityResult2D executeDetVitality2D(DetTickContext& context,const DetVitality2DConfig& config,
    std::uint32_t permissionId,std::uint64_t amount);
/// @brief Copy one registered actor's vitality; unknown actor/config throws.
DetVitalityState2D detVitalityState2D(DetTickContext& context,const DetVitality2DConfig& config,SimEntityId actor);
DetVitalityState2D detVitalityState2D(const DetSessionCheckpoint& checkpoint,const DetVitality2DConfig& config,SimEntityId actor);
/// @brief Copy last result; None/sequence0 precedes the first attempt.
DetVitalityResult2D detVitalityResult2D(DetTickContext& context,const DetVitality2DConfig& config);
DetVitalityResult2D detVitalityResult2D(const DetSessionCheckpoint& checkpoint,const DetVitality2DConfig& config);
/// @brief Exact descriptor/field factories for committed typed reads.
DetTypedStateSchema detVitalityStateSchema2D(const DetVitality2DConfig& config);
std::uint32_t detVitalityActorField2D(const DetVitality2DConfig& config,SimEntityId actor,DetVitalityActorField2D member);
/// @brief Full immutable policy bytes and descriptors; not a save file or authentication.
std::vector<std::uint8_t> encodeDetVitalityConfig2D(const DetVitality2DConfig& config);
std::vector<DetTypedStateSchema> detVitalityPolicySchemas2D(const DetVitality2DConfig& config);
} // namespace ayt::entity
