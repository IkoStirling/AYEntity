#include "CameraRigExample.h"
#include <AYEntity/EntityModule.h>
#include <AYEntity/SceneSerializer.h>
#include <AYEntity/PerspectiveCameraSelection.h>
#include <filesystem>
#include <cstdio>
int main(int argc,char** argv) {
    using namespace ayt::entity;
    if(argc!=2) { std::fprintf(stderr,"Usage: AYEntity_CameraRigExample <fresh-scene.scn>\n"); return 2; }
    if(std::filesystem::exists(argv[1])) { std::fprintf(stderr,"Refusing to overwrite existing scene\n"); return 2; }
    if(!registerEntityCoreComponents(ComponentRegistry::instance())) return 1;
    auto& world = World::processWorld(); if(!world.initialize()) return 1;
    const auto rig=examples::makeCharacterCameraRig(world);
    world.updatePresentation(.1f,1); CameraFrame frame;
    if(!evaluatePerspectiveCamera3D(world,16.f/9,1,frame)) return 1;
    if(!saveScene(world,argv[1])) return 1;
    std::printf("Saved %zu entities. Third-person world eye: %.3f %.3f %.3f\n",world.getAllEntities().size(),frame.position.x,frame.position.y,frame.position.z);
    rig.select(examples::CameraShot::SpellCloseup); world.updatePresentation(.1f,1);
    if(selectPerspectiveCamera3D(world).entity!=rig.spell) return 1;
    world.destroyEntity(rig.character); if(!world.getAllEntities().empty()) return 1;
    std::printf("Spell switch and owner cascade succeeded\n"); world.shutdown(); return 0;
}
