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
        OrthoCameraComponent* camera =
            entity->getComponent<OrthoCameraComponent>();
        if (transform == nullptr || camera == nullptr || !camera->isEnabled()) {
            continue;
        }
        if (!selected
            || camera->priority > selected.camera->priority
            || (camera->priority == selected.camera->priority
                && entity->getId() < selected.entity->getId())) {
            selected = {entity, transform, camera};
        }
    }
    return selected;
}

} // namespace ayt::entity
