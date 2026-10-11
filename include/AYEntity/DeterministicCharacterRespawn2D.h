#pragma once
// design reference: AYEntity/design.md Stage44 explicit tick-end relocation.
#include <AYEntity/DeterministicCharacterController2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetCharacterRespawn2DProfileVersion=1;
inline constexpr std::size_t kDetCharacterRespawn2DMaxSpawns=8;
/// @brief One immutable actor-specific XY destination; IDs are increasing and nonzero.
/// @note Z and rotation are preserved. Geometry is checked against the actual solved
/// world at execution, not against an authoring-time frozen query.
struct DetRespawnSpawn2D {std::uint32_t id=0;SimEntityId actor=0;math::DetVec2 position{};};
/** @brief Fresh opt-in character/collision pipeline with bounded registered spawn policy.
 * @note Explicitly set character.advanced=true, collision.extended=true,
 * collision.oneWayPlatforms=true and collision.characterRespawn=true. No unrelated
 * feature is enabled by this facade. Installs controller8/collision9 and respawn1;
 * ordinary installers reject characterRespawn. Install before actors/seal, never
 * after an existing collision/controller installation. Abandon failed registration.
 * All policy/spawn bytes enter typed defaults/global/actor copies and the manifest.
 * Mutable pending/result state is actor-local; no callback, pointer or hidden queue.
 * Only EndpointTransitions and RestoreCapturedStanding are supported. Normal
 * whole-world movement and trigger validation precede tick-end relocation. Rejected
 * destinations leave that baseline intact. Accepted relocation does not trace a
 * teleport chord; actual normal-motion trace and endpoint Enter/Exit are retained.
 * Reciprocal solids, including the full one-way thickness and real wedge interior,
 * block the standing destination. Contact without interior overlap is allowed.
 * Several actors resolve in stable actor-ID order against earlier accepted moves.
 * Spawn actor IDs must already name a solid Kinematic actor at initial seal;
 * later retired identities retain the immutable permission without a stale handle.
 */
/// @brief Full immutable spawn authority for a fresh sole-owner controller8/collision9 pipeline.
/// @note Explicit advanced/extended/one-way/respawn opt-ins and early requests are required.
struct DetCharacterRespawn2DConfig {
    DetCharacterController2DConfig character;
    std::uint32_t stateSchema=136,policySchema=137,validatorId=72,logicProfileId=72;
    std::vector<DetRespawnSpawn2D> spawns;
};
enum class DetRespawnRequestCode2D : std::uint32_t {Queued=0,AlreadyRequested=1,UnknownSpawn=2,Unavailable=3};
enum class DetRespawnResult2D : std::uint32_t {None=0,Accepted=1,Occupied=2,Unavailable=3,TriggerCapacity=4};
enum DetRespawnField2D : std::uint32_t {
    DetRespawnPendingSpawn=10,DetRespawnRequestTick=20,DetRespawnLastResult=30,
    DetRespawnLastSpawn=40,DetRespawnResultTick=50,DetRespawnApplied=60,DetRespawnPreCompleted=70
};
/// @brief Copied last resolved request; resultTick is committed nextTick, zero before a result.
struct DetCharacterRespawnState2D {DetRespawnResult2D result=DetRespawnResult2D::None;std::uint32_t spawn=0;std::uint64_t resultTick=0;};
/// @brief Install the sole character/collision motion owner and pure registered respawn closure.
bool installDetCharacterRespawn2D(DeterministicSession& session,DetCharacterRespawn2DConfig config);
/** @brief Queue at most one actor-specific request before character pre; no pose is written.
 * @note First valid request wins per actor/tick. Normal denials do not retry or
 * queue hidden work. Wrong policy or late pipeline use throws and faults the tick.
 * Successful collision commit restores captured standing shape, clears own/body
 * velocity, ground/support/carry, jump/drop/coyote/buffer/step/motion outputs and
 * target speed. Current held jump/drop samples remain latched as consumed, avoiding
 * a false rising edge on the next tick. Support is evaluated by the next real pre
 * under ordinary relative-motion rules; immediate attachment is not guaranteed.
 * Interpolation current/previous become the destination with previous flag false;
 * rotation is preserved. Pending/applied protocol is zero at committed boundaries.
 */
DetRespawnRequestCode2D detCharacterRespawnRequest2D(DetTickContext& context,SimEntityId actor,
    const DetCharacterRespawn2DConfig& config,std::uint32_t spawnId);
/// @brief Read the actor's copied last result, without retaining callback context.
DetCharacterRespawnState2D detCharacterRespawnState2D(DetTickContext& context,SimEntityId actor,const DetCharacterRespawn2DConfig& config);
/// @brief Read a compatible full checkpoint's copied result; this does not execute or restore it.
DetCharacterRespawnState2D detCharacterRespawnState2D(const DetSessionCheckpoint& checkpoint,SimEntityId actor,const DetCharacterRespawn2DConfig& config);
} // namespace ayt::entity
