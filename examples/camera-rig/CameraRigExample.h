#pragma once
#include <AYEntity.h>
#include <AYEntity/CameraRuntime.h>
#include <AYEntity/components/CameraControllerComponent.h>
#include <AYEntity/components/PerspectiveCameraComponent.h>
#include <stdexcept>
namespace ayt::entity::examples {
enum class CameraShot { ThirdPerson, SpellCloseup, Normal };
struct CharacterCameraRig {
    Entity *character{}, *system{}, *rig{}, *thirdPerson{}, *spell{}, *normal{};
    void select(CameraShot shot) const {
        thirdPerson->getComponents<PerspectiveCameraComponent>().at(0)->active = shot==CameraShot::ThirdPerson;
        spell->getComponents<PerspectiveCameraComponent>().at(0)->active = shot==CameraShot::SpellCloseup;
        normal->getComponents<PerspectiveCameraComponent>().at(0)->active = shot==CameraShot::Normal;
    }
};
/// Example-only assembly, independent of physics/render initialization. Caller owns the World.
/// Spatial attachment follows the character; independent ownership releases all cameras on deletion.
inline CharacterCameraRig makeCharacterCameraRig(World& world) {
    CharacterCameraRig result;
    const auto spatial=[&](const char* name) { auto* e=world.createSpatialEntity(); if(!e) throw std::runtime_error("Entity creation failed"); e->setName(name); return e; };
    result.character=spatial("Character"); result.character->transform()->position={0,1,0};
    result.system=world.createDataEntity(); result.system->setName("CameraSystem");
    if(!result.system->setOwner(result.character)) throw std::runtime_error("Camera system owner failed");
    result.rig=spatial("CameraRig");
    if(!result.rig->setSpatialParent(result.character) || !result.rig->setOwner(result.system)) throw std::runtime_error("Camera rig attachment failed");
    result.thirdPerson=spatial("ThirdPerson"); result.spell=spatial("SpellCloseup"); result.normal=spatial("NormalCamera");
    for(auto* camera:{result.thirdPerson,result.spell,result.normal}) {
        if(!camera->setSpatialParent(result.rig) || !camera->setOwner(result.rig)) throw std::runtime_error("Lens attachment failed");
        auto* lens=camera->createComponent<PerspectiveCameraComponent>();
        if(!lens) throw std::runtime_error("Perspective camera creation failed");
        lens->blendDuration=.35f;
    }
    result.spell->transform()->position={0,.6f,-1.2f};
    result.normal->transform()->position={0,1,-3};
    auto* controller=result.thirdPerson->addComponent<CameraControllerComponent>();
    controller->mode=2; controller->offset={0,.5f,0}; controller->distance=5;
    controller->positionDamping=8; controller->rotationDamping=8;
    if(!controller->setTarget(result.character)) throw std::runtime_error("Camera target failed");
    result.select(CameraShot::ThirdPerson); (void)cameraRuntime(world); return result;
}
} // namespace ayt::entity::examples
