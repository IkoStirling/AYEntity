#pragma once
#include <AYEntity/DeterministicCollision2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetCharacterController2DProfileVersion=1;
/// Applied once on release/jump with policy caps; direct support changes do not inherit the old carrier.
enum class DetCharacterLeaveVelocity2D : std::uint32_t {Discard=0,Inherit=1};
/// Stable fields; internal intent/carry fields are zero at every committed boundary.
/// SupportTick equals committed nextTick when grounded, otherwise zero; initial grounded state uses zero.
enum DetCharacterField2D : std::uint32_t {
    DetCharacterEnabled=10,DetCharacterGrounded=20,DetCharacterSupport=30,
    DetCharacterVelocity=40,DetCharacterSupportVelocity=50,DetCharacterTargetSpeed=60,
    DetCharacterJumpRequested=70,DetCharacterAppliedCarry=80,DetCharacterIntentVelocity=90,
    DetCharacterJumped=100,DetCharacterSnapped=110,DetCharacterSupportTick=120
};
/** @brief Immutable software-binary32 policy for registered axis-aligned XY character motion.
 * @note The facade installs profile2 collision and two callbacks in the same priority,
 * ordered preSystemId < collision.systemId < postSystemId. All schema/system/validator/
 * logic IDs and capacities are checked before registration; install before actors/seal.
 * Existing collision installation is not adopted. Use the included collision policy for
 * all body defaults and scene queries. Input callbacks run before preSystemId.
 * Policy values/defaults, IDs and profile enter the manifest. No mutable captures.
 * Gravity is a nonnegative downward acceleration; speed/acceleration limits are finite
 * [0,1,000,000], platform speed positive and <=1,000,000; distances <=1,000.
 * contactTolerance <= groundProbeDistance <= snapDistance. Snap distance zero disables
 * adhesion; all three distances must then be zero. No implicit epsilon is used.
 */
/// @brief Immutable registered motion and support policy, including the owned collision configuration.
struct DetCharacterController2DConfig {
    DetCollision2DConfig collision=[] {DetCollision2DConfig c;c.extended=true;return c;}();
    std::uint32_t stateSchema=6,policySchema=7,preSystemId=20,postSystemId=40,
        validatorId=40,logicProfileId=40;
    math::DetFloat32 maxHorizontalSpeed=math::DetFloat32::fromInt(8);
    math::DetFloat32 acceleration=math::DetFloat32::fromInt(20);
    math::DetFloat32 deceleration=math::DetFloat32::fromInt(30);
    math::DetFloat32 gravity=math::DetFloat32::fromInt(20);
    math::DetFloat32 jumpSpeed=math::DetFloat32::fromInt(8);
    math::DetFloat32 maxFallSpeed=math::DetFloat32::fromInt(64);
    math::DetFloat32 maxPlatformSpeed=math::DetFloat32::fromInt(1024);
    math::DetFloat32 groundProbeDistance=math::DetFloat32::fromInt(1)/math::DetFloat32::fromInt(16);
    math::DetFloat32 groundContactTolerance=math::DetFloat32::fromInt(1)/math::DetFloat32::fromInt(1024);
    math::DetFloat32 snapDistance=math::DetFloat32::fromInt(1)/math::DetFloat32::fromInt(8);
    DetCharacterLeaveVelocity2D leaveVelocity=DetCharacterLeaveVelocity2D::Discard;
};
/// Target horizontal own speed in world units/second; jumpPressed is a one-tick request, consumed even if airborne.
struct DetCharacterInput2D {
    math::DetFloat32 targetSpeed{};
    bool jumpPressed=false;
};
/** @brief Registered controller state; velocity excludes the active carrier's contribution.
 * @note Only enabled, solid Kinematic bodies are characters. Supports are reciprocal-mask
 * matching solid Static/MovingObstacle top faces with strictly positive horizontal
 * overlap; ties use gap then stable ID. Probe finds candidates; grounded additionally
 * requires gap <= explicit contactTolerance and nonascending relative vertical motion.
 * supportVelocity is the last observed full carrier velocity, never accumulated.
 * Own X is capped at +/- (maxHorizontalSpeed+maxPlatformSpeed), own Y at
 * +/- (jumpSpeed+maxFallSpeed+maxPlatformSpeed). These caps apply to inheritance
 * and solver response, including repeated landing/leave cycles. maxFallSpeed also
 * caps downward own velocity during the next pre gravity step. Collision's actual
 * solved pose is not changed by this bookkeeping cap.
 * Disabled controllers have canonical zero state and leave ordinary collision motion
 * untouched. Initial grounded state must name an alive valid top support, zero own Y
 * and matching support velocity. Ungrounded initialization at a floor is allowed;
 * the first pre callback can recognize contact and accept a jump immediately.
 * A support despawn queued in the tick is an explicit fresh retired-ID transition;
 * it releases on the following pre callback. Unknown/unissued support IDs are invalid.
 */
/// @brief Copyable registered enable/ground/support state and own/carrier software velocities.
struct DetCharacterState2D {
    bool enabled=false,grounded=false;
    DetEntityRef support{};
    math::DetVec2 velocity{},supportVelocity{};
};
/** @brief Install the registered controller, immutable policy/validator and profile2 collision as one motion pipeline.
 * @note Link AYEntity::DeterminismKernel, Determinism or DeterminismHost. No second pose
 * writer is installed: pre creates velocity intent, the existing relative collision
 * solver advances XY/continuous-trigger paths, post settles support and own velocity.
 * Snap only extends a downward intent toward the still-overlapping previous support;
 * it uses the same solver and is suppressed for jumping/rising characters. It is not
 * a teleport, a step-up or a prediction based on frozen query targets.
 * Nonnegative gravity is applied before a supported jump replaces own Y with
 * jumpSpeed. A zero jumpSpeed disables the request without dropping support;
 * zero gravity/maxFallSpeed are valid. Jump inheritance is added after this reset.
 * Full carrier velocity is added once per tick. Walls cannot turn blocked carry into
 * fictitious reverse own X velocity. Inherit transfers the last carrier velocity once
 * on release/jump, bounded by the documented own-speed caps; Discard removes it.
 * A direct support switch inherits neither old
 * carrier kick. Jump suppresses support recognition for that entire tick.
 * All persistent authority lives in typed state. Invalid restore is rejected atomically;
 * executing errors fault the Session and require explicit restore. An allocation failure
 * during registration can leave partial configuration: abandon a failed installation.
 * Bounds remain collision maxBodies<=64; no stairs, slopes, one-way platforms, crouch,
 * coyote/buffered jump, dynamic mass/friction, rotated/capsule shapes or 3D.
 */
/// @brief Install registered character intent/support systems and the sole profile2 XY collision motion owner.
bool installDetCharacterController2D(DeterministicSession& session,DetCharacterController2DConfig config={});
/// Validated actor block defaults/overrides; other actors keep the disabled zero defaults.
std::vector<std::uint64_t> detCharacterState2D(const DetCharacterController2DConfig& config,const DetCharacterState2D& state={});
/// Copy enabled/ground/support/own velocity from the current callback's typed actor state.
DetCharacterState2D detCharacterState2D(DetTickContext& context,SimEntityId actor,const DetCharacterController2DConfig& config);
/** @brief Set an enabled character's registered input before the pre callback.
 * @note Target is finite and within maxHorizontalSpeed. Target holds across ticks;
 * jump is consumed once by pre, including airborne rejection. This writes no pose.
 * Call only before the pre callback. Later input writes are not applied in that
 * tick; a late jump request fails the committed-boundary validator. Intent/carry/
 * jumped/snapped fields are private pipeline authority and must not be written by
 * application systems. Do not write body velocity after pre or pose outside the
 * sole collision motion owner. Application policy is responsible for callback order.
 * Invalid input throws before writes; do not catch a pure argument error and treat
 * that input as applied. Invalid typed-context access retains Session sticky failure.
 */
/// @brief Set bounded registered target speed and a consumed one-tick jump request before character pre.
void detCharacterInput2D(DetTickContext& context,SimEntityId actor,const DetCharacterController2DConfig& config,DetCharacterInput2D input);
} // namespace ayt::entity
