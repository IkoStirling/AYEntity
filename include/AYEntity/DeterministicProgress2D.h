#pragma once
// design reference: AYEntity/design.md Stage37 finite registered gameplay progress.
#include <AYEntity/DeterministicActions2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetProgress2DProfileVersion=1;
inline constexpr std::size_t kDetProgress2DMaxCounters=16,kDetProgress2DMaxObjectives=8,
    kDetProgress2DMaxConditions=8,kDetProgress2DMaxClaims=16,kDetProgress2DMaxPolicyBytes=10000;
/// @brief Fixed UInt64 counter with inclusive bounds and an authored initial value.
struct DetProgressCounter2D {std::uint32_t id=0;std::uint64_t minimum=0,maximum=UINT64_MAX,initial=0;};
/// @brief One inclusive integer threshold; conditions are sorted by counter ID.
struct DetProgressCondition2D {std::uint32_t counter=0;std::uint64_t atLeast=0;};
/// @brief Derived AND objective, not a hidden completion latch or a callback.
struct DetProgressObjective2D {std::uint32_t id=0;std::vector<DetProgressCondition2D> conditions;};
/// @brief One actor's immutable permission to collect a controlled trigger once.
/// @note Alternative actors may share a trigger; only the first Applied disable can claim it.
struct DetCollectibleClaim2D {
    std::uint32_t id=0;
    SimEntityId trigger=0,actor=0;
    std::uint32_t counter=0;
    std::uint64_t amount=1;
};
/** @brief Finite registered counters/objectives/claims, bound to matching installed Actions.
 * @note Install Actions and this facade before actors/seal. No system, queue or
 * input acquisition is installed. Execute calls at collision.priority before
 * every path/character pre. IDs in each table are strictly increasing and nonzero.
 * All immutable configuration, including complete Actions policy, enters typed
 * defaults/global/actor copies, in at most10 policy schemas. Mutable authority is
 * global only; actor copies remain defaults. Session schema capacity still applies.
 * Vitality2's explicitly owned HP-only adapter may restore its dedicated counter
 * after the motion pipeline; ordinary application Progress calls remain pre-path.
 */
struct DetProgress2DConfig {
    DetActions2DConfig actions;
    std::uint32_t stateSchema=96,policySchemaBase=97,validatorId=64,logicProfileId=64;
    std::vector<DetProgressCounter2D> counters;
    std::vector<DetProgressObjective2D> objectives;
    std::vector<DetCollectibleClaim2D> claims;
};
enum class DetProgressOperation2D : std::uint32_t {None=0,Add=1,Subtract=2,Claim=3};
enum class DetProgressCode2D : std::uint32_t {
    None=0,Applied=1,Unchanged=2,UnknownCounter=3,OutOfRange=4,UnknownClaim=5,
    AlreadyClaimed=6,MissingActor=7,NoMatchingEnter=8,InactiveTrigger=9,ActionDenied=10
};
/// @brief Explicit counter request; Claim is reserved for claimDetCollectible2D.
struct DetProgressRequest2D {
    DetProgressOperation2D operation=DetProgressOperation2D::Add;
    std::uint32_t counter=0;
    std::uint64_t amount=0;
};
/** @brief Copied last operation result, including normal denials.
 * @note Sequence is one-based and includes all attempts. Tick is actual zero-based
 * and meaningful only for sequence>0. ID is a counter ID or claim ID. Action code/
 * sequence are zero unless the claim reached executeDetAction2D. This is not a log.
 */
struct DetProgressResult2D {
    DetProgressCode2D code=DetProgressCode2D::None;
    std::uint64_t tick=0,sequence=0;
    DetProgressOperation2D operation=DetProgressOperation2D::None;
    std::uint32_t id=0,counter=0;
    std::uint64_t amount=0;
    DetActionCode2D actionCode=DetActionCode2D::None;
    std::uint64_t actionSequence=0;
};
/// @brief Copied single-claim state; tick is meaningful only when claimed=true.
struct DetCollectibleState2D {bool claimed=false;std::uint64_t tick=0;};
enum DetProgressField2D : std::uint32_t {
    DetProgressTotalCount=10,DetProgressLastOperation=20,DetProgressLastId=30,
    DetProgressLastCounter=40,DetProgressLastAmount=50,DetProgressLastCode=60,
    DetProgressLastActionCode=70,DetProgressLastTick=80,DetProgressLastActionSequence=90
};
/** @brief Install complete immutable progress and pure boundary/restore validators.
 * @note Check false and abandon partial setup. Claims initially name an actual
 * AABB trigger authorized by Actions and a solid Kinematic actor. Unknown identities
 * are rejected; retired identities remain valid and return MissingActor. Claimed
 * live triggers must retain mask0. Re-enabling a collected trigger is an invalid
 * committed state rejected by the validator, not a normal Action denial.
 * Counter values are independently changeable: no false invariant equates a
 * counter with the sum of collected amounts. Objectives are recomputed from values.
 */
/// @brief Register finite typed gameplay progress after matching Actions.
bool installDetProgress2D(DeterministicSession& session,DetProgress2DConfig config);
/// @brief Add/subtract using inclusive bounds and checked arithmetic; zero is Unchanged.
/// @note Normal OutOfRange/UnknownCounter leaves values intact and records a result.
DetProgressResult2D executeDetProgress2D(DetTickContext& context,const DetProgress2DConfig& config,DetProgressRequest2D request);
/** @brief Claim only from an exact canonical Enter delivered by the configured collision producer/type.
 * @note The entry itself checks the event, claim, actor/body participation and counter
 * range before executing CollisionDisable. Only Applied disables the trigger and
 * increments the counter/claim stamp. Budget/other normal denials preserve both.
 * Already disabled triggers do not grant a claim, and duplicate calls do not add.
 * Invalid policy/context/counter overflow throws and follows explicit Session restore.
 * No wall clock, external inventory, confirmed effect or hidden latch is written.
 */
/// @brief Atomically collect one authorized trigger on a delivered real Enter.
DetProgressResult2D claimDetCollectible2D(DetTickContext& context,const DetProgress2DConfig& config,std::uint32_t claimId);
/// @brief Copied latest result; None/sequence0 precedes the first operation.
DetProgressResult2D detProgressResult2D(DetTickContext& context,const DetProgress2DConfig& config);
DetProgressResult2D detProgressResult2D(const DetSessionCheckpoint& checkpoint,const DetProgress2DConfig& config);
/// @brief Read one registered counter; unknown IDs/config throw.
std::uint64_t detProgressCounter2D(DetTickContext& context,const DetProgress2DConfig& config,std::uint32_t counterId);
std::uint64_t detProgressCounter2D(const DetSessionCheckpoint& checkpoint,const DetProgress2DConfig& config,std::uint32_t counterId);
/// @brief Derive all authored thresholds from current counter values; completion can reverse.
bool detProgressObjective2D(DetTickContext& context,const DetProgress2DConfig& config,std::uint32_t objectiveId);
bool detProgressObjective2D(const DetSessionCheckpoint& checkpoint,const DetProgress2DConfig& config,std::uint32_t objectiveId);
/// @brief Copy one claim's registered boolean/stamp, including a retired trigger.
DetCollectibleState2D detCollectibleState2D(DetTickContext& context,const DetProgress2DConfig& config,std::uint32_t claimId);
DetCollectibleState2D detCollectibleState2D(const DetSessionCheckpoint& checkpoint,const DetProgress2DConfig& config,std::uint32_t claimId);
/// @brief Canonical mutable descriptor and stable field resolvers for committed reads.
DetTypedStateSchema detProgressStateSchema2D(const DetProgress2DConfig& config);
std::uint32_t detProgressCounterField2D(const DetProgress2DConfig& config,std::uint32_t counterId);
std::uint32_t detCollectibleField2D(const DetProgress2DConfig& config,std::uint32_t claimId,bool stamp=false);
/// @brief Complete profile1 identity bytes; not a save file or a replay codec.
std::vector<std::uint8_t> encodeDetProgressConfig2D(const DetProgress2DConfig& config);
} // namespace ayt::entity
