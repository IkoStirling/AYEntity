#pragma once
// design reference: AYEntity/design.md Stage34 registered trigger interactions.
#include <AYEntity/DeterministicActions2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetInteraction2DProfileVersion=1;
inline constexpr std::size_t kDetInteraction2DMaxRules=16;
inline constexpr std::size_t kDetInteraction2DMaxPolicyBytes=9216;

/** @brief One authored Enter/Exit rule with a single bounded pending action.
 * @note Trigger/actor are stable Sim identities. The trigger must initially be
 * an active trigger body and actor a solid Kinematic; either may later retire.
 * No Stay rule, external effect, implicit actor spawn or action retry is supported.
 */
struct DetInteractionRule2D {
    std::uint32_t id=0;
    SimEntityId trigger=0,actor=0;
    DetTriggerPhase2D phase=DetTriggerPhase2D::Enter;
    DetActionRequest2D action;
    /// Consumed on scheduling, including an action subsequently denied.
    bool once=false;
    /// 0..65535 logical ticks. Earliest next schedule is lastScheduleTick+N.
    /// Zero permits multiple same-tick events; one permits the following tick.
    std::uint32_t cooldownTicks=0;
    /// 0..65535. Zero executes on event processing tick; N executes at tick+N.
    std::uint32_t delayTicks=0;
};

/** @brief Immutable registered rules bound to an already installed Actions facade.
 * @note Install Actions first, then these rules, then actors/Level, then seal.
 * Rules contain1..16 strictly increasing nonzero IDs. Full rule and Actions
 * configuration bytes (not only a hash) bind typed global/actor defaults, in at
 * most10 policy schemas. Actor copies stay immutable; mutable authority is global.
 * The system runs at collision.priority before collision and every configured
 * path pre. The caller must also put it before character pre/input consumers.
 */
struct DetInteraction2DConfig {
    DetActions2DConfig actions;
    std::uint32_t stateSchema=80,policySchemaBase=81,systemId=9,
        validatorId=62,logicProfileId=62;
    std::vector<DetInteractionRule2D> rules;
};

/// Eight fields per sorted rule, based at100*(zero-based rule index+1).
enum DetInteractionField2D : std::uint32_t {
    DetInteractionFired=10,DetInteractionPending=20,
    DetInteractionScheduleCount=30,DetInteractionAttemptCount=40,
    DetInteractionLastScheduleTick=50,DetInteractionDueTick=60,
    DetInteractionLastExecutionTick=70,DetInteractionLastResult=80
};

/** @brief Copied registered state for one rule; all times are zero-based ticks.
 * @note Count zero distinguishes an absent schedule/execution from tick zero.
 * DueTick is canonical zero when no job is pending. CooldownUntil is derived
 * from registered lastScheduleTick and immutable cooldown, not hidden state.
 */
struct DetInteractionState2D {
    bool fired=false,pending=false;
    std::uint64_t scheduleCount=0,attemptCount=0,lastScheduleTick=0,dueTick=0,
        lastExecutionTick=0,cooldownUntil=0;
    DetActionCode2D lastResult=DetActionCode2D::None;
};

/** @brief Install finite next-tick trigger routing and pure restore validators.
 * @note Check false and discard failed partial configuration. Only canonical
 * 24-byte events from actions.collision.systemId AND its eventType are consumed.
 * Other producer/type namespaces are ignored; malformed matching events fault.
 * Existing jobs due now execute by rule ID first. Then events use canonical
 * (source,sequence) order and rules ascending ID; newly scheduled delay0 jobs
 * execute immediately. Pending duplicates, cooldown and consumed once rules
 * ignore events. Collision Enter/Exit arrives the tick after the collision.
 * Full state/results participate in checkpoint, rollback and replay. No external
 * confirmed gameplay effect is executed here; applications separately consume
 * confirmation according to their existing Session/Rollback contract.
 */
/// @brief Install rules after matching Actions and before actors/seal.
/// @note This does not install Actions, collision, paths or characters.
bool installDetInteraction2D(DeterministicSession& session,DetInteraction2DConfig config);
/// @brief Copy rule state during a system callback; unknown rule/config throws.
DetInteractionState2D detInteractionState2D(DetTickContext& context,
    const DetInteraction2DConfig& config,std::uint32_t ruleId);
/// @brief Copy a committed rule state using its canonical registered descriptor.
DetInteractionState2D detInteractionState2D(const DetSessionCheckpoint& checkpoint,
    const DetInteraction2DConfig& config,std::uint32_t ruleId);
/// @brief Canonical mutable-global descriptor; actor copies remain its defaults.
DetTypedStateSchema detInteractionStateSchema2D(const DetInteraction2DConfig& config);
/// @brief Resolve a stable field from a sorted rule ID, rejecting unknown IDs.
std::uint32_t detInteractionField2D(const DetInteraction2DConfig& config,
    std::uint32_t ruleId,DetInteractionField2D field);
/// @brief Complete profile1 immutable policy bytes, not a file/replay format.
std::vector<std::uint8_t> encodeDetInteractionConfig2D(const DetInteraction2DConfig& config);
} // namespace ayt::entity
