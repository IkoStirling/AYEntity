#pragma once
#include <AYEntity/IEntity.h>
#include <AYEntity/CameraFrame.h>
#include <functional>
#include <unordered_map>

namespace ayt::entity {
class World;
class Entity;
/// Sweep a camera sphere from the anchor to desired eye; return unobstructed center distance.
/// The callback executes on the presentation owner thread and must not mutate/destroy the World.
struct CameraCollisionProbe {
    math::FVector3 anchor{}, desiredEye{};
    float radius = .2f;
    const Entity* cameraEntity = nullptr;
};
using CameraCollisionQuery = std::function<float(const CameraCollisionProbe&)>;

/// World-owned presentation control and independent transitions per viewport/domain.
/// Standard Entity.Runtime installs it; standalone/headless callers use registerCameraRuntimeSystem.
class CameraRuntimeSystem : public ISystem {
public:
    const char* getName() const override { return "CameraRuntimeSystem"; }
    void onUpdate(float dt) override;
    /// Install a sphere-sweep adapter; an empty callback disables collision avoidance.
    void setCollisionQuery(CameraCollisionQuery query) { _collision = std::move(query); }
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
    static constexpr int kPriority = 399;
private:
    friend CameraRuntimeSystem& cameraRuntime(World& world);
    World* _owner = nullptr;
    struct TransitionState { CameraTransition transition; uint64_t tick = UINT64_MAX; };
    std::unordered_map<uint64_t, TransitionState> _transitions;
    CameraCollisionQuery _collision;
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
