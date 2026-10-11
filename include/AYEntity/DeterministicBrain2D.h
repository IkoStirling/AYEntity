#pragma once
// design reference: AYEntity/design.md Stage53 bounded registered enemy decisions.
#include <AYEntity/DeterministicCharacterController2D.h>
#include <AYEntity/DeterministicAttack2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetBrain2DProfileVersion=1;
inline constexpr std::uint32_t kDetBrain2DSightProfileVersion=2;
inline constexpr std::size_t kDetBrain2DMaxOwners=8,kDetBrain2DMaxPolicyBytes=18432;
enum class DetBrainMode2D : std::uint32_t {None=0,Patrol=1,Chase=2,AttackRequested=3,Busy=4,Dead=5,Retired=6,SightBlocked=7};
/// @brief One fixed character owner, primary HP target and center-based decision policy.
/// @note Speeds are nonnegative and bounded by character.maxHorizontalSpeed. Bounds
/// are absolute world X with left<right; they are direction changes, not pose clamps.
/// Center sensing is inclusive in XY; attackDistance is inclusive X within sense.x.
struct DetBrainOwner2D {
    SimEntityId actor=0,target=0;
    math::DetFloat32 patrolLeft=math::DetFloat32::fromInt(-4),patrolRight=math::DetFloat32::fromInt(4),
        patrolSpeed=math::DetFloat32::fromInt(1),chaseSpeed=math::DetFloat32::fromInt(2);
    math::DetVec2 senseHalfExtent{math::DetFloat32::fromInt(8),math::DetFloat32::fromInt(2)};
    math::DetFloat32 attackDistance=math::DetFloat32::fromInt(1);
    bool initialRight=true;
};
/** @brief Finite patrol/chase/attack decisions over separately installed matching facades.
 * @note Install matching Character/Attack before Brain and actors/seal; no dependency
 * is installed twice. Default order Brain3, external player input4, Attack5, Vitality6,
 * optional RespawnFlow7, character pre20. Other systems must not replace Brain-owned
 * character/attack inputs. It writes target speed and explicit false jump/drop/crouch,
 * plus held/facing Attack input, never pose, body velocity or HP. Zero target speed
 * stops the movement intent; real deceleration, carrier and gravity remain controller
 * policy. No navigation, wall/ledge avoidance, random/timer input or instant freeze.
 * Owners are sorted enabled solid Kinematic HP/Attack actors while present; disable
 * is outside this facade's first policy. Retirement is allowed. A fixed
 * target must be an allowed Attack target; the complete Attack whitelist remains its
 * actual damage authority. Missing/dead targets cause patrol. Retired owners release
 * Attack input without reading a missing character. Dead owners request zero motion.
 * Character-respawn requires matching recovery-enabled Attack Vitality/Flow policy,
 * allowing only a real current Accepted relocation to reset committed target speed.
 * At most8 owners; full literal policy<=18KiB/19 chunks and joint64 schemas apply.
 * Explicit lineOfSight selects profile2 and version2 state/policy/validator/logic;
 * false preserves profile1's complete descriptors, literal bytes and behavior.
 * SightBlocked7 is appended; Retired6 keeps its existing numeric value. There are
 * still at most121 mutable fields and no cached geometry or additional timer.
 * One immutable matching collision snapshot is captured before any owner input
 * write. For a present alive idle owner with a live target inside inclusive sense
 * bounds, its pose-XY center to target pose-XY center is a closed ray segment.
 * Actual owner layer/mask reciprocity applies, its own ID is ignored and trigger
 * proxies excluded. Target ID hits are skipped; any other full solid proxy hit
 * blocks sight, including endpoints/inside starts, ordinary Kinematics, geometric
 * one-way boxes and filled wedges. Moving proxies stay at this capture position;
 * no predicted path, motion response, directional one-way rule or partial hit list.
 * Blocked sight follows the same absolute patrol-bound direction/speed and releases
 * held attack. Busy/Dead/Retired precedence and all other decisions remain unchanged.
 * Sight gates idle decisions only: it does not cancel an already launched Attack,
 * add occlusion to its full target whitelist or change hit/HP authority.
 * Sample all owners and check arithmetic before writing Character/Attack inputs.
 * Pure restore validates recorded mode/center-range/input consistency and complete
 * identity; it cannot authenticate old visibility from a later moved checkpoint.
 */
/// @brief Immutable finite decisions and explicit nonconflicting registration IDs.
struct DetBrain2DConfig {
    DetCharacterController2DConfig character;
    DetAttack2DConfig attack;
    std::uint32_t stateSchema=272,policySchemaBase=273,systemId=3,validatorId=78,logicProfileId=78;
    std::vector<DetBrainOwner2D> owners;
    /// Explicit profile2 opt-in; false keeps profile1's complete bytes/behavior.
    bool lineOfSight=false;
};
/// Per sorted owner, stable fields are based at1000+200*index.
enum DetBrainField2D : std::uint32_t {
    DetBrainMode=10,DetBrainPatrolRight=20,DetBrainFacingRight=30,DetBrainDecisionCount=40,
    DetBrainLastDecisionTick=50,DetBrainDesiredSpeed=60,DetBrainDesiredAttackHeld=70,
    DetBrainObservedAttackPhase=80,DetBrainObservedAttackProcessedTick=90,
    DetBrainObservedOwnerAlive=100,DetBrainObservedTargetPresent=110,DetBrainObservedTargetAlive=120,
    DetBrainObservedOwnerPosition=130,DetBrainObservedTargetPosition=140,DetBrainAttackPulseCount=150
};
enum DetBrainGlobalField2D : std::uint32_t {DetBrainProcessedTick=1};
/// @brief Copied last Brain decision and its real pre-Attack observations.
/// @note At checkpoint nextTick N>0, observations were acquired by Brain at N-1;
/// observedAttackProcessedTick is N-1, describing the previous Attack callback.
/// It is not the later checkpoint's current Attack phase. Decision count increments
/// once per owner/tick, including Dead/Retired; a pulse is a request, not a hit.
/// Tick copies are zero-based and meaningful with decisionCount. SightBlocked
/// is a recorded pre-motion observation, not visibility at this later checkpoint
/// or proof of execution history; no checkpoint-time ray is substituted for it.
struct DetBrainState2D {
    DetBrainMode2D mode=DetBrainMode2D::None;
    bool patrolRight=true,facingRight=true,desiredAttackHeld=false;
    math::DetFloat32 desiredSpeed{};
    std::uint64_t decisionCount=0,lastDecisionTick=0,observedAttackProcessedTick=0,attackPulseCount=0;
    DetAttackPhase2D observedAttackPhase=DetAttackPhase2D::Idle;
    bool observedOwnerAlive=false,observedTargetPresent=false,observedTargetAlive=false;
    math::DetVec2 observedOwnerPosition{},observedTargetPosition{};
};
/// @brief Register one bounded stable-order decision callback and pure identity/state validator.
/// @note Dependency mismatch or registration failure requires abandoning the partial Session.
bool installDetBrain2D(DeterministicSession& session,DetBrain2DConfig config);
/// @brief Copy one owner's decision without retaining callback context or changing authority.
DetBrainState2D detBrainState2D(DetTickContext& context,const DetBrain2DConfig& config,SimEntityId owner);
DetBrainState2D detBrainState2D(const DetSessionCheckpoint& checkpoint,const DetBrain2DConfig& config,SimEntityId owner);
/// @brief Selected mutable descriptor;8 owners use121 fields and immutable actor defaults.
/// @note Profile1 is unchanged; only explicit sight uses descriptor version2.
DetTypedStateSchema detBrainStateSchema2D(const DetBrain2DConfig& config);
/// @brief Actual complete literal policy descriptors, not a guessed maximum allocation.
std::vector<DetTypedStateSchema> detBrainPolicySchemas2D(const DetBrain2DConfig& config);
/// @brief Resolve a stable owner field through its sorted fixed actor ID.
std::uint32_t detBrainField2D(const DetBrain2DConfig& config,SimEntityId owner,DetBrainField2D field);
/// @brief Complete selected Character descriptors, Attack policy and decision-table bytes.
std::vector<std::uint8_t> encodeDetBrainConfig2D(const DetBrain2DConfig& config);
}
