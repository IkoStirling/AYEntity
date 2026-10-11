#pragma once
#include <AYEntity/DeterministicCollision2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetPathMotion2DProfileVersion=1;
inline constexpr std::uint32_t kDetPathMotion2DTriggerProfileVersion=2;
enum class DetPathPlayback2D : std::uint32_t {Once=0,Loop=1,PingPong=2};
/// Relative position and outgoing travel duration. Backward travel uses the lower point's duration.
struct DetPathWaypoint2D {
    math::DetVec2 offset{};
    std::uint32_t travelTicks=1,dwellTicks=0;
};
/** @brief Immutable tick-counted path for registered MovingObstacle bodies.
 * @note Install alongside the sole collision owner, before actors/seal. The path
 * pre callback must precede character pre; post must follow character post. IDs
 * are distinct from collision and other systems. Collision is not installed here.
 * The policy, all waypoints, periods and IDs enter typed manifest state. Two to
 * 24 finite points are allowed; first offset is canonical zero. Travel counts are
 * 1..65535 and dwell counts 0..65535. Loop includes the last-to-first leg; there
 * is no teleport. Pause/disable keeps the cursor and writes zero prescribed velocity.
 * @note tickPeriod must bit-match the Session dt. Each sampled target must be exactly
 * reachable by software binary32 (velocity*dt plus position); otherwise the tick
 * faults, rather than silently drifting past an endpoint. Dyadic coordinates,
 * durations and dt provide straightforward authored paths.
 */
struct DetPathMotion2DConfig {
    DetCollision2DConfig collision;
    std::uint32_t stateSchema=8,policySchema=9,preSystemId=15,postSystemId=45,
        validatorId=45,logicProfileId=45;
    math::DetFloat32 tickPeriod=math::DetFloat32::fromInt(1)/math::DetFloat32::fromInt(60);
    DetPathPlayback2D playback=DetPathPlayback2D::PingPong;
    std::vector<DetPathWaypoint2D> waypoints;
    /// Explicit profile2 role: bound bodies are non-one-way AABB MovingObstacle
    /// triggers. False preserves every profile1 descriptor, policy and identity.
    /// This changes neither collision masks nor real trigger event semantics.
    bool movingTrigger=false;
};
enum DetPathMotionField2D : std::uint32_t {
    DetPathBound=10,DetPathEnabled=20,DetPathPaused=30,DetPathFinished=40,
    DetPathOrigin=50,DetPathWaypoint=60,DetPathDirection=70,DetPathElapsedTicks=80,
    DetPathDwellRemaining=90,DetPathLastVelocity=100,DetPathBlocked=110,
    DetPathRestartRequested=120,DetPathExpectedPosition=130,DetPathAttempted=140
};
/// Copyable registered phase/result. Factory authors only binding, controls and origin; phase starts at point0.
struct DetPathMotionState2D {
    bool bound=false,enabled=true,paused=false,finished=false,blocked=false;
    math::DetVec2 origin{},lastVelocity{};
    std::uint32_t waypoint=0,elapsedTicks=0,dwellRemaining=0;
    std::int32_t direction=1;
};
struct DetPathMotionInput2D {bool enabled=true,paused=false,restart=false;};
/** @brief Register velocity intent and post-collision progress, with no second pose writer.
 * @note By default bound actors must be nontrigger MovingObstacle bodies. The
 * explicit movingTrigger role instead requires MovingObstacle/AABB/trigger and
 * rejects one-way or ignored-one-way state. No application
 * may overwrite their velocity after path pre, or their pose outside collision.
 * A travel opportunity advances only when collision reached the requested target;
 * otherwise blocked=true and the cursor stays, so an explicit policy can stop or
 * reverse a blocked platform. The next opportunity retries from its actual pose.
 * A moving trigger receives no solid response or support/carry: blocked still
 * describes a sampled-target mismatch, including software arithmetic rounding.
 * On arrival, the target point's dwell counts complete subsequent zero-motion
 * ticks. Once finishes at the final point; PingPong turns without an extra move.
 * @note All counters, controls and results are checkpoint authority. No wall clock,
 * mutable callback capture or query cache participates. Legacy collision bytes
 * are unchanged. Failed installation must be abandoned; executing errors require
 * explicit Session restore. Bounds/shape validity remain collision's responsibility.
 */
/// @note With matching collision.movingSlopes, bindings also permit translating
/// ordinary wedge bodies. Path profile1 remains unchanged; collision owns shape motion.
bool installDetPathMotion2D(DeterministicSession& session,DetPathMotion2DConfig config);
/// @brief Read the selected canonical path state descriptor for typed committed-boundary consumers.
/// @note Returns the exact installed layout; it grants no path control or Session ownership.
DetTypedStateSchema detPathMotionStateSchema2D(const DetPathMotion2DConfig& config);
/// @brief Read the exact installed immutable policy descriptor, including every waypoint and the selected role.
/// @note Profile1 retains its old fields/defaults; profile2 adds the true role field12.
/// Consumers must bind this full descriptor, rather than copy a guessed profile1 layout or only its hash.
DetTypedStateSchema detPathMotionPolicySchema2D(const DetPathMotion2DConfig& config);
/// @brief Create canonical bound/unbound actor state at the first relative waypoint.
std::vector<std::uint64_t> detPathMotionState2D(const DetPathMotion2DConfig& config,const DetPathMotionState2D& state={});
/// @brief Copy a bound platform's registered controls, path phase and last accepted motion.
DetPathMotionState2D detPathMotionState2D(DetTickContext& context,SimEntityId actor,const DetPathMotion2DConfig& config);
/** @brief Set registered controls before path pre. Restart resets phase only at origin; it never teleports.
 * @note Invalid input/binding throws before writes. Restart away from origin is
 * rejected; use normal travel to return there. Missing input preserves controls.
 * Internal restart/expected/attempt fields are canonical zero at committed boundaries.
 */
void detPathMotionInput2D(DetTickContext& context,SimEntityId actor,const DetPathMotion2DConfig& config,DetPathMotionInput2D input);
} // namespace ayt::entity
