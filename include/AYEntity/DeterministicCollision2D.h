#pragma once
// design reference: AYEntity/design.md Stage26/28/29 step, motion policy and static slopes.
#include <AYEntity/DeterministicSession.h>
#include <AYMath/DetGeometry2D.h>
#include <AYMath/DetSlopeGeometry2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetCollision2DProfileVersion=1;
inline constexpr std::uint32_t kDetCollision2DExtendedProfileVersion=2;
inline constexpr std::uint32_t kDetCollision2DOneWayProfileVersion=3;
inline constexpr std::uint32_t kDetCollision2DStepProfileVersion=4;
inline constexpr std::uint32_t kDetCollision2DMotionProfileVersion=5;
inline constexpr std::uint32_t kDetCollision2DSlopeProfileVersion=6;
inline constexpr std::uint32_t kDetCollision2DMovingSlopeProfileVersion=7;
inline constexpr std::uint32_t kDetCollision2DTerrainProfileVersion=8;
enum class DetBodyMode2D : std::uint32_t {Disabled=0,Static=1,Kinematic=2,MovingObstacle=3};
enum class DetCollisionShape2D : std::uint32_t {Aabb=0,SlopeRisesRight=1,SlopeRisesLeft=2};
enum class DetCollisionMotionPolicy2D : std::uint32_t {Fault=0,StopPrescribedOnConflict=1};
enum class DetCollisionMotionResult2D : std::uint32_t {Clear=0,Blocked=1,CrushPrevented=2};
/// Stable field IDs in the adapter's typed body schema; callers write velocity in earlier systems.
enum DetBodyField2D : std::uint32_t {DetBodyMode=10,DetBodyHalf=20,DetBodyOffset=30,
    DetBodyVelocity=40,DetBodyLayer=50,DetBodyMask=60,DetBodyTrigger=70,
    DetBodyOneWay=80,DetBodyIgnoredOneWay=90,
    /// Profile4 facade-owned protocol; both fields are false at every committed boundary.
    DetBodyStepRequested=100,DetBodyStepped=110,
    /// Profile5 last accepted tick results; Clear has zero reference/axis/stamp.
    DetBodyMotionResult=120,DetBodyMotionOther=130,DetBodyMotionAxis=140,DetBodyMotionResultTick=150,
    DetBodyShape=160};
struct DetCollisionBody2D {
    DetBodyMode2D mode=DetBodyMode2D::Disabled;
    math::DetVec2 half{},offset{},velocity{};
    std::uint32_t layer=1,mask=UINT32_MAX;
    bool trigger=false;
    /// Profile3: solid Static/MovingObstacle top face; other faces are traversable.
    bool oneWay=false;
    /// Profile3: one stable one-way ID ignored by this solid Kinematic body.
    /// A retired identity is harmless; unknown/self/non-platform references are rejected.
    DetEntityRef ignoredOneWay{};
    /// Profile6: Static filled wedge. Profile7 additionally permits translating
    /// MovingObstacle wedges. Never Kinematic, one-way or trigger wedges.
    DetCollisionShape2D shape=DetCollisionShape2D::Aabb;
};
/** @brief Immutable registered collision policy, captured by value by the installed system.
 * @note Actor/history schema IDs share the session namespace. At most 64 active
 * bodies, 61 legacy pairs or 60 extended current+transient pairs; use scene limits.
 * System runs after velocity/input systems. History and policy are typed globals,
 * so checkpoint/restore/replay need no additional owner or persistent hidden cache.
 */
struct DetCollision2DConfig {
    std::uint32_t bodySchema=4,historySchema=5,systemId=30;
    std::int32_t priority=0;
    std::uint32_t eventType=0x30001,maxBodies=64,maxTriggerPairs=32;
    /// Opt in to profile 2: simultaneous relative sweeps, mutual kinematic response,
    /// prescribed moving obstacles and transient trigger Enter+Exit. Registers a
    /// pure semantic validator (ID=systemId) for seal/restore/tick boundaries.
    /// Profile 1 remains byte-compatible. Profile 2 allows at most 60 history pairs.
    bool extended=false;
    std::uint32_t maxContactIterations=128;
    /// Explicit profile3 opt-in, requires extended=true. Adds two registered body
    /// fields and new body/history versions; default profiles1/2 keep their bytes.
    /// Only relative downward top-plane crossings with strict X overlap block.
    /// From-below, side and already-inside motion is allowed; no penetration repair.
    /// Queries remain geometric AABB queries, independent of directional response.
    bool oneWayPlatforms=false;
    /// Profile4 static step-up policy, installed only by the character facade.
    /// Requires extended/oneWayPlatforms, finite 0<maxStepHeight<=1000 and <=59 pairs.
    /// Default false/zero preserves profiles1..3. No slopes or moving steps.
    /// One eligible candidate per tick, stable-ID sorted and selected by tick%count.
    /// Whole-world up/across/down trials use dt/4,dt/2,dt/4; each phase retains
    /// the ordinary contact-pass budget (baseline plus trial <=4*maxContactIterations).
    /// Other bodies move during each phase. Non-static/one-way/slope solid responses
    /// involving the stepping actor reject the trial; triggers use the chosen path.
    bool stepUp=false;
    math::DetFloat32 maxStepHeight{};
    /// Explicit profile5, requires extended and at most59 trigger pairs. Stops
    /// prescribed movers on ordinary MO/Static or MO/MO contact, or a proved
    /// direct opposed-face kinematic pinch. One whole-world retry from tick start
    /// freezes participating movers, retaining independent motion and the accepted
    /// continuous trigger path. Unclassified iteration/numeric/geometry failures
    /// still fault. A failed retry does not freeze more movers or partially commit.
    DetCollisionMotionPolicy2D motionPolicy=DetCollisionMotionPolicy2D::Fault;
    /// Explicit profile6. Ordinary solid Static wedges only, at most58 pairs;
    /// motionPolicy and static step-up remain independent opt-ins. Axis2 is the
    /// non-unit upward slope plane, not an AABB Y axis. Grounded facade characters
    /// split the whole-world path at the wedge's high-end surface transition;
    /// these events share the ordinary contact-pass budget. Old profiles are unchanged.
    bool staticSlopes=false;
    /// Explicit profile7, requires staticSlopes. Fixed wedge shapes may translate
    /// as ordinary MovingObstacle bodies; all sweeps and responses are relative.
    /// At most57 trigger pairs. No rotation, one-way or trigger wedges.
    bool movingSlopes=false;
    /// Explicit profile8, requires movingSlopes. Grounded support follows the
    /// continuous upper surface of adjacent walkable shapes in the sole solver.
    /// At most56 trigger pairs; jumps and airborne actors never attach implicitly.
    bool continuousTerrain=false;
};
struct DetCollisionProxy2D {
    SimEntityId id=0;
    math::DetAabb2 box;
    std::uint32_t layer=1,mask=UINT32_MAX;
    bool trigger=false;
    DetCollisionShape2D shape=DetCollisionShape2D::Aabb;
};
struct DetCollisionPair2D {
    SimEntityId first=0,second=0;
    friend auto operator<=>(const DetCollisionPair2D&,const DetCollisionPair2D&)=default;
};
/// Rebuilt sweep-and-prune, strict interior overlap and reciprocal masks;
/// results sorted by canonical stable-ID pair, independent of insertion order.
std::vector<DetCollisionPair2D> detCollisionPairs2D(std::span<const DetCollisionProxy2D> proxies);
/// Canonical public descriptor shared by facade helpers and checkpoint readers.
/// Invalid configurations throw; old profiles retain their exact descriptors.
DetTypedStateSchema detCollisionBodySchema2D(const DetCollision2DConfig& config);
/// Produce validated typed actor defaults/overrides; offset uses world XY only.
/// Active shapes require positive represented area; static velocity must be zero.
/// MovingObstacle needs extended=true and follows prescribed velocity without mass.
/// oneWay/ignoredOneWay require oneWayPlatforms=true; only supported body modes may set them.
/// Profile4 body defaults include facade-only request/result fields, both false;
/// this helper does not expose authoring of transient step protocol state.
std::vector<std::uint64_t> detCollisionBodyState2D(const DetCollision2DConfig& config,const DetCollisionBody2D& body);
/** @brief Install one stateless collision tick callback and its typed schemas/globals.
 * @note Configure before actors/seal; check false and abandon failed configuration.
 * Profile 1: kinematic/static sweep/slide, tick-end Enter/Stay/Exit, semantics next tick.
 * Opt-in profile 2: relative motion, equal kinematic normal sharing, prescribed
 * obstacle normal velocity, boundary semantic validation and transient Enter+Exit.
 * Events are pair/phase ordered and delivered next tick; disabled/despawned pairs Exit.
 * Opt-in profile3 adds one-way Static/MovingObstacle top planes and a registered
 * per-kinematic ignored platform. It permits passage through platform interiors;
 * ordinary solid penetration still faults. Relative downward crossings require
 * a start bottom at/above the top and strict X overlap at impact. Side/bottom
 * faces never block. No implicit epsilon or native-float arithmetic is used.
 * Initial ordinary penetration, unrepresentable motion, crushing/contact-pass exhaustion fault.
 * Time/axis/stable pair order is fixed; no tangent friction/carry. Scratch/capacity
 * checks precede collision pose/history writes. Session emit budget and prior writes
 * still require explicit restore on fault; no automatic rollback.
 * @pre Follow DeterministicSession World/Core ownership; one XY motion owner.
 * XY only; pose Z/rotation/scale do not affect shapes. No dynamic rigid-body solver.
 * @note stepUp=true is rejected before registration by this public installer.
 * The deterministic character facade owns profile4's transient request/result
 * protocol and clears both fields before the committed boundary. Profile4
 * queries still inspect full geometry and do not predict the step trajectory.
 * Profile5 commits typed last-tick motion results; initializer outputs are zero.
 * Only classified prescribed conflicts receive one safe retry. Original invalid
 * geometry, nonfinite/unrepresentable motion and resource errors are not recovery.
 * Ordinary one-way passages are unchanged and are not opposed-face pinch evidence.
 * @note Profile6 uses filled wedge narrowphase and separately rounded tangent projection.
 * Explicit movingSlopes/continuousTerrain select profiles7/8 (57/56 pair caps).
 * Moving wedge responses use the platform frame; true triangle/triangle prescribed
 * sweeps use SAT. Recoverable pinch still requires directly proved opposed flat
 * features; arbitrary diagonal crushing/chains fault. Profile8 follows grounded
 * walkable seams while preserving bounded own X; no implicit gap/jump attachment.
 * No capsule, curved/rotating slope, mass, restitution or surface-friction solver.
 */
bool installDetCollision2D(DeterministicSession& session,DetCollision2DConfig config={});
enum class DetTriggerPhase2D : std::uint32_t {Enter=1,Stay=2,Exit=3};
struct DetTriggerEvent2D {
    DetTriggerPhase2D phase=DetTriggerPhase2D::Enter;
    DetCollisionPair2D pair;
    friend bool operator==(const DetTriggerEvent2D&,const DetTriggerEvent2D&)=default;
};
/// Explicit 24-byte LE profile/phase/stable-ID pair, no object memory dumps.
std::vector<std::uint8_t> encodeDetTriggerEvent2D(const DetTriggerEvent2D& event);
/// Validate profile/phase/ordered nonzero IDs before replacing output.
bool decodeDetTriggerEvent2D(std::span<const std::uint8_t> bytes,DetTriggerEvent2D& event);
} // namespace ayt::entity
