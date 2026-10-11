#pragma once
#include <AYEntity/DeterministicCharacterController2D.h>
#include <AYEntity/DeterministicPathMotion2D.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ayt::entity {
/// @brief Experimental initial-level semantic profile; existing simulation wire profiles are unchanged.
inline constexpr std::uint32_t kDetLevel2DProfileVersion=1;
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
/// @brief Bounded initial level description; every authoritative float is a software binary32 value.
/// @note At most 64 active bodies and 8 named paths. Character policy includes the sole collision owner.
struct DetLevel2D {
    std::uint32_t applicationVersion=1,inputVersion=1,stepNumerator=1,stepDenominator=64;
    DetCharacterController2DConfig character;
    std::vector<DetLevelPath2D> paths;
    std::vector<DetLevelActor2D> actors;
};
/// @brief Application-owned registration ranges; adjust these before combining other schemas/systems.
/// @note Defaults reserve schemas 8..23 for paths,32..47 for content; path pre 11..18/post 41..48.
struct DetLevel2DOptions {
    std::uint32_t pathSchemaBase=8,pathPreSystemBase=11,pathPostSystemBase=41,
        pathValidatorBase=45,pathLogicBase=45,identitySchemaBase=32,
        identityValidatorId=60,identityLogicId=60;
};
/// @brief Strict ASCII AYDLEVEL 1 decode with 8-hex-digit Float32 bits; failure leaves output unchanged.
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
 * @brief Install bounded initial level state and its complete immutable content identity.
 * @note Use a fresh matching Session; registration failure requires discarding the destination.
 */
bool configureDetLevel2D(DeterministicSession& session,const DetLevel2D& level,std::string& error,
    const DetLevel2DOptions& options={});
} // namespace ayt::entity
