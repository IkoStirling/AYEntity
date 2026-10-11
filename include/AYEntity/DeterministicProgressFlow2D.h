#pragma once
// design reference: AYEntity/design.md Stage41 finite registered progress routing.
#include <AYEntity/DeterministicProgress2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetProgressFlow2DProfileVersion=1;
inline constexpr std::size_t kDetProgressFlow2DMaxClaims=16,kDetProgressFlow2DMaxRules=8,
    kDetProgressFlow2DMaxPolicyBytes=12288;
/// @brief Automatic claim permission, optionally gated by a current derived objective.
/// @note Claim IDs are increasing existing Progress IDs; objective0 means no gate.
struct DetProgressFlowClaimBinding2D {std::uint32_t claim=0,requiredObjective=0;};
/// @brief One fixed action on a derived objective's false-to-true transition.
/// @note Once is consumed on an actual attempt, including normal Action denial.
struct DetProgressFlowRule2D {
    std::uint32_t id=0,objective=0;
    DetActionRequest2D action;
    bool once=false;
};
/** @brief Finite automatic collection and rising-edge actions over matching installed Progress.
 * @note Install Actions, Progress, then Flow before actors/seal. Tables are sorted
 * and nonzero, with at most16 claim bindings and8 objective rules; at least one
 * table is nonempty. Full Progress/Actions/Flow policy enters typed immutable
 * defaults, at most12288 bytes/13 policy schemas. All mutable authority is global;
 * actor copies remain defaults. Session's total schema capacity still applies.
 * All external Progress writes run BEFORE Flow at collision.priority. Flow runs
 * before every path/character pre. The default order is input6, Flow8, Interaction9.
 * The installer checks authorized path IDs; application assembly checks other pre
 * callbacks. A later write changing a routed objective's truth faults at the boundary.
 * Initial previousSatisfied=false means an initially true objective fires at tick0.
 */
/// @brief Immutable registered automatic-progress policy and registration namespaces.
struct DetProgressFlow2DConfig {
    DetProgress2DConfig progress;
    std::uint32_t collectStateSchema=112,objectiveStateSchema=113,policySchemaBase=114,
        systemId=8,validatorId=65,logicProfileId=65;
    std::vector<DetProgressFlowClaimBinding2D> claims;
    std::vector<DetProgressFlowRule2D> rules;
};
/// Per sorted binding, fields are based at100*(zero-based index+1).
enum DetProgressFlowClaimField2D : std::uint32_t {
    DetFlowClaimAttemptCount=10,DetFlowClaimLastTick=20,DetFlowClaimProgressSequence=30,
    DetFlowClaimLastResult=40,DetFlowClaimActionAttemptCount=50,
    DetFlowClaimLastActionCode=60,DetFlowClaimLastActionSequence=70
};
/// Per sorted objective rule, fields are based at100*(zero-based index+1).
enum DetProgressFlowObjectiveField2D : std::uint32_t {
    DetFlowPreviousSatisfied=10,DetFlowOnceConsumed=20,DetFlowObjectiveAttemptCount=30,
    DetFlowObjectiveLastTick=40,DetFlowObjectiveLastResult=50,DetFlowObjectiveActionSequence=60
};
/// @brief Copied last claim attempt and cumulative attempts, including normal denials.
/// @note Gate-closed events do not attempt Progress. Tick is meaningful when attemptCount>0.
struct DetProgressFlowClaimState2D {
    std::uint64_t attemptCount=0,lastTick=0,progressSequence=0,actionAttemptCount=0;
    DetProgressCode2D lastResult=DetProgressCode2D::None;
    DetActionCode2D lastActionCode=DetActionCode2D::None;
    std::uint64_t lastActionSequence=0;
};
/// @brief Copied edge memory, once consumption and last action outcome for one rule.
/// @note No pending queue/retry exists. Tick is meaningful when attemptCount>0.
struct DetProgressFlowObjectiveState2D {
    bool previousSatisfied=false,onceConsumed=false;
    std::uint64_t attemptCount=0,lastTick=0;
    DetActionCode2D lastResult=DetActionCode2D::None;
    std::uint64_t actionSequence=0;
};
/** @details Consume only canonical Enter events from Progress's collision producer
 * AND type. Malformed matching events fault; other namespaces and Stay/Exit are
 * ignored. Events use (source,sequence) order, then ascending claim ID. Each binding
 * attempts at most once per tick. Each gate reads current counters, so earlier
 * claims may unlock later claims. Closed gates and normal denials require a new
 * Enter; remaining inside does not retry or collect after a later unlock.
 * After collection, rules evaluate in ascending ID. A false-to-true edge attempts
 * its fixed Action; once consumes on that attempt, including denial. Non-once can
 * retry only after false then true, never every tick while true. No external effect,
 * wall clock, arbitrary callback, pose writer or hidden mutable capture is installed.
 * Invalid configuration/registered access or exhausted counters follows Session's
 * fault/explicit-restore contract. Restore is atomic and validates exact identities.
 */
/// @brief Register bounded automatic progress after matching installed Progress.
/// @note Check false and discard partial setup; this does not install dependencies.
bool installDetProgressFlow2D(DeterministicSession& session,DetProgressFlow2DConfig config);
/// @brief Copy one automatic claim's registered result; unknown IDs/config throw.
DetProgressFlowClaimState2D detProgressFlowClaim2D(DetTickContext& context,const DetProgressFlow2DConfig& config,std::uint32_t claimId);
DetProgressFlowClaimState2D detProgressFlowClaim2D(const DetSessionCheckpoint& checkpoint,const DetProgressFlow2DConfig& config,std::uint32_t claimId);
/// @brief Copy one objective rule's registered edge memory and execution result.
DetProgressFlowObjectiveState2D detProgressFlowObjective2D(DetTickContext& context,const DetProgressFlow2DConfig& config,std::uint32_t ruleId);
DetProgressFlowObjectiveState2D detProgressFlowObjective2D(const DetSessionCheckpoint& checkpoint,const DetProgressFlow2DConfig& config,std::uint32_t ruleId);
/// @brief Canonical mutable global descriptors; maximum fields113/49, actor copies immutable.
DetTypedStateSchema detProgressFlowCollectSchema2D(const DetProgressFlow2DConfig& config);
/// @brief Canonical objective edge/result global descriptor; actor copies remain defaults.
DetTypedStateSchema detProgressFlowObjectiveSchema2D(const DetProgressFlow2DConfig& config);
/// @brief Resolve a claim record field from its immutable stable claim ID.
std::uint32_t detProgressFlowClaimField2D(const DetProgressFlow2DConfig& config,std::uint32_t claimId,DetProgressFlowClaimField2D field);
/// @brief Resolve an objective record field from its immutable stable rule ID.
std::uint32_t detProgressFlowObjectiveField2D(const DetProgressFlow2DConfig& config,std::uint32_t ruleId,DetProgressFlowObjectiveField2D field);
/// @brief Complete immutable profile1 identity, not a checkpoint or file codec.
std::vector<std::uint8_t> encodeDetProgressFlowConfig2D(const DetProgressFlow2DConfig& config);
} // namespace ayt::entity
