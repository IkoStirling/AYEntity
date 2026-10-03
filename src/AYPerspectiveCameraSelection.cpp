#include <AYEntity.h>
#include <AYEntity/PerspectiveCameraSelection.h>
#include <AYEntity/components/PerspectiveCameraComponent.h>
#include <cmath>

namespace ayt::entity {
namespace {
bool validPose(const math::FVector3& p, const math::FQuaternion& q) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
        && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z)
        && std::isfinite(q.w) && std::isfinite(q.dot(q)) && q.dot(q) > 1.0e-12f;
}
}

SelectedPerspectiveCamera3D selectPerspectiveCamera3D(World& world) {
    SelectedPerspectiveCamera3D selected;
    for (Entity* entity : world.query<Transform, PerspectiveCameraComponent>()) {
        if (!entity) continue;
        auto* transform = entity->getComponent<Transform>();
        if (!transform || !validPose(transform->position, transform->rotation)) continue;
        for (auto* camera : entity->getComponents<PerspectiveCameraComponent>()) {
            if (!camera || !camera->active) continue;
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
                                 PerspectiveCameraFrame& output) {
    const auto selected = selectPerspectiveCamera3D(world);
    if (!selected) return false;
    alpha = std::isfinite(alpha) ? std::clamp(alpha, 0.0f, 1.0f) : 1.0f;
    const auto position = selected.transform->interpolatedPosition(alpha);
    const auto rotation = selected.transform->interpolatedRotation(alpha);
    if (!validPose(position, rotation)) return false;
    PerspectiveCameraFrame frame;
    frame.position = position;
    frame.view = selected.camera->viewMatrix(position, rotation);
    frame.projection = selected.camera->projectionMatrix(aspect);
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
    hash ^= uint64_t(reinterpret_cast<uintptr_t>(&world));
    hash ^= (uint64_t(handle.version) << 32u) | uint64_t(handle.id);
    frame.identity = hash ? hash : 1;
    output = frame;
    return true;
}
} // namespace ayt::entity
