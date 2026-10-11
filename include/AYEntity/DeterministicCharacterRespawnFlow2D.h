#pragma once
// design reference: AYEntity/design.md registered content routing over profile9 respawn.
#include <AYEntity/DeterministicCharacterRespawn2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetCharacterRespawnFlow2DProfileVersion=1;
inline constexpr std::uint32_t kDetCharacterRespawnFlow2DExternalProfileVersion=2;
inline constexpr std::size_t kDetCharacterRespawnFlow2DMaxBindings=8,
    kDetCharacterRespawnFlow2DMaxRegions=16,kDetCharacterRespawnFlow2DMaxPolicyBytes=4096,
    kDetCharacterRespawnFlow2DMaxExternalPermissions=16;
/// @brief Initial active checkpoint authority for one stable actor, sorted by actor ID.
struct DetRespawnFlowBinding2D {SimEntityId actor=0;std::uint32_t spawn=0;};
enum class DetRespawnFlowRegionKind2D : std::uint32_t {Checkpoint=0,Death=1};
/// @brief One immutable actor-specific Enter permission, sorted by nonzero region ID.
/// @note Checkpoint names an authorized spawn; Death must use spawn0. The trigger
/// must initially exist as an active trigger and the actor as a solid Kinematic.
/// Retired stable identities retain permission without retaining an Entity handle.
struct DetRespawnFlowRegion2D {
    std::uint32_t id=0;SimEntityId trigger=0,actor=0;
    DetRespawnFlowRegionKind2D kind=DetRespawnFlowRegionKind2D::Checkpoint;
    std::uint32_t spawn=0;
};
/// @brief One actor's external-death authority in an actual earlier registered system.
/// @note producerSystem is the current callback identity, never an input command source.
struct DetRespawnFlowExternalDeathPermission2D {
    std::uint32_t id=0;SimEntityId actor=0;std::uint32_t producerSystem=0;
};
/** @brief Registered checkpoint/death/manual routing after a matching Respawn facade.
 * @note Install Respawn, then this facade, before actors/seal. It does not install
 * collision, character or Respawn. Bindings contain1..8 increasing actor IDs;
 * regions contain0..16 increasing IDs, so a manual-only recipe is supported.
 * Full nested Respawn policy and routing bytes enter immutable typed defaults,
 * globals and actor copies: at most4096 bytes/five policy schemas. The global
 * state has at most121 fields; actor copies remain their initial defaults.
 * Explicit externalDeaths selects profile2 and a separate at-most41-field
 * external schema for eight actors; the complete policy bound remains4096 bytes.
 * False requires default new-only fields and retains the exact old profile1 bytes.
 * Default order at collision.priority is canonical input6, RespawnFlow7,
 * ProgressFlow8, Interaction9, then path/character pre. Put every caller of the
 * manual setter before Flow, and Flow before every configured path/character pre.
 * Registration capacities are shared with all other Session features.
 */
/// @brief Full immutable routing identity; this adds no ActionKind or pose writer.
/// @brief Complete routing policy with optional source-bound external deaths.
struct DetCharacterRespawnFlow2DConfig {
    DetCharacterRespawn2DConfig respawn;
    std::uint32_t stateSchema=144,policySchemaBase=145,systemId=7,validatorId=73,logicProfileId=73;
    std::vector<DetRespawnFlowBinding2D> bindings;
    std::vector<DetRespawnFlowRegion2D> regions;
    /// Explicit opt-in; false preserves every profile1 descriptor and policy byte.
    bool externalDeaths=false;
    std::uint32_t externalStateSchema=168;
    std::vector<DetRespawnFlowExternalDeathPermission2D> externalDeathPermissions;
};
enum class DetRespawnFlowRequestKind2D : std::uint32_t {None=0,Death=1,Manual=2,ExternalDeath=3};
/// Fields are based at1000*(zero-based binding index+1), not at the runtime actor ID.
enum DetRespawnFlowField2D : std::uint32_t {
    DetRespawnFlowActiveSpawn=10,DetRespawnFlowCheckpointCount=20,
    DetRespawnFlowLastCheckpointTick=30,DetRespawnFlowLastCheckpointRegion=40,
    DetRespawnFlowDeathCount=50,DetRespawnFlowLastDeathTick=60,DetRespawnFlowLastDeathRegion=70,
    DetRespawnFlowManualCount=80,DetRespawnFlowRequestCount=90,
    DetRespawnFlowLastRequestTick=100,DetRespawnFlowLastRequestCode=110,
    DetRespawnFlowLastRequestKind=120,DetRespawnFlowLastRequestSpawn=130,
    DetRespawnFlowManualRequested=140,DetRespawnFlowProcessedTick=150
};
/// @brief Profile2-only members in externalStateSchema, using the same binding bases.
enum DetRespawnFlowExternalField2D : std::uint32_t {
    DetRespawnFlowExternalRequested=10,DetRespawnFlowExternalPendingPermission=20,
    DetRespawnFlowExternalCount=30,DetRespawnFlowExternalLastTick=40,
    DetRespawnFlowExternalLastPermission=50
};
/// @brief Copied routing state; counts distinguish absent history from processing tick0.
/// @note All exposed last ticks are zero-based processing ticks, not emission ticks.
/// ManualCount counts chosen manual attempts; a simultaneous death consumes the
/// manual flag without a second attempt. RequestCode is meaningful when RequestCount>0.
/// The eventual collision outcome remains in detCharacterRespawnState2D; this
/// record neither duplicates that result nor claims that a queued request was accepted.
/// Profile2 DeathCount includes selected region/external deaths. LastDeathRegion0
/// with DeathCount>0 names an external last death; LastExternalDeathPermission and
/// matching last stamp identify its literal permission. Lost arbitration is not counted.
struct DetCharacterRespawnFlowState2D {
    std::uint32_t activeSpawn=0,lastCheckpointRegion=0,lastDeathRegion=0,lastRequestSpawn=0;
    std::uint64_t checkpointCount=0,lastCheckpointTick=0,deathCount=0,lastDeathTick=0,
        manualCount=0,requestCount=0,lastRequestTick=0;
    DetRespawnRequestCode2D lastRequestCode=DetRespawnRequestCode2D::Queued;
    DetRespawnFlowRequestKind2D lastRequestKind=DetRespawnFlowRequestKind2D::None;
    /// Selected external death attempts; zero defaults under profile1, not accepted moves.
    std::uint64_t externalDeathCount=0,lastExternalDeathTick=0;
    std::uint32_t lastExternalDeathPermission=0;
};
/** @brief Consume matching canonical Enter events and resolve one request per bound actor.
 * @note Only the configured collision producer AND event type are consumed;
 * malformed matching24-byte payloads fault. Stay/Exit and other namespaces are
 * ignored. Delivered events are from the previous collision tick. First select
 * every actor's checkpoint by canonical(source,sequence), then region ID. Apply
 * all checkpoint selections, then actors by increasing ID choose the first death
 * event in that same order; death wins over manual. Both use the new active spawn.
 * At most one underlying request is attempted per actor/tick. Occupied, Unavailable,
 * AlreadyRequested or capacity denial never schedules a retry. A new real Enter
 * or new manual input is required. Enter caused by an accepted spawn's endpoint
 * is a real event and can select that checkpoint again on the following tick.
 * Only registered state changes; no callback effect, timer, teleport trace or pose
 * write. Pending manual flags are false and ProcessedTick==nextTick at every
 * committed boundary. Pure validation binds full identities and rejects malformed
 * counters/stamps/permissions atomically; it does not authenticate historical play.
 */
/// @brief Register routing after matching Respawn; discard failed partial assembly.
bool installDetCharacterRespawnFlow2D(DeterministicSession& session,DetCharacterRespawnFlow2DConfig config);
/// @brief Register this tick's manual request before Flow; false does not cancel an earlier true.
/// @note True calls collapse to one flag. Unknown permission, mismatched policy or
/// a call after Flow faults; retired/disabled actor denial is recorded by the normal
/// underlying request. The flag is cleared even when death wins or a request is denied.
void detCharacterRespawnFlowInput2D(DetTickContext& context,SimEntityId actor,
    const DetCharacterRespawnFlow2DConfig& config,bool manual);
/** @brief Register one external death from a permitted actual callback before Flow.
 * @note Profile2 only. Full policy, actor permission, current context.systemId()
 * and ProcessedTick==tick are required. The first same-actor permission wins;
 * duplicates collapse. Flow applies all checkpoints, then region Death beats
 * external Death, which beats Manual. Pending flags clear even on denial or lost
 * arbitration; no automatic retry or HP mutation. Committed pending fields are0.
 * Unknown permission, unauthorized source, mismatched policy or late use throws.
 * A direct input callback cannot impersonate a later Vitality producer. Vitality
 * must retain its own registered within-tick lethal flag and flush in its actual
 * configured callback. This adds no pose writer or fake collision Enter.
 */
/// @brief Queue an authorized external death in this actual producer callback.
void detCharacterRespawnFlowDeath2D(DetTickContext& context,
    const DetCharacterRespawnFlow2DConfig& config,std::uint32_t permissionId);
/// @brief Read a copied routing result during a callback without retaining context.
DetCharacterRespawnFlowState2D detCharacterRespawnFlowState2D(DetTickContext& context,
    SimEntityId actor,const DetCharacterRespawnFlow2DConfig& config);
/// @brief Read a compatible complete checkpoint; this does not execute or restore it.
DetCharacterRespawnFlowState2D detCharacterRespawnFlowState2D(const DetSessionCheckpoint& checkpoint,
    SimEntityId actor,const DetCharacterRespawnFlow2DConfig& config);
/// @brief Canonical mutable-global descriptor, with immutable actor copies.
DetTypedStateSchema detCharacterRespawnFlowStateSchema2D(const DetCharacterRespawnFlow2DConfig& config);
/// @brief All mutable descriptors: profile1 primary only, profile2 primary and external.
std::vector<DetTypedStateSchema> detCharacterRespawnFlowStateSchemas2D(const DetCharacterRespawnFlow2DConfig& config);
/// @brief Exact immutable policy descriptors for assembly/namespace preflight.
std::vector<DetTypedStateSchema> detCharacterRespawnFlowPolicySchemas2D(const DetCharacterRespawnFlow2DConfig& config);
/// @brief Resolve a field from its registered stable actor permission.
std::uint32_t detCharacterRespawnFlowField2D(const DetCharacterRespawnFlow2DConfig& config,
    SimEntityId actor,DetRespawnFlowField2D field);
/// @brief Resolve a profile2 member in externalStateSchema; profile1 rejects access.
std::uint32_t detCharacterRespawnFlowExternalField2D(const DetCharacterRespawnFlow2DConfig& config,
    SimEntityId actor,DetRespawnFlowExternalField2D field);
/// @brief Full selected-profile routing/nested Respawn identity, not a checkpoint codec.
std::vector<std::uint8_t> encodeDetCharacterRespawnFlowConfig2D(const DetCharacterRespawnFlow2DConfig& config);
} // namespace ayt::entity
