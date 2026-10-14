#pragma once
#include <AYEntity/DeterministicSession.h>
#include <AYMath/DetMatrix.h>

namespace ayt::entity {
enum class DetRigidMode3D:std::uint32_t {Disabled,Static,Kinematic,Dynamic};
enum class DetRigidShape3D:std::uint32_t {Sphere,Box};
enum class DetJointKind3D:std::uint32_t {Distance,BallSocket,Fixed};
/** @brief Registered binary32 rigid-body properties and force accumulators.
 * @note Sphere half components equal its radius; Box half components are local
 * half extents. Dynamic mass is positive; Static/Kinematic mass is zero. Box and
 * sphere inertia are derived from shape and mass, then rotated into world space.
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
};
/** @brief Immutable joint policy, increasing nonzero joint IDs and live actor IDs.
 * @note Anchors are local to each body. Distance enforces restLength; BallSocket
 * enforces coincident anchors; Fixed additionally enforces rotationB=rotationA*
 * referenceRotation. At least one endpoint must be Dynamic, no implicit world
 * endpoint. Bounded iterative solver; no motors, limits, breakage or warm cache.
 */
struct DetRigidJoint3D {
    std::uint32_t id=0;
    DetJointKind3D kind=DetJointKind3D::Distance;
    SimEntityId first=0,second=0;
    math::DetVec3 anchorFirst{},anchorSecond{};
    math::DetFloat32 restLength{};
    math::DetQuaternion referenceRotation{};
};
/** @brief Opt-in bounded 3D physics over the registered Session, profile1.
 * @note Semi-implicit velocity integration, software quaternion rotation,
 * sphere/OBB narrow phase (15-axis SAT and clipped face contacts), accumulated
 * normal impulses and two-axis Coulomb friction, then joint/contact correction.
 * Stable actor/joint/pair/feature order and fixed iterations/substeps. Scratch
 * calculation precedes writes. Discrete substeps, no swept CCD/meshes/capsules,
 * sleeping/islands or native Jolt. Bounds do not promise scalable general physics.
 * Bodies must have disjoint pose authority from character/other physics systems.
 */
struct DetRigidBody3DConfig {
    std::uint32_t bodySchema=600,policySchema=601,jointSchema=602,historySchema=603;
    std::uint32_t systemId=600,validatorId=600,logicProfileId=600,eventType=0x60001;
    std::int32_t priority=10;
    std::uint32_t maxBodies=32,maxContacts=128,maxTriggerPairs=32;
    std::uint32_t substeps=4,velocityIterations=8,positionIterations=4;
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
/// Explicit 24-byte LE profile1 trigger; decode rejects before replacing output.
std::vector<std::uint8_t> encodeDetRigidTrigger3D(DetRigidTrigger3D);
bool decodeDetRigidTrigger3D(std::span<const std::uint8_t>,DetRigidTrigger3D&);
}
