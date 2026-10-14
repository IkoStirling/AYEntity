#include "AYEntity/OrthoCameraSelection.h"

#include "AYEntity.h"
#include "AYEntity/EntityImpl.h"
#include "AYEntity/World.h"
#include "AYEntity/components/OrthoCameraComponent.h"
#include "AYEntity/components/TransformComponent.h"
#include <AYEntity/CameraRuntime.h>

namespace ayt::entity
{

SelectedOrthoCamera2D selectOrthoCamera2D(World& world, int32_t viewportId) noexcept
{
    SelectedOrthoCamera2D selected;
    for (Entity* entity : world.query<Transform, OrthoCameraComponent>()) {
        if (entity == nullptr) continue;
        Transform* transform = entity->getComponent<Transform>();
        if (transform == nullptr) continue;
        math::FVector3 worldPosition; math::FQuaternion worldRotation;
        if (!entity->worldPose(worldPosition,worldRotation)) continue;
        for (OrthoCameraComponent* camera :
             entity->getComponents<OrthoCameraComponent>()) {
            if (camera == nullptr || !camera->isEnabled() || camera->viewportId != viewportId) continue;
            const bool sameEntity = selected && entity == selected.entity;
            const auto* candidateInstance = entity->componentInstance(camera);
            const auto* selectedInstance = sameEntity
                ? entity->componentInstance(selected.camera) : nullptr;
            if (!selected
                || camera->priority > selected.camera->priority
                || (camera->priority == selected.camera->priority
                    && (entity->getId() < selected.entity->getId()
                        || (sameEntity && candidateInstance && selectedInstance
                            && candidateInstance->id < selectedInstance->id)))) {
                selected = {entity, transform, camera};
            }
        }
    }
    return selected;
}

bool evaluateOrthoCamera2D(World& world, float aspect, float alpha, CameraFrame& output, int32_t viewportId) {
    const auto selected = selectOrthoCamera2D(world, viewportId);
    if (!selected) return false;
    alpha = std::isfinite(alpha) ? std::clamp(alpha,0.f,1.f) : 1.f;
    math::FVector3 position; math::FQuaternion rotation;
    if (!selected.entity->worldPose(position,rotation,alpha)) return false;
    if (!camera_detail::finite(position) || !std::isfinite(rotation.dot(rotation)) || rotation.dot(rotation) < 1e-12f) return false;
    const float angle = rotation.toEulerAngles().z;
    const auto center = selected.camera->worldCenter(position,angle);
    if (!std::isfinite(selected.camera->nearZ) || !std::isfinite(selected.camera->farZ)
        || selected.camera->farZ<=selected.camera->nearZ) return false;
    CameraFrame frame;
    frame.position = {center.x,center.y,0};
    const float worldAngle = selected.camera->worldRotation(angle);
    frame.orientation = math::FQuaternion::fromAxisAngle({0,0,1},
        worldAngle + (selected.camera->safeZoom() < 0 ? 3.14159265359f : 0.f));
    frame.view = selected.camera->viewMatrix(center.x,center.y,worldAngle);
    frame.projection = selected.camera->projectionMatrix(aspect);
    frame.projectionMode = CameraProjection::Orthographic;
    frame.verticalSize = selected.camera->effectiveViewSize(aspect) / std::fabs(selected.camera->safeZoom());
    frame.aspect = selected.camera->viewportAspectOr(aspect);
    frame.nearZ = selected.camera->nearZ; frame.farZ = selected.camera->farZ;
    frame.layerMask = selected.camera->layerMask; frame.cutGeneration = selected.camera->cutGeneration;
    if (!camera_detail::finite(frame.view) || !camera_detail::finite(frame.projection)
        || !camera_detail::finite(frame.position) || !std::isfinite(worldAngle)
        || !std::isfinite(frame.nearZ) || !std::isfinite(frame.farZ) || frame.farZ <= frame.nearZ) return false;
    const auto* instance = selected.entity->componentInstance(selected.camera);
    if (!instance) return false;
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : instance->id) { hash ^= c; hash *= 1099511628211ull; }
    const auto handle = world.getEntityHandle(selected.entity->getId());
    frame.sourceEntityId = handle.id; frame.sourceEntityVersion = handle.version;
    hash ^= uint64_t(reinterpret_cast<uintptr_t>(&world));
    hash ^= (uint64_t(handle.version) << 32) | handle.id;
    frame.identity = hash ? hash : 1;
    applyCameraEffects(world,*selected.entity,frame);
    output = frame; return true;
}

} // namespace ayt::entity
