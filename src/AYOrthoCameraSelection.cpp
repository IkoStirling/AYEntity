#include "AYEntity/OrthoCameraSelection.h"

#include "AYEntity.h"
#include "AYEntity/EntityImpl.h"
#include "AYEntity/World.h"
#include "AYEntity/components/OrthoCameraComponent.h"
#include "AYEntity/components/TransformComponent.h"

namespace ayt::entity
{

SelectedOrthoCamera2D selectOrthoCamera2D(World& world) noexcept
{
    SelectedOrthoCamera2D selected;
    for (Entity* entity : world.query<Transform, OrthoCameraComponent>()) {
        if (entity == nullptr) continue;
        Transform* transform = entity->getComponent<Transform>();
        if (transform == nullptr) continue;
        for (OrthoCameraComponent* camera :
             entity->getComponents<OrthoCameraComponent>()) {
            if (camera == nullptr || !camera->isEnabled()) continue;
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

} // namespace ayt::entity
