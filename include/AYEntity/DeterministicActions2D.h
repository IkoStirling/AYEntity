#pragma once
// design reference: AYEntity/design.md Stage33 registered direct actions.
#include <AYEntity/DeterministicPathMotion2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetActions2DProfileVersion=1;
enum class DetActionKind2D : std::uint32_t {
    CollisionEnable=1,CollisionDisable=2,PathEnable=3,PathDisable=4,
    PathPause=5,PathResume=6,PathRestart=7
};
enum class DetActionCode2D : std::uint32_t {
    None=0,Applied=1,Unchanged=2,UnknownTarget=3,Unsupported=4,MissingActor=5,
    Occupied=6,RestartAwayFromOrigin=7,BudgetExceeded=8
};
struct DetActionRequest2D {
    SimEntityId target=0;
    DetActionKind2D kind=DetActionKind2D::CollisionEnable;
};
struct DetActionResult2D {
    DetActionCode2D code=DetActionCode2D::None;
    /// Actual zero-based tick; only meaningful when sequence is nonzero.
    std::uint64_t tick=0;
    /// One-based global attempt count, including denials and budget denials.
    std::uint64_t sequence=0;
    DetActionRequest2D request{};
};
struct DetActionTarget2D {
    SimEntityId actor=0;
    /// Authorized enabled mask; body mask is always this nonzero value or zero.
    std::uint32_t collisionMask=UINT32_MAX;
    bool collisionControl=true;
    /// Optional already-installed bound path, using the same collision config.
    std::optional<DetPathMotion2DConfig> path;
};
/** @brief Fixed direct-action authority; install before actors and before seal.
 * No system or request queue is installed. All callers execute at collision.priority
 * before path pre (when present) and character pre. No pose is written here.
 * The table contains0..8 strictly increasing nonzero actor IDs; zero targets
 * grants no action authority and may bind pure Progress counters. Absent actors are
 * allowed and return MissingActor. Immutable canonical configuration is encoded
 * into at most8 typed policy chunks (at most8KiB), registered as globals and
 * actor copies, and checked at every Session boundary and action call.
 * Mutable authority is global only; all actor copies of stateSchema remain zero.
 */
struct DetActions2DConfig {
    DetCollision2DConfig collision;
    std::uint32_t stateSchema=64,policySchemaBase=65,validatorId=61,logicProfileId=61;
    /// 1..1024 accepted budget opportunities per tick. Further attempts record
    /// BudgetExceeded without changing the target; global sequence still advances.
    std::uint32_t maxActionsPerTick=64;
    std::vector<DetActionTarget2D> targets;
};
enum DetActionsField2D : std::uint32_t {
    DetActionsTotalCount=10,DetActionsBudgetTick=20,DetActionsTickCount=30,
    DetActionsLastCode=40,DetActionsLastTarget=50,DetActionsLastKind=60,
    DetActionsLastTick=70
};
/** @brief Register immutable action authority, global results and pure validators.
 * Check false and abandon a partially configured Session. Collision/path installers
 * may follow this installation, but must use the exact matching configuration.
 * Controlled bodies are ordinary AABB Static/MovingObstacle or AABB triggers;
 * unsupported Kinematic/one-way/wedge targets are refused by actions. Mask zero
 * disables reciprocal collision and geometric queries, without changing body mode.
 * Ordinary enabling is refused while reciprocal solid interior overlap exists;
 * one-way neighbours are treated conservatively as full solids. Trigger enabling
 * may overlap. Body shapes/roles and optional bound paths are validated at seal.
 */
bool installDetActions2D(DeterministicSession& session,DetActions2DConfig config);
/** @brief Execute synchronously before path/character pre, then record one result.
 * Normal denials do not fault or partly change a body/path. Invalid config, wrong
 * schema/policy, unknown kind, counter overflow and malformed context use throw.
 * Path actions preserve other controls and an earlier same-tick restart request.
 * Restart is allowed only at the represented path origin; it never teleports.
 * The next path pre clears ephemeral LastVelocity/blocked and settles controls.
 */
DetActionResult2D executeDetAction2D(DetTickContext& context,const DetActions2DConfig& config,DetActionRequest2D request);
/// @brief Last attempt, or None/sequence0 before the first action; this is not a queue.
DetActionResult2D detActionResult2D(DetTickContext& context,const DetActions2DConfig& config);
/// @brief Global attempt count, including normal denials and budget denials.
std::uint64_t detActionCount2D(DetTickContext& context,const DetActions2DConfig& config);
/// @brief Canonical global-state descriptor, useful for typed committed-state reads.
DetTypedStateSchema detActionsStateSchema2D(const DetActions2DConfig& config);
/// @brief Immutable registered chunk descriptors for dependent pure boundary validators.
std::vector<DetTypedStateSchema> detActionsPolicySchemas2D(const DetActions2DConfig& config);
/// @brief Profile1 canonical configuration identity bytes, not a replay/file parser.
/// Includes all IDs, collision options, table masks and optional full path policy.
std::vector<std::uint8_t> encodeDetActionsConfig2D(const DetActions2DConfig& config);
} // namespace ayt::entity
