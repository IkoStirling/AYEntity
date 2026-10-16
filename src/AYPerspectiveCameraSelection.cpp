#include <AYEntity.h>
#include <AYEntity/PerspectiveCameraSelection.h>
#include <AYEntity/components/PerspectiveCameraComponent.h>
#include <AYEntity/CameraRuntime.h>
#include <cmath>

namespace ayt::entity {
namespace {
bool validPose(const math::FVector3& p, const math::FQuaternion& q) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
        && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z)
        && std::isfinite(q.w) && std::isfinite(q.dot(q)) && q.dot(q) > 1.0e-12f;
}
}

SelectedPerspectiveCamera3D selectPerspectiveCamera3D(World& world, int32_t viewportId) {
    SelectedPerspectiveCamera3D selected;
    for (Entity* entity : world.query<Transform, PerspectiveCameraComponent>()) {
        if (!entity) continue;
        auto* transform = entity->getComponent<Transform>();
        if (!transform || !validPose(transform->position, transform->rotation)) continue;
        math::FVector3 worldPosition; math::FQuaternion worldRotation;
        if (!entity->worldPose(worldPosition,worldRotation)) continue;
        for (auto* camera : entity->getComponents<PerspectiveCameraComponent>()) {
            if (!camera || !camera->active || camera->viewportId != viewportId
                || !camera_detail::finite(camera->localOffset) || !validPose({}, camera->localRotation)) continue;
            const auto* candidate = entity->componentInstance(camera);
            const auto* previous = selected && entity == selected.entity
                ? entity->componentInstance(selected.camera) : nullptr;
            if (!selected || camera->priority > selected.camera->priority
                || (camera->priority == selected.camera->priority
                    && (entity->getId() < selected.entity->getId()
                        || (previous && candidate && candidate->id < previous->id))))
                selected = {entity, transform, camera};
        }
    }
    return selected;
}

bool evaluatePerspectiveCamera3D(World& world, float aspect, float alpha,
                                 PerspectiveCameraFrame& output, int32_t viewportId) {
    const auto selected = selectPerspectiveCamera3D(world, viewportId);
    if (!selected) return false;
    alpha = std::isfinite(alpha) ? std::clamp(alpha, 0.0f, 1.0f) : 1.0f;
    math::FVector3 position; math::FQuaternion rotation;
    if (!selected.entity->worldPose(position, rotation, alpha)) return false;
    if (!validPose(position, rotation)) return false;
    PerspectiveCameraFrame frame;
    frame.position = position + rotation.normalize() * selected.camera->localOffset;
    frame.orientation = (rotation.normalize() * selected.camera->localRotation.normalize()).normalize();
    frame.view = selected.camera->viewMatrix(frame.position, frame.orientation);
    frame.projection = selected.camera->projectionMatrix(aspect);
    frame.layerMask = selected.camera->layerMask;
    frame.projectionMode = selected.camera->projectionMode == 1
        ? CameraProjection::Orthographic : CameraProjection::Perspective;
    frame.aspect = std::isfinite(aspect) && aspect > 1e-6f ? aspect : 1.f;
    frame.fovYDegrees = std::isfinite(selected.camera->fovYDegrees)
        ? std::clamp(selected.camera->fovYDegrees, 1.f, 179.f) : 50.f;
    frame.verticalSize = std::isfinite(selected.camera->orthographicHeight)
        && selected.camera->orthographicHeight > 1e-4f ? selected.camera->orthographicHeight : 10.f;
    frame.nearZ = std::isfinite(selected.camera->nearZ) && selected.camera->nearZ >= 1e-4f
        && selected.camera->nearZ <= 1e6f ? selected.camera->nearZ : .1f;
    frame.farZ = std::isfinite(selected.camera->farZ) && selected.camera->farZ > frame.nearZ
        && selected.camera->farZ <= 1e8f ? selected.camera->farZ : frame.nearZ + 100.f;
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(frame.view.ptr()[i]) || !std::isfinite(frame.projection.ptr()[i]))
            return false;
    frame.cutGeneration = selected.camera->cutGeneration;
    const auto* instance = selected.entity->componentInstance(selected.camera);
    if (!instance) return false;
    // Scene/Play World and recycled Entity ids must not inherit camera history.
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : instance->id) { hash ^= c; hash *= 1099511628211ull; }
    const auto handle = world.getEntityHandle(selected.entity->getId());
    frame.sourceEntityId = handle.id; frame.sourceEntityVersion = handle.version;
    hash ^= uint64_t(reinterpret_cast<uintptr_t>(&world));
    hash ^= (uint64_t(handle.version) << 32u) | uint64_t(handle.id);
    frame.identity = hash ? hash : 1;
    applyCameraEffects(world,*selected.entity,frame);
    output = frame;
    return true;
}
} // namespace ayt::entity
