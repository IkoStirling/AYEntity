#pragma once
#include <AYEntity/DeterministicCollision2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetCharacterController2DProfileVersion=1;
inline constexpr std::uint32_t kDetCharacterController2DAdvancedProfileVersion=2;
inline constexpr std::uint32_t kDetCharacterController2DStepProfileVersion=3;
inline constexpr std::uint32_t kDetCharacterController2DMotionProfileVersion=4;
inline constexpr std::uint32_t kDetCharacterController2DSlopeProfileVersion=5;
/// Applied once on release/jump with policy caps; direct support changes do not inherit the old carrier.
enum class DetCharacterLeaveVelocity2D : std::uint32_t {Discard=0,Inherit=1};
/// Stable fields; internal intent/carry fields are zero at every committed boundary.
/// SupportTick equals committed nextTick when grounded, otherwise zero; initial grounded state uses zero.
enum DetCharacterField2D : std::uint32_t {
    DetCharacterEnabled=10,DetCharacterGrounded=20,DetCharacterSupport=30,
    DetCharacterVelocity=40,DetCharacterSupportVelocity=50,DetCharacterTargetSpeed=60,
    DetCharacterJumpRequested=70,DetCharacterAppliedCarry=80,DetCharacterIntentVelocity=90,
    DetCharacterJumped=100,DetCharacterSnapped=110,DetCharacterSupportTick=120,
    /// Advanced profile2 only. Remaining windows count future pre opportunities.
    DetCharacterCoyoteRemaining=130,DetCharacterJumpBufferRemaining=140,
    DetCharacterJumpHeld=150,DetCharacterDropHeld=160,DetCharacterCrouchHeld=170,
    DetCharacterDropStarted=180,DetCharacterCrouched=190,DetCharacterStandingCaptured=200,
    DetCharacterStandingHalf=210,DetCharacterStandingOffset=220,DetCharacterDropRemaining=230,
    DetCharacterCoyoteSupportY=240,DetCharacterJumpLatch=250,DetCharacterDropLatch=260,
    DetCharacterDropTick=270,
    /// Step profile3 only: result of the last committed tick, cleared by next pre.
    /// True requires enabled/grounded with zero carrier and body Y velocity.
    DetCharacterStepped=280,
    /// Slope profile5: committed nonunit support normal; zero when airborne/disabled.
    DetCharacterGroundNormal=290
};
/** @brief Immutable software-binary32 policy for registered axis-aligned XY character motion.
 * @note The facade installs profile2 (legacy), profile3 (advanced), or explicit
 * profile4 (static step-up) collision and
 * two callbacks in the same priority,
 * ordered preSystemId < collision.systemId < postSystemId. All schema/system/validator/
 * logic IDs and capacities are checked before registration; install before actors/seal.
 * Advanced and oneWayPlatforms must be enabled together. Legacy mode requires zero
 * new timer options, crouchEnabled=false and the default crouchHeightScale.
 * Existing collision installation is not adopted. Use the included collision policy for
 * all body defaults and scene queries. Input callbacks run before preSystemId.
 * Policy values/defaults, IDs and profile enter the manifest. No mutable captures.
 * Gravity is a nonnegative downward acceleration; speed/acceleration limits are finite
 * [0,1,000,000], platform speed positive and <=1,000,000; distances <=1,000.
 * contactTolerance <= groundProbeDistance <= snapDistance. Snap distance zero disables
 * adhesion; all three distances must then be zero. No implicit epsilon is used.
 * With advanced=true, collision.stepUp=true and a positive collision.maxStepHeight
 * explicitly select controller3/collision4. Default profiles and bytes are unchanged.
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
    /// Explicit profile2 controller/state/policy opt-in. Requires collision.extended
    /// and collision.oneWayPlatforms; false preserves the Stage21 schema/fingerprint.
    bool advanced=false;
    /// Coyote N gives N pre opportunities after leaving contact. Buffer N gives
    /// the press pre plus N future pre opportunities. All counts are <=65535.
    std::uint32_t coyoteTicks=0,jumpBufferTicks=0,dropThroughTicks=0;
    /// Crouch scales standing halfY and adjusts offsetY to keep the same foot plane.
    /// Scale is finite and strictly between zero and one, even when disabled.
    bool crouchEnabled=false;
    math::DetFloat32 crouchHeightScale=math::DetFloat32::fromInt(1)/math::DetFloat32::fromInt(2);
    /// Static-slope profile only: maximum rise/run in (0,16]. Steeper surfaces slide and cannot be walked uphill.
    /// Default1 is also required when staticSlopes is off, keeping old policy bytes unchanged.
    math::DetFloat32 maxSlopeRisePerRun=math::DetFloat32::fromInt(1);
};
/// Target horizontal own speed in world units/second. Legacy jump is a one-tick request.
/// Advanced jumpPressed/dropPressed sample held buttons: only a registered rising
/// edge requests an action; explicitly submit false to release. Missing input keeps
/// held samples, so replaying or holding true cannot repeat the same press.
struct DetCharacterInput2D {
    math::DetFloat32 targetSpeed{};
    bool jumpPressed=false;
    bool crouchHeld=false,dropPressed=false;
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
    /// Advanced authoring/copy state. Disabled or legacy actors use zero defaults.
    /// Uncaptured standing shape is allowed only at initial tick0 with all advanced
    /// state zero. First pre captures body half/offset. Dynamic spawn/enabling must
    /// supply captured, valid standingHalf/standingOffset and a matching body shape.
    bool crouched=false,standingCaptured=false;
    math::DetVec2 standingHalf{},standingOffset{};
    std::uint32_t coyoteRemaining=0,jumpBufferRemaining=0,dropRemaining=0;
    bool jumpHeld=false,dropHeld=false,crouchHeld=false,jumpLatch=false,dropLatch=false;
    math::DetFloat32 coyoteSupportY{};
    std::uint64_t dropTick=0;
    /// Profile3 output only: this tick accepted a static step and settled support.
    /// Actor initialization/spawn must leave this false. Read committed copies for presentation.
    bool stepped=false;
    /// Slope profile only. For grounded initialization supply the actual flat/slope normal.
    math::DetVec2 groundNormal{};
};
/** @brief Install the registered controller, immutable policy/validator and relative collision as one motion pipeline.
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
 * Bounds remain collision maxBodies<=64; no dynamic mass/friction,
 * rotated/capsule shapes or 3D. Legacy/advanced profiles do not step up.
 * Explicit collision.motionPolicy selects controller4/collision5 for recoverable
 * prescribed-obstacle blocking and directly proved crushing conflicts. Only the
 * participating prescribed movers stop, with one whole-world retry; continuous
 * carrier intent is removed in that retry. Typed collision results describe the
 * accepted tick. Invalid geometry, iteration exhaustion without a classified
 * conflict and failed retries still fault the Session.
 * Explicit collision.staticSlopes selects controller5/collision6 and filled solid
 * Static triangular wedges. The highest foot corner defines support height;
 * maxSlopeRisePerRun limits walkable rise/run. Grounded walking follows the
 * tangent through collision's sole motion owner, including downhill adhesion;
 * jumping releases that support. Steeper surfaces cannot be walked uphill and
 * respond with sliding. Committed groundNormal is the wedge's raw upward normal
 * (also on its highest-foot top segment), (0,1) for an AABB flat support, and
 * zero while airborne/disabled. State own Y remains
 * zero while grounded; the body velocity carries the actual tangent motion.
 * Crouch clearance uses real wedge interior, and step trials exclude slope shapes.
 * Profile6 allows at most58 trigger pairs. New profiles/policies have explicit
 * manifest identities; existing default profiles and state bytes remain unchanged.
 * Explicit collision.stepUp selects controller3/collision4 and allows supported
 * walking to try ordinary solid Static AABB steps up to collision.maxStepHeight.
 * No jump/drop/rising or MovingObstacle/one-way support requests a step. The sole
 * collision owner tries an entire bounded world path in scratch; blocked clearance,
 * no improved forward motion, absent exact landing or dynamic solid response keeps
 * the ordinary solved path. Trigger paths follow the selected physical segments.
 * At most one candidate is tried per tick, selected from stable-ID order using the
 * registered tick. Other bodies continue their relative motion during all segments.
 * Step profile requires maxTriggerPairs<=59; old profiles retain their capacities.
 * Advanced profile2 adds tick-counted coyote/buffer windows, held-button edge latches,
 * single ignored one-way support and foot-preserving crouch. Coyote jumping inherits
 * no second carrier X kick; Inherit adds the recorded carrier Y to jumpSpeed.
 * If snap still retained an active carrier, jump transfers its current velocity once.
 * A buffered jump consumes its request once. Active drop wins over jump and clears
 * both windows; only a grounded one-way support can be dropped. The ignored ID lives
 * in DetBodyIgnoredOneWay, for at least max(1,dropThroughTicks) solver ticks and until
 * the body has completely left its thickness (or horizontal extent), checked in pre.
 * Queued target despawn is a fresh DropTick transition and releases on next pre.
 * Disabling advanced controllers clears all controller state and their owned ignored ID.
 * Crouch/stand change half and offset only. Standing requires no strict interior
 * overlap with any current reciprocal-mask solid, including ignored/one-way bodies;
 * blocked standing keeps the entire crouch shape. Both posture transitions also
 * require a represented positive box and bit-identical world foot plane. If binary32
 * cannot represent that change, the whole old posture is retained; crouchHeld is a
 * request and need not equal crouched. No ULP search or pose correction is performed.
 * Stable ID order resolves concurrent
 * shape changes. No query cache or second pose writer is used.
 * Legacy mode rejects advanced options and crouch/drop input; its 12 state/24 policy
 * fields, profile1 identity and bytes remain unchanged. Advanced schema/validator/
 * logic profile2 and collision profile3 are explicitly incompatible identities.
 * Step profile3 adds a committed stepped result and immutable step policy; collision4
 * request/result fields are internal and zero at every committed boundary.
 */
/// @brief Install registered character intent/support systems and the sole relative XY collision motion owner.
bool installDetCharacterController2D(DeterministicSession& session,DetCharacterController2DConfig config={});
/// Validated actor block defaults/overrides; other actors keep the disabled zero defaults.
std::vector<std::uint64_t> detCharacterState2D(const DetCharacterController2DConfig& config,const DetCharacterState2D& state={});
/// Copy enabled/ground/support/own velocity from the current callback's typed actor state.
DetCharacterState2D detCharacterState2D(DetTickContext& context,SimEntityId actor,const DetCharacterController2DConfig& config);
/** @brief Set an enabled character's registered input before the pre callback.
 * @note Target is finite and within maxHorizontalSpeed. Target holds across ticks;
 * legacy jump is consumed once by pre, including airborne rejection. In advanced
 * mode jump/drop held samples persist and their latches are registered; crouchHeld
 * persists too. Buffer/coyote/drop counts use simulation ticks, never wall time.
 * This writes no pose.
 * Call only before the pre callback. Later input writes are not applied in that
 * tick; a late legacy jump or changed advanced held/latch relationship fails the
 * committed-boundary validator. Intent/carry/
 * jumped/snapped fields are private pipeline authority and must not be written by
 * application systems. Do not write body velocity after pre or pose outside the
 * sole collision motion owner. Application policy is responsible for callback order.
 * Invalid input throws before writes; do not catch a pure argument error and treat
 * that input as applied. Invalid typed-context access retains Session sticky failure.
 */
/// @brief Set bounded registered target speed and legacy jump or advanced held samples before character pre.
void detCharacterInput2D(DetTickContext& context,SimEntityId actor,const DetCharacterController2DConfig& config,DetCharacterInput2D input);
} // namespace ayt::entity
