#pragma once
#include <AYEntity/DeterministicCharacterController2D.h>
#include <AYEntity/DeterministicPathMotion2D.h>
#include <AYEntity/DeterministicInteraction2D.h>
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
/// @brief Bound on original ASCII source bytes, before decoding.
inline constexpr std::size_t kDetLevel2DMaxTextBytes=65536;
/// @brief Bound on complete field-wise semantic bytes entering the manifest.
inline constexpr std::size_t kDetLevel2DMaxCanonicalBytes=8192;
/// @brief An immutable named straight path; IDs are increasing nonzero resource identities.
struct DetLevelPath2D {
    std::uint32_t id=0;
    DetPathPlayback2D playback=DetPathPlayback2D::PingPong;
    std::vector<DetPathWaypoint2D> waypoints;
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
};
/// @brief Strict ASCII AYDLEVEL1/2 decode with 8-hex-digit Float32 bits; failure leaves output unchanged.
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
/// @brief Resolve complete registered Actions authority from format2 targets.
/// @note Empty action content, invalid references/options or unsupported controls throw.
DetActions2DConfig detLevelActionsConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
/// @brief Resolve complete registered Interaction policy from format2 rules.
/// @note Empty rules or invalid references/options throw; this does not install systems.
DetInteraction2DConfig detLevelInteractionConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options={});
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
