#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityParticleIntegrationModule.h>
#include <AYEntity/ParticleSystem.h>
#include <AYEntity/components/ParticleEmitterComponent.h>
#include <AYEntity/components/ParticleSurface2DComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <iostream>

int main() {
    using namespace ayt;
    entity::bootstrapEntityCore();
    auto result=entity::registerEntityParticleComponents(entity::ComponentRegistry::instance());
    if(!result) {std::cerr<<"particle component registration failed\n";return 1;}
    auto& world=entity::World::instance();
    world.shutdown();world.initialize();
    auto* ground=world.createEntity();ground->addComponent<entity::Transform>();
    auto* surface=ground->addComponent<entity::ParticleSurface2DComponent>();
    if(!surface) {std::cerr<<"surface registration failed\n";return 1;}
    surface->halfExtent={2,2};surface->height=.5f;surface->surfaceTag=7;
    auto* rain=world.createEntity();rain->addComponent<entity::Transform>();
    auto* emitter=rain->addComponent<entity::ParticleEmitterComponent>();
    if(!emitter) {std::cerr<<"particle registration failed\n";return 1;}
    particle::ParticleEffect effect;
    effect.burst=1;effect.rate=0;effect.looping=false;
    effect.velocityMin=effect.velocityMax=effect.gravity={};
    effect.lifetime={2,2};
    effect.collision2D.mode=particle::CollisionMode2D::Ground;
    effect.collision2D.spawnHeight=1;effect.collision2D.fallSpeed=5;
    emitter->setEffect(effect);
    entity::ParticleSimulationSystem simulation;
    simulation.onUpdate(0);
    if(!emitter->runtime || emitter->runtime->particles().size()!=1) {
        std::cerr<<"rain did not spawn\n";return 1;
    }
    simulation.onUpdate(.2f);
    const bool hit=emitter->runtime->particles().empty() && emitter->runtime->impacts().size()==1
        && emitter->runtime->impacts()[0].surfaceTag==7 && simulation.impactVisuals().size()==1;
    world.shutdown();
    if(!hit) {std::cerr<<"rain impact loop failed\n";return 1;}
    std::cout<<"PASS 2D particle ground impact and visual\n";
    return 0;
}
