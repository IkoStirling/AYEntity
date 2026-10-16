#pragma once
#include <AYEntity/IEntity.h>
#include <AYEntity/CameraFrame.h>
#include <functional>
#include <unordered_map>
#include <vector>

namespace ayt::entity {
class World;
class Entity;
/// Sweep a camera sphere from the anchor to desired eye; return unobstructed center distance.
/// The callback executes on the presentation owner thread and must not mutate/destroy the World.
struct CameraCollisionProbe {
    math::FVector3 anchor{}, desiredEye{};
    float radius = .2f;
    const Entity* cameraEntity = nullptr;
    const Entity* targetEntity = nullptr;
    uint32_t layerMask = 0xFFFFFFFFu;
    bool ignoreTarget = true;
};
using CameraCollisionQuery = std::function<float(const CameraCollisionProbe&)>;

/// World-owned presentation control and independent transitions per viewport/domain.
/// Standard Entity.Runtime installs it; standalone/headless callers use registerCameraRuntimeSystem.
class CameraRuntimeSystem : public ISystem {
public:
    const char* getName() const override { return "CameraRuntimeSystem"; }
    void onUpdate(float dt) override;
    /// Custom sphere-sweep adapter. Empty restores the integration fallback; collisionEnabled disables per controller.
    void setCollisionQuery(CameraCollisionQuery query) { _collision = std::move(query); }
    /// Integration fallback; a custom setCollisionQuery callback takes precedence.
    /// The adapter resolves its external service for each call and must not retain a World pointer.
    void setDefaultCollisionQuery(CameraCollisionQuery query) { _defaultCollision = std::move(query); }
    /// Apply transition once per presentation tick, even if renderer samples repeatedly.
    CameraFrame present(const CameraFrame& frame, int32_t viewportId, bool twoD,
                        float duration, bool smooth = true);
    /// Reset a domain when no selected camera exists, avoiding stale transitions on reappearance.
    void resetView(int32_t viewportId, bool twoD);
    /// Clear presentation snapshots/transitions after a Scene reload in the same World.
    void resetAllViews() noexcept { _transitions.clear(); }
    /// Copy the last Scene presentation snapshot for picking/UI during transitions. False leaves output unchanged.
    /// Host/Editor overrides use their own camera snapshots, not this World-owned Scene source.
    /// Before first extraction, evaluate the lens directly; a snapshot is the last sampled presentation.
    bool snapshot(int32_t viewportId, bool twoD, CameraFrame& output) const noexcept;
    /// Constrain a composed lens/effect snapshot too, so local offsets/shake cannot bypass the sweep.
    void constrainFrame(const Entity& entity, CameraFrame& frame) const;
    struct DebugCollision {uint32_t entityId=0;math::FVector3 anchor{},desiredEye{},resolvedEye{};float radius=0,allowed=0;bool valid=false;};
    struct DebugView {int32_t viewport=0;bool twoD=false;CameraFrame frame;float progress=1,duration=0;};
    struct DebugSnapshot {std::vector<DebugCollision> collisions;std::vector<DebugView> views;};
    /// Owner-thread copied last real probes/transitions. Never invokes the collision adapter.
    DebugSnapshot debugSnapshot() const;
    void setDebugCaptureEnabled(bool enabled) noexcept { _debugCapture=enabled;if(!enabled)_debugCollisions.clear(); }
    void clearDebugHistory() noexcept { _debugCollisions.clear(); }
    static constexpr int kPriority = 399;
private:
    friend CameraRuntimeSystem& cameraRuntime(World& world);
    bool _debugCapture=false;
    mutable std::vector<DebugCollision> _debugCollisions;
    void observeCollision(DebugCollision value) const noexcept;
    World* _owner = nullptr;
    struct TransitionState { CameraTransition transition; uint64_t tick = UINT64_MAX; };
    std::unordered_map<uint64_t, TransitionState> _transitions;
    CameraCollisionQuery _collision;
    CameraCollisionQuery _defaultCollision;
    uint64_t _tick = 0;
    float _dt = 0;
};
/// Idempotently acquire/install the World-owned camera runtime. No process-global camera state.
CameraRuntimeSystem& cameraRuntime(World& world);
/// Register the runtime in the active World; safe to call from both 2D and 3D bootstraps.
void registerCameraRuntimeSystem();
/// Apply runtime shake to an evaluated, unjittered snapshot (not the Entity pose).
void applyCameraEffects(World& world, const Entity& entity, CameraFrame& frame);
}
