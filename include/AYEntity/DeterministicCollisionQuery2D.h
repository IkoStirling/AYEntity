#pragma once
#include <AYEntity/DeterministicCollision2D.h>

namespace ayt::entity {
inline constexpr std::uint32_t kDetCollisionQuery2DProfileVersion=1;

/** @brief Reciprocal layer/mask and stable identity filtering for a scene query.
 * @note layer must be nonzero; mask zero intentionally matches nothing. Triggers
 * are included by default. ignore=0 ignores no actor. Filtering cannot conceal an
 * invalid body: capture validates every active body before any query runs.
 */
struct DetCollisionQueryFilter2D {
    std::uint32_t layer=1,mask=UINT32_MAX;
    SimEntityId ignore=0;
    bool includeTriggers=true;
};
/// @brief Stable actor identity and an owned software-geometry scene-query hit.
struct DetCollisionQueryHit2D {
    SimEntityId entity=0;
    math::DetHit2D hit;
};

/** @brief Immutable, bounded snapshot of registered deterministic XY collision bodies.
 * @note Link AYEntity::Determinism or DeterminismKernel. Capture in a system callback
 * from its DetTickContext with the same installed collision policy, or at a quiescent
 * boundary from a Session checkpoint. The checkpoint overload checks manifest numeric
 * profiles, body/history field IDs/types/versions and pose snapshot profiles. Policy
 * globals, typed body lanes and represented
 * XY bounds are validated; Disabled bodies are omitted. At most policy.maxBodies/64
 * active bodies are retained. Invalid inputs or unrepresentable XY bounds/coordinate
 * differences throw; interval quotients may use software infinity for classification.
 * This validates collision data and its schema identity, not application/content/code
 * compatibility or arbitrary game validators; acquire checkpoints from the matching
 * Session. Decoding a file alone does not establish that application's compatibility.
 *
 * Capture copies values and retains no World/context pointers or hidden mutable cache.
 * System (priority,id) order determines which writes a callback capture sees. Queries
 * do not change pose, RNG, history or events. Capture again after movement or restore;
 * a previous snapshot keeps its original values. Z, rotation and scale do not affect
 * shapes; offsets are in world XY. Sweep uses frozen target boxes, including moving
 * obstacles, and does not predict their subsequent movement or solve response.
 *
 * overlap returns all matching stable IDs in ascending order. Ray/sweep hits are
 * ordered by (software fraction, axis, stable ID); X wins exact entry-time ties.
 * Ray is a closed segment; inside/boundary starts hit at zero with zero normal.
 * Sweep requires positive area and uses strict blocking semantics from AYMath:
 * initial penetration is reported, inward face entry blocks, grazing/outward motion
 * does not. Signed zeros follow software numeric comparison. There is no epsilon,
 * truncation, native physics dependency or arbitrary component serialization.
 */
/// @brief Capture registered XY collision values and return stable ordered query results.
/// @note Pure query errors caught by the caller do not establish a Session fault;
/// invalid DetTickContext accesses retain that context's sticky failure semantics.
class DetCollisionQuery2D {
public:
    explicit DetCollisionQuery2D(DetTickContext& context,DetCollision2DConfig config={});
    explicit DetCollisionQuery2D(const DetSessionCheckpoint& state,DetCollision2DConfig config={});
    std::size_t size() const noexcept {return _proxies.size();}
    /// Closed overlap by default; interior=true excludes mere face/corner touching.
    std::vector<SimEntityId> overlap(const math::DetAabb2& box,DetCollisionQueryFilter2D filter={},bool interior=false) const;
    std::vector<DetCollisionQueryHit2D> raycast(math::DetVec2 origin,math::DetVec2 displacement,DetCollisionQueryFilter2D filter={}) const;
    std::vector<DetCollisionQueryHit2D> sweep(const math::DetAabb2& box,math::DetVec2 displacement,DetCollisionQueryFilter2D filter={}) const;
private:
    std::vector<DetCollisionProxy2D> _proxies;
};
} // namespace ayt::entity
