#pragma once
#include <AYEntity/DeterministicSession.h>
#include <AYMath/DetMatrix.h>

namespace ayt::entity {
enum class DetRigidMode3D:std::uint32_t {Disabled,Static,Kinematic,Dynamic};
enum class DetRigidShape3D:std::uint32_t {Sphere,Box,Capsule};
enum class DetJointKind3D:std::uint32_t {Distance,BallSocket,Fixed,Hinge,Slider};
/// Rigid3D profile2; changed schema/logic identities explicitly reject profile1 saves.
inline constexpr std::uint32_t kDetRigidBody3DProfileVersion=2;
/** @brief Registered binary32 rigid-body properties and force accumulators.
 * @note Sphere half components equal its radius; Box half components are local
 * half extents. Capsule uses{x=radius,y=half straight segment,z=radius}, localY
 * axis; y>=0, y=0 degenerates to a sphere. Uniform-density inertia is derived
 * from shape/mass and rotated into world space. Dynamic mass is positive.
 * Forces/torques clear only after a successful physics tick. Kinematic velocities
 * are prescribed, Static velocities/forces zero. Body shape has no native backend.
 */
struct DetRigidBody3D {
    DetRigidMode3D mode=DetRigidMode3D::Dynamic;
    DetRigidShape3D shape=DetRigidShape3D::Box;
    math::DetVec3 half=math::DetVec3::fromInts(1,1,1);
    math::DetFloat32 mass=math::DetFloat32::fromInt(1);
    math::DetVec3 velocity{},angularVelocity{},force{},torque{};
    math::DetFloat32 restitution{},friction=math::DetFloat32::fromBits(0x3f000000u);
    math::DetFloat32 linearDamping{},angularDamping{};
    std::uint32_t layer=1,mask=UINT32_MAX;
    bool trigger=false;
    /// Opt-in linear sphere CCD. Scene must contain only Static shapes and Dynamic
    /// spheres; no joints/Kinematic/Dynamic box/capsule or continuous trigger.
    /// Radius>=2*continuousSlop; separating initial skin contacts may advance by
    /// at most one slop during the remaining step. Geometry
    /// rotation is held during sweeps; sphere spin is unaffected. Bounded hit/cast
    /// exhaustion fails before physics publishes state, never silently tunnels.
    bool continuous=false;
};
/** @brief Immutable joint policy, increasing nonzero joint IDs and live actor IDs.
 * @note Anchors are local to each body. Distance enforces restLength; BallSocket
 * enforces coincident anchors; Fixed additionally enforces rotationB=rotationA*
 * referenceRotation. At least one endpoint must be Dynamic, no implicit world
 * endpoint. Hinge keeps anchors/axes coincident with free axis twist; Slider keeps
 * relative rotation and allows anchor travel along first local axis. Optional
 * limits/motors only for Hinge/Slider. No breakage or warm cache/unwrapped turns.
 */
struct DetRigidJoint3D {
    std::uint32_t id=0;
    DetJointKind3D kind=DetJointKind3D::Distance;
    SimEntityId first=0,second=0;
    math::DetVec3 anchorFirst{},anchorSecond{};
    math::DetFloat32 restLength{};
    math::DetQuaternion referenceRotation{};
    math::DetVec3 axisFirst=math::DetVec3::fromInts(0,1,0),axisSecond=math::DetVec3::fromInts(0,1,0);
    bool limits=false,motor=false;
    /// Hinge radians[-pi,pi] relative to referenceRotation; Slider signed anchor
    /// distance along first local axis. Motor target is rad/s or units/s, bounded
    /// torque/force per substep. Immutable config, not runtime servo/position targets.
    math::DetFloat32 lower{},upper{},targetSpeed{},maxMotorForce{};
};
/** @brief Opt-in bounded 3D physics over the registered Session, profile2.
 * @note Semi-implicit velocity integration, software quaternion rotation,
 * sphere/OBB narrow phase (15-axis SAT and clipped face contacts), accumulated
 * normal impulses and two-axis Coulomb friction, then joint/contact correction.
 * Stable actor/joint/pair/feature order and fixed iterations/substeps. Scratch
 * calculation precedes writes. Capsule closest-segment/OBB piecewise-distance
 * contacts, Hinge/Slider limits and velocity motors, optional linear sphere CCD.
 * No mesh/compound, general rotating CCD, sleeping/islands or native Jolt.
 * Bodies must have disjoint pose authority from character/other physics systems.
 */
struct DetRigidBody3DConfig {
    std::uint32_t bodySchema=600,policySchema=601,jointSchema=602,historySchema=603;
    std::uint32_t systemId=600,validatorId=600,logicProfileId=600,eventType=0x60001;
    std::int32_t priority=10;
    std::uint32_t maxBodies=32,maxContacts=128,maxTriggerPairs=32;
    std::uint32_t substeps=4,velocityIterations=8,positionIterations=4;
    std::uint32_t maxContinuousHits=8,maxSweepIterations=32;
    math::DetFloat32 continuousSlop=math::DetFloat32::fromBits(0x37800000u);
    math::DetVec3 gravity=math::DetVec3::fromInts(0,-10,0);
    math::DetFloat32 penetrationSlop=math::DetFloat32::fromBits(0x3a800000u);
    math::DetFloat32 correction=math::DetFloat32::fromBits(0x3e800000u);
    math::DetFloat32 restitutionThreshold=math::DetFloat32::fromBits(0x3f000000u);
    std::vector<DetRigidJoint3D> joints;
};
struct DetRigidContact3D {
    SimEntityId first=0,second=0;
    math::DetVec3 point{},normal{};
    math::DetFloat32 penetration{};
};
enum class DetRigidTriggerPhase3D:std::uint32_t {Enter=1,Stay=2,Exit=3};
struct DetRigidTrigger3D {DetRigidTriggerPhase3D phase;SimEntityId first,second;};
/// Matching body descriptor; defaults represent an inactive body.
DetTypedStateSchema detRigidBodySchema3D(const DetRigidBody3DConfig& config);
/// Complete fresh actor state; validated finite geometry and normalized rotation.
/// Previous/current pose start equal; there is no initial interpolation from origin.
DetActorState detRigidBodyActor3D(const DetRigidBody3DConfig& config,const DetRigidBody3D& body,
    math::DetVec3 position={},math::DetQuaternion rotation={});
/// Configure before actors/seal, install once; failure requires abandoning partial setup.
bool installDetRigidBody3D(DeterministicSession& session,DetRigidBody3DConfig config={});
/// Copy registered body state in a callback; does not mutate pose.
DetRigidBody3D detRigidBodyState3D(DetTickContext&,const DetRigidBody3DConfig&,SimEntityId);
/// Add finite force and world torque before physics; Dynamic only, checked before writes.
void detRigidBodyForce3D(DetTickContext&,const DetRigidBody3DConfig&,SimEntityId,math::DetVec3 force,math::DetVec3 torque={});
/// Rebuild final checkpoint geometric contacts; no tick, future sweep or cache.
std::vector<DetRigidContact3D> detRigidContacts3D(const DetSessionCheckpoint&,const DetRigidBody3DConfig&);
/// Linear sphere sweep against frozen checkpoint geometry; normal points actor
/// toward target, earliest fraction[0,1], target ID breaks ties. Uses configured
/// slop/iteration budget, reciprocal filters; sensors excluded unless requested.
/// Starts-overlapping returns0. NoHit=nullopt; invalid/budget throws, no mutation.
struct DetRigidSweepHit3D {SimEntityId actor=0,target=0;math::DetFloat32 fraction{};math::DetVec3 point{},normal{};};
/// @brief Pure configured sphere sweep; skin contacts count as hits, never modifies CP.
std::optional<DetRigidSweepHit3D> detRigidSphereSweep3D(const DetSessionCheckpoint&,const DetRigidBody3DConfig&,SimEntityId actor,math::DetVec3 displacement,bool includeTriggers=false);
/// Explicit 24-byte LE profile1 trigger; decode rejects before replacing output.
std::vector<std::uint8_t> encodeDetRigidTrigger3D(DetRigidTrigger3D);
bool decodeDetRigidTrigger3D(std::span<const std::uint8_t>,DetRigidTrigger3D&);
}
