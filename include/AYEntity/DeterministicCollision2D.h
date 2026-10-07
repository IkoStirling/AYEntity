#pragma once
#include <AYEntity/DeterministicSession.h>
#include <AYMath/DetGeometry2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetCollision2DProfileVersion=1;
inline constexpr std::uint32_t kDetCollision2DExtendedProfileVersion=2;
enum class DetBodyMode2D : std::uint32_t {Disabled=0,Static=1,Kinematic=2,MovingObstacle=3};
/// Stable field IDs in the adapter's typed body schema; callers write velocity in earlier systems.
enum DetBodyField2D : std::uint32_t {DetBodyMode=10,DetBodyHalf=20,DetBodyOffset=30,
    DetBodyVelocity=40,DetBodyLayer=50,DetBodyMask=60,DetBodyTrigger=70};
struct DetCollisionBody2D {
    DetBodyMode2D mode=DetBodyMode2D::Disabled;
    math::DetVec2 half{},offset{},velocity{};
    std::uint32_t layer=1,mask=UINT32_MAX;
    bool trigger=false;
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
};
struct DetCollisionProxy2D {
    SimEntityId id=0;
    math::DetAabb2 box;
    std::uint32_t layer=1,mask=UINT32_MAX;
    bool trigger=false;
};
struct DetCollisionPair2D {
    SimEntityId first=0,second=0;
    friend auto operator<=>(const DetCollisionPair2D&,const DetCollisionPair2D&)=default;
};
/// Rebuilt sweep-and-prune, strict interior overlap and reciprocal masks;
/// results sorted by canonical stable-ID pair, independent of insertion order.
std::vector<DetCollisionPair2D> detCollisionPairs2D(std::span<const DetCollisionProxy2D> proxies);
/// Produce validated typed actor defaults/overrides; offset uses world XY only.
/// Active shapes require positive represented area; static velocity must be zero.
/// MovingObstacle needs extended=true and follows prescribed velocity without mass.
std::vector<std::uint64_t> detCollisionBodyState2D(const DetCollision2DConfig& config,const DetCollisionBody2D& body);
/** @brief Install one stateless collision tick callback and its typed schemas/globals.
 * @note Configure before actors/seal; check false and abandon failed configuration.
 * Profile 1: kinematic/static sweep/slide, tick-end Enter/Stay/Exit, semantics next tick.
 * Opt-in profile 2: relative motion, equal kinematic normal sharing, prescribed
 * obstacle normal velocity, boundary semantic validation and transient Enter+Exit.
 * Events are pair/phase ordered and delivered next tick; disabled/despawned pairs Exit.
 * Initial penetration, unrepresentable motion, crushing/contact-pass exhaustion fault.
 * Time/axis/stable pair order is fixed; no tangent friction/carry. Scratch/capacity
 * checks precede collision pose/history writes. Session emit budget and prior writes
 * still require explicit restore on fault; no automatic rollback.
 * @pre Follow DeterministicSession World/Core ownership; one XY motion owner.
 * XY only; pose Z/rotation/scale do not affect shapes. No dynamic rigid-body solver.
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
