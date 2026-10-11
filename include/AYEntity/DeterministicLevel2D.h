#pragma once
#include <AYEntity/DeterministicCharacterController2D.h>
#include <AYEntity/DeterministicPathMotion2D.h>
#include <AYEntity/DeterministicInteraction2D.h>
#include <AYEntity/DeterministicProgressFlow2D.h>
#include <AYEntity/DeterministicCharacterRespawnFlow2D.h>
#include <AYEntity/DeterministicVitality2D.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ayt::entity {
/// @brief Experimental initial-level semantic profile; existing simulation wire profiles are unchanged.
inline constexpr std::uint32_t kDetLevel2DProfileVersion=1;
/// @brief Extended authoring profile; format1 retains its original canonical bytes.
inline constexpr std::uint32_t kDetLevel2DInteractionProfileVersion=2;
/// @brief Complete counters/claims/automatic progress authoring profile; older bytes are unchanged.
inline constexpr std::uint32_t kDetLevel2DProgressProfileVersion=3;
/// @brief Checkpoint/death/manual respawn authoring; earlier semantic bytes are unchanged.
inline constexpr std::uint32_t kDetLevel2DRespawnProfileVersion=4;
/// @brief Bounded life/damage/accepted-respawn authoring; formats1..4 keep their bytes.
inline constexpr std::uint32_t kDetLevel2DVitalityProfileVersion=5;
/// @brief Explicit bounded periodic regions; formats1..5 retain their literal bytes.
inline constexpr std::uint32_t kDetLevel2DPeriodicVitalityProfileVersion=6;
/// @brief Explicit moving AABB trigger paths; formats1..6 keep their bytes.
inline constexpr std::uint32_t kDetLevel2DMovingTriggerProfileVersion=7;
/// @brief Bound on original ASCII source bytes, before decoding.
inline constexpr std::size_t kDetLevel2DMaxTextBytes=65536;
/// @brief Bound on complete field-wise semantic bytes entering the manifest.
inline constexpr std::size_t kDetLevel2DMaxCanonicalBytes=8192;
/// @brief An immutable named straight path; IDs are increasing nonzero resource identities.
struct DetLevelPath2D {
    std::uint32_t id=0;
    DetPathPlayback2D playback=DetPathPlayback2D::PingPong;
    std::vector<DetPathWaypoint2D> waypoints;
    /// Format7: explicit Path2 MovingObstacle/AABB/trigger role; false selects old Path1.
    bool movingTrigger=false;
};
/// @brief Initial XY placement and explicit body/character/path binding, never a checkpoint import.
struct DetLevelActor2D {
    SimEntityId id=0;
    math::DetVec2 position{};
    DetCollisionBody2D body;
    bool character=false;
    std::uint32_t path=0;
};
/// @brief Fixed action authority referring to an existing initial actor/path.
/// @note path0 grants no path controls. A nonzero path must equal that actor's
/// authored binding; its complete installed policy is resolved from the level.
struct DetLevelActionTarget2D {
    SimEntityId actor=0;
    std::uint32_t collisionMask=UINT32_MAX;
    bool collisionControl=true;
    std::uint32_t path=0;
};
/// @brief One actor's dedicated UInt64 HP counter and explicit restoration policy.
/// @note minimum is0; maximumHP>0 and initialHP<=maximumHP. Recovery off requires
/// restoredHP0/recoverManualfalse; recovery on requires restoredHP in[1,maximumHP].
struct DetLevelVitalityActor2D {
    SimEntityId actor=0;
    std::uint32_t counter=0;
    std::uint64_t maximumHP=1,initialHP=1;
    std::uint32_t invulnerabilityTicks=0;
    std::uint64_t restoredHP=0;
    bool recoverManual=false;
};
/// @brief Bounded initial level description; every authoritative float is a software binary32 value.
/// @note At most 64 active bodies and 8 named paths. Character policy includes the sole collision owner.
struct DetLevel2D {
    std::uint32_t applicationVersion=1,inputVersion=1,stepNumerator=1,stepDenominator=64;
    DetCharacterController2DConfig character;
    std::vector<DetLevelPath2D> paths;
    std::vector<DetLevelActor2D> actors;
    /// Explicit source/semantic version. Format1 forbids nondefault new fields.
    std::uint32_t formatVersion=kDetLevel2DProfileVersion;
    /// Format2: sorted fixed initial actors, at most8; empty requires rules empty.
    std::vector<DetLevelActionTarget2D> actionTargets;
    std::uint32_t maxActionsPerTick=64;
    /// Format2: sorted IDs, at most16; targets/roles must resolve within this level.
    std::vector<DetInteractionRule2D> rules;
    /// Format3: bounded sorted Progress tables; no counters requires all five tables empty.
    std::vector<DetProgressCounter2D> counters;
    std::vector<DetProgressObjective2D> objectives;
    std::vector<DetCollectibleClaim2D> claims;
    /// Format3: explicit automatic claim permissions and rising-objective actions.
    std::vector<DetProgressFlowClaimBinding2D> flowBindings;
    std::vector<DetProgressFlowRule2D> objectiveActions;
    /// Format4: sorted actor-specific spawn authority, at most8 entries.
    std::vector<DetRespawnSpawn2D> respawnSpawns;
    /// Format4:1..8 actor defaults when respawn is enabled;0 otherwise.
    std::vector<DetRespawnFlowBinding2D> respawnBindings;
    /// Format4:0..16 exact Enter permissions; empty allows manual-only routing.
    std::vector<DetRespawnFlowRegion2D> respawnRegions;
    /// Format5:0..8 sorted solid Kinematic actors, each with one distinct HP counter.
    std::vector<DetLevelVitalityActor2D> vitalityActors;
    /// Format5: exact direct Hurt/Heal permissions and real Enter bindings, at most16 each.
    std::vector<DetVitalityPermission2D> vitalityPermissions;
    std::vector<DetVitalityRegion2D> vitalityRegions;
    std::uint32_t maxVitalityOperationsPerTick=32;
    /// Explicit Vitality2/RespawnFlow2 acquisition; no implicit Manual restoration.
    bool vitalityRespawnRecovery=false;
    /// Format6 selects actual Vitality3; false preserves Enter-only1/2 behavior.
    bool periodicVitalityRegions=false;
};
/// @brief Application-owned registration ranges; adjust these before combining other schemas/systems.
/// @note Defaults reserve schemas 8..23 for paths,32..47 for content; path pre 11..18/post 41..48.
struct DetLevel2DOptions {
    std::uint32_t pathSchemaBase=8,pathPreSystemBase=11,pathPostSystemBase=41,
        pathValidatorBase=45,pathLogicBase=45,identitySchemaBase=32,
        identityValidatorId=60,identityLogicId=60;
    std::uint32_t actionsStateSchema=64,actionsPolicySchemaBase=65,
        actionsValidatorId=61,actionsLogicProfileId=61;
    std::uint32_t interactionStateSchema=80,interactionPolicySchemaBase=81,
        interactionSystemId=9,interactionValidatorId=62,interactionLogicProfileId=62;
    std::uint32_t progressStateSchema=96,progressPolicySchemaBase=97,
        progressValidatorId=64,progressLogicProfileId=64;
    std::uint32_t flowCollectStateSchema=112,flowObjectiveStateSchema=113,flowPolicySchemaBase=114,
        flowSystemId=8,flowValidatorId=65,flowLogicProfileId=65;
    std::uint32_t respawnStateSchema=136,respawnPolicySchema=137,
        respawnValidatorId=72,respawnLogicProfileId=72;
    std::uint32_t respawnFlowStateSchema=144,respawnFlowPolicySchemaBase=145,
        respawnFlowSystemId=7,respawnFlowValidatorId=73,respawnFlowLogicProfileId=73;
    std::uint32_t respawnFlowExternalStateSchema=168;
    std::uint32_t vitalityProgressStateSchema=176,vitalityProgressPolicySchemaBase=177,
        vitalityProgressValidatorId=76,vitalityProgressLogicProfileId=76;
    std::uint32_t vitalityStateSchema=192,vitalityPolicySchemaBase=193,
        vitalitySystemId=6,vitalityPostSystemId=62,vitalityValidatorId=75,vitalityLogicProfileId=75,
        vitalityRecoveryStateSchema=208;
    std::uint32_t vitalityRegionStateSchema=224;
};
/// @brief Strict ASCII AYDLEVEL1..7 decode with Float32 bits and UInt64 HP; failure leaves output unchanged.
/// @note Rejects unknown/trailing tokens, nonfinite values, invalid roles/policies and initial geometry.
bool decodeDetLevel2D(std::string_view text,DetLevel2D& output,std::string& error,
    const DetLevel2DOptions& options={});
/// @brief Read at most 64 KiB from a quiescent resource and validate before replacing output.
bool loadDetLevel2D(const std::filesystem::path& path,DetLevel2D& output,std::string& error,
    const DetLevel2DOptions& options={});
/// @brief Canonical human-readable encoding; throws invalid_argument on invalid level/options.
std::string encodeDetLevel2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
/// @brief Validate through a fresh owned Session, the actual factories/pipeline and initial seal.
bool validateDetLevel2D(const DetLevel2D& level,std::string& error,const DetLevel2DOptions& options={});
/// @brief Session/Host recipe identity from canonical level bytes, not native object memory or whitespace.
/// @note FNV64 is content identification, not authentication; full bytes additionally enter typed defaults.
DetSessionConfig detLevelSessionConfig(const DetLevel2D& level);
/// @brief Resolve the registered path binding used by configure; unknown IDs/options throw.
DetPathMotion2DConfig detLevelPathConfig2D(const DetLevel2D& level,std::uint32_t path,
    const DetLevel2DOptions& options={});
/// @brief Resolve complete registered Actions authority from format2..7 content.
/// @note Pure gameplay or HP counters may resolve a zero-target authority. Otherwise empty
/// action content, invalid references/options or unsupported controls throw.
DetActions2DConfig detLevelActionsConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
/// @brief Resolve complete registered Interaction policy from format2 rules.
/// @note Empty rules or invalid references/options throw; this does not install systems.
DetInteraction2DConfig detLevelInteractionConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
/// @brief Resolve complete registered Progress policy from format3 counter content.
/// @note Empty counters or invalid tables/options throw; zero-target Actions is valid for pure counters.
DetProgress2DConfig detLevelProgressConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
/// @brief Resolve complete registered Flow policy from format3 explicit bindings/rules.
/// @note Empty Flow tables or invalid references/options throw; unbound claims are not automatically collected.
DetProgressFlow2DConfig detLevelProgressFlowConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
/// @brief Resolve full format4 actor-specific spawn policy with selected registration IDs.
/// @note No respawn content or invalid references/options throw; this does not install a pipeline.
DetCharacterRespawn2DConfig detLevelRespawnConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
/// @brief Resolve full format4 checkpoint/death/manual routing, never a hidden retry queue.
/// @note Regions may be empty for manual-only permissions; matching Respawn is installed by configure.
DetCharacterRespawnFlow2DConfig detLevelRespawnFlowConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
/// @brief Resolve dedicated profile1 HP Progress sharing the complete Level Actions authority.
/// @note HP counters have no claims/objectives and are writable only through Vitality.
/// Empty vitality or invalid references/options throw; this does not install systems.
DetProgress2DConfig detLevelVitalityProgressConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
/// @brief Resolve complete Vitality1/2/3 policy, including selected Flow2 recovery authority.
/// @note Empty vitality or invalid tables/options throw. Recovery adds no pose writer
/// and only current matching Queued+Accepted restores; Manual requires its actor opt-in.
DetVitality2DConfig detLevelVitalityConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
/** @details Configure initial level authority in a fresh Session; does not seal or own the tick loop.
 * @note Set Host recipe.config=detLevelSessionConfig(level), then call this from recipe.configure.
 * Install an earlier canonical input system separately. Collision remains the sole XY pose writer.
 * All level bytes enter immutable typed-default/global chunks and a pure validator; changed content
 * rejects restore even if a 64-bit hash collides. Defaults also appear on actors and must stay unchanged.
 * Characters start enabled/airborne with zero own velocity; first pre recognizes actual support.
 * Bound paths start at actor.position, with zero velocity; no authored phase, support, motion result,
 * ignored ID, rotation or mutable cache is imported. All referenced paths must have a bound actor.
 * Validate first in a fresh owned Session. Destination registration failures may leave partial setup:
 * discard that Session, as for the existing facade installers. This is not a transactional reload.
 * The caller supplies a fresh, actor-free Session with matching application/input/step/content recipe;
 * earlier application schemas/systems must use nonconflicting registration IDs.
 * input version and dt are checked here. Standard Host retains tick ownership and presentation.
 * Format2 installs Actions, then optional Interaction, before the existing level pipeline.
 * Format3 additionally installs Progress and optional Flow before beforeActors.
 * Format4 retains those features and, when characterRespawn is true, installs
 * the fresh Respawn-owned controller/collision pipeline and RespawnFlow. Earlier
 * formats reject this flag and all respawn tables. Checkpoints are selected before
 * death/manual arbitration; death wins, one underlying request per actor/tick.
 * Put manual input before RespawnFlow, which precedes ProgressFlow/Interaction
 * and every path/character pre. Occupied/Unavailable/capacity denial has no hidden
 * retry; Stay never retries. The destination-only hook remains after all facades.
 * Format5 retains all earlier content and installs dedicated HP Progress1 plus
 * Vitality1, or explicit Vitality2/RespawnFlow2 recovery. One actual Vitality source
 * permission per sorted HP actor is derived into the full Flow2 policy. Canonical
 * input precedes Vitality, then RespawnFlow, then gameplay Flow/Interaction and pre.
 * Lethal HP is external Death, never Manual. Accepted restoration runs after all
 * character/path post systems, once per actor/tick; occupied/capacity denial keeps
 * HP unchanged and has no retry. Manual restoration requires recoverManual=true.
 * HP uses a separate Progress namespace, sharing Actions but no claims/objectives;
 * external writes into dedicated HP Progress invalidate Vitality closure. Public
 * Hurt/Heal budget and registered restoration counts remain distinct. Progress1
 * has no per-tick operation budget. Runtime factories and scratch seal bound all
 * optional schemas and nested complete policies; no simultaneous-maxima promise.
 * Format6 additionally appends an explicit periodic opt-in and each region's
 * period0..65535. Period0 stays Enter-only; nonzero uses Enter immediately and
 * only actual due Stay events. Exit clears that episode, with no timer retry.
 * Vitality3 region clocks are typed in the optional factory-selected schema224,
 * independent of HP recovery. No extra system or native overlap sampling occurs.
 * Formats1..5 reject new nondefault fields and keep all old bytes and profiles.
 * Format7 appends each path's explicit movingTrigger flag. True permits only
 * MovingObstacle AABB triggers with no one-way role; false keeps Path1 solid
 * bindings. Actions selects3 only for an explicitly authorized Path2 target.
 * Collision remains the sole XY writer and emits actual continuous trigger
 * events; path pause and trigger mask disable remain separate controls.
 * Formats1..6 reject this flag and retain their complete canonical bytes.
 * Entire tables and nested policies enter identity, not only a content hash.
 * Flow precedes Interaction and every path/character pre at collision.priority;
 * application Progress writes must precede Flow. Empty counters forbids objective,
 * claim and Flow tables. Pure counters still install matching zero-target Actions.
 * Table maxima do not guarantee a simultaneously valid combination: the full
 * 8KiB semantic limit, actual policy chunks and Session's64-schema/system/profile
 * budgets are checked by fresh installation/seal. Options determine registered
 * identity and are included by each complete facade policy, not by a callback.
 * Do not install a duplicate Actions/Interaction policy separately. Use the level getters
 * for canonical input callbacks. Only initial level IDs may be granted authority here;
 * direct facade capabilities for absent/dynamic actors remain a separate API contract.
 * beforeActors is an optional configuration-only registration hook, called on
 * the destination after facade/path installation and before content/actors.
 * Decode/validate/getters and their scratch Session never invoke it. It must
 * register application authority only, without sealing, actors or ticks; its
 * registered descriptors/profiles enter the Session manifest normally. The
 * function itself is not serialized or a runtime action callback. False/throw
 * requires discarding the partially configured destination; no reload rollback.
 * @brief Install bounded initial level state and its complete immutable content identity.
 * @note Use a fresh matching Session; registration failure requires discarding the destination.
 */
bool configureDetLevel2D(DeterministicSession& session,const DetLevel2D& level,std::string& error,
    const DetLevel2DOptions& options={},const std::function<bool(DeterministicSession&)>& beforeActors={});
} // namespace ayt::entity
