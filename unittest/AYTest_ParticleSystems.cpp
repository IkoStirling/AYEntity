#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityParticleIntegrationModule.h>
#include <AYEntity/ParticleSystem.h>
#include <AYEntity/SceneSerializer.h>
#include <AYEntity/components/ParticleEmitterComponent.h>
#include <AYEntity/components/ParticleEffectComponent.h>
#include <AYParticle/EffectAssetIO.h>
#include <AYParticle/EffectResource.h>
#include <AYResource/ResourceManager.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYTest.h>
#include <filesystem>

using namespace ayt;
namespace {
entity::ParticleEmitterComponent* emitter(const particle::ParticleEffect& effect) {
    auto* e=entity::World::instance().createEntity();
    e->addComponent<entity::Transform>();
    auto* component=e->addComponent<entity::ParticleEmitterComponent>();
    component->setEffect(effect); return component;
}
struct Fixture {
    render::RendererSubSystem* renderer=nullptr;
    Fixture(bool graphics=false) {
        auto& world=entity::World::instance(); world.shutdown(); world.initialize();
        if (graphics) {
            static int sentinel=0;
            render::RendererSubSystem::setWindowProvider({});
            render::RendererSubSystem::setBootstrapBackend(render::Backend::Noop);
            render::RendererSubSystem::setBootstrapWindow(&sentinel,800,600);
            renderer=new render::RendererSubSystem();
            game::SubSystemRegistry::instance().registerSubSystem(renderer);
            if (!renderer->initialize()) renderer=nullptr;
        }
    }
    ~Fixture() {
        entity::World::instance().shutdown();
        if (renderer) game::SubSystemRegistry::instance().unregisterSubSystem("Renderer");
    }
};
particle::ParticleEffect burst(uint32_t count=100) {
    auto e=particle::smoke(); e.burst=count; e.rate=0;
    // Structural extraction fixtures stay visible at birth; smoke now fades in.
    e.sizeCurve.clear(); e.opacityCurve.clear(); e.colorGradient.clear();
    e.velocityMin=e.velocityMax={}; e.gravity={}; e.lifetime={2,2}; return e;
}
}
TEST_SUITE(AYEntityParticles)
TEST_CASE(particle_gpu_preference_falls_back_without_graphics) {
    Fixture fixture;
    auto effect=burst(10); effect.backend=particle::Backend::Gpu; auto* c=emitter(effect);
    entity::ParticleSimulationSystem system; system.onUpdate(.1f);
    CHECK(!c->usingGpu); CHECK(c->backendStatus.starts_with("CPU fallback:"));
    CHECK(c->runtime->particles().size()==10); CHECK(system.gpuReservedParticles==0);
    CHECK(system.gpuEmitters==0); CHECK(system.liveParticles==10);
    c->backend=256; system.onUpdate(.1f); CHECK(!c->error.empty());
    CHECK(c->runtime->particles().empty());
}
TEST_CASE(particle_gpu_preference_falls_back_on_noop) {
    Fixture fixture(true); CHECK_NOT_NULL(fixture.renderer); if(!fixture.renderer) return;
    auto effect=burst(10); effect.backend=particle::Backend::Auto; auto* c=emitter(effect);
    entity::registerEntityParticleSystems(); auto& world=entity::World::instance();
    world.updatePresentation(.01f,1); world.updatePresentation(.01f,1);
    CHECK(!c->usingGpu); CHECK(c->backendStatus.starts_with("CPU fallback:"));
    CHECK(c->runtime->particles().size()==10);
    fixture.renderer->update(0); CHECK(fixture.renderer->renderScene().items().size()==1);
}
TEST_CASE(particle_scene_roundtrip_excludes_live_state) {
    Fixture fixture;
    auto* c=emitter(particle::explosion()); c->layer=7; c->sortingKey=9;
    c->backend=1;
    c->texturePath="textures/particle.aytex"; c->play(); c->runtime->update(0);
    const auto expected=c->effect();
    CHECK_TRUE(!c->runtime->particles().empty());
    const auto path=(std::filesystem::temp_directory_path()/"ay_particle_roundtrip.ayscene").string();
    CHECK(entity::saveScene(entity::World::instance(),path));
    entity::World::instance().shutdown(); entity::World::instance().initialize();
    CHECK(entity::loadScene(entity::World::instance(),path));
    bool found=false;
    for (auto* e:entity::World::instance().query<entity::ParticleEmitterComponent>()) {
        auto* loaded=e->getComponent<entity::ParticleEmitterComponent>();
        CHECK(loaded->effect()==expected); CHECK(loaded->runtime==nullptr);
        CHECK_INT_EQ(loaded->layer,7); CHECK_INT_EQ(loaded->sortingKey,9); found=true;
    }
    CHECK(found); std::filesystem::remove(path);
}
TEST_CASE(particle_world_budget_and_play_controls) {
    Fixture fixture; auto* a=emitter(burst()); auto* b=emitter(burst());
    entity::ParticleSimulationSystem system; system.maxParticles=120; system.onUpdate(0);
    CHECK(a->runtime->particles().size()+b->runtime->particles().size()==120);
    CHECK(system.liveParticles==120);
    a->pause(); system.onUpdate(0.5f);
    CHECK_FLOAT_EQ(a->runtime->particles()[0].age,0,0);
    a->stop(true); CHECK(a->runtime->particles().empty());
    b->capacity=0; system.onUpdate(0.1f); CHECK(!b->error.empty());
    CHECK(b->runtime->particles().empty());
}
TEST_CASE(particle_2d_and_3d_batches_submit_once) {
    Fixture fixture(true); CHECK_NOT_NULL(fixture.renderer); if (!fixture.renderer) return;
    auto* a=emitter(burst()); a->layer=4; a->sortingKey=12;
    auto effect=burst(20); effect.dimension=particle::Dimension::ThreeD;
    auto* b=emitter(effect);
    entity::ParticleSimulationSystem simulation; simulation.onUpdate(0);
    entity::ParticleRenderSystem extraction;
    auto& renderer=fixture.renderer->renderer();
    renderer.setMainCameraLookAtPerspective({0,0,5},{0,0,0},{0,1,0},50,4.0f/3,0.1f,100);
    render::RenderScene scene; extraction.buildRenderScene(scene);
    CHECK_INT_EQ(static_cast<int>(scene.items().size()),2);
    if (scene.items().size()!=2) return;
    CHECK_NOT_NULL(scene.items()[0].particleBatch);
    CHECK(scene.items()[0].particleBatch->vertices.size()==400);
    CHECK_NOT_NULL(scene.items()[0].payload);
    CHECK(scene.items()[0].payload->packedSortKey==((4u<<24)|12));
    CHECK(scene.items()[1].payload==nullptr);
    CHECK(scene.items()[1].particleBatch->vertices.size()==80);
    CHECK(scene.items()[0].shadowFlags==render::ShadowFlags::None);
    renderer.beginFrame(); renderer.render(scene); renderer.endFrame();
    CHECK(renderer.getFrameStats().drawCalls==2);
    b->stop(true); scene.clear(); extraction.buildRenderScene(scene);
    CHECK_INT_EQ(static_cast<int>(scene.items().size()),1);
}
TEST_CASE(particle_lifetime_tracks_reach_2d_and_3d_render_vertices) {
    Fixture fixture(true); CHECK_NOT_NULL(fixture.renderer); if(!fixture.renderer) return;
    auto effect=burst(1); effect.size={1,1}; effect.looping=false;
    effect.sizeCurve={{0,1},{0.5f,2},{1,0}};
    effect.opacityCurve={{0,1},{0.5f,0.5f},{1,0}};
    effect.colorGradient={{0,{1,0,0,1}},{0.5f,{0,1,0,1}},{1,{0,0,1,1}}};
    emitter(effect); effect.dimension=particle::Dimension::ThreeD; emitter(effect);
    entity::ParticleSimulationSystem simulation; simulation.onUpdate(1);
    entity::ParticleRenderSystem extraction; render::RenderScene scene; extraction.buildRenderScene(scene);
    CHECK_INT_EQ(int(scene.items().size()),2);
    for(const auto& item:scene.items()) {
        CHECK_NOT_NULL(item.particleBatch); if(!item.particleBatch) continue;
        CHECK(item.particleBatch->vertices.size()==4);
        const auto& v=item.particleBatch->vertices.front();
        CHECK_FLOAT_EQ(v.r,0,0.0001f); CHECK_FLOAT_EQ(v.g,1,0.0001f);
        CHECK_FLOAT_EQ(v.a,0.5f,0.0001f);
        const auto& other=item.particleBatch->vertices[1];
        const float dx=other.x-v.x,dy=other.y-v.y,dz=other.z-v.z;
        CHECK_FLOAT_EQ(dx*dx+dy*dy+dz*dz,4,0.0001f);
    }
}
TEST_CASE(particle_2d_camera_culling_and_layer_mask) {
    Fixture fixture(true); CHECK_NOT_NULL(fixture.renderer); if (!fixture.renderer) return;
    auto* c=emitter(burst(10)); c->layer=3;
    entity::ParticleSimulationSystem simulation; simulation.onUpdate(0);
    entity::ParticleRenderSystem extraction; render::RenderScene scene;
    scene.setOverlayCamera2D(math::Float4x4::identity(),math::Float4x4::identity(),1u<<2);
    extraction.buildRenderScene(scene); CHECK(scene.items().empty());
    scene.setOverlayCamera2D(math::Float4x4::identity(),math::Float4x4::identity(),1u<<3);
    extraction.buildRenderScene(scene); CHECK(scene.items().size()==1);
    for (auto* e:entity::World::instance().query<entity::Transform,entity::ParticleEmitterComponent>())
        e->getComponent<entity::Transform>()->position={100,100,0};
    c->play(); simulation.onUpdate(0); scene.clear();
    scene.setOverlayCamera2D(math::Float4x4::identity(),math::Float4x4::identity());
    extraction.buildRenderScene(scene); CHECK(scene.items().empty());
}
TEST_CASE(particle_world_shutdown_detaches_render_builder) {
    Fixture fixture(true); CHECK_NOT_NULL(fixture.renderer); if (!fixture.renderer) return;
    auto* c=emitter(burst(10)); (void)c;
    entity::registerEntityParticleSystems(); entity::registerEntityParticleSystems();
    auto& world=entity::World::instance();
    CHECK(world.systemCount()==2);
    world.updatePresentation(0,1);
    fixture.renderer->update(0);
    CHECK(fixture.renderer->renderScene().items().size()==1);
    world.shutdown(); world.initialize();
    fixture.renderer->update(0);
    CHECK(fixture.renderer->renderScene().items().empty());
}

TEST_CASE(particle_editor_override_drives_culling) {
    Fixture fixture(true); CHECK_NOT_NULL(fixture.renderer); if (!fixture.renderer) return;
    auto* c=emitter(burst(10)); c->layer=3;
    for (auto* e:entity::World::instance().query<entity::Transform,entity::ParticleEmitterComponent>())
        e->getComponent<entity::Transform>()->position={100,0,0};
    entity::ParticleSimulationSystem simulation; simulation.onUpdate(0);
    render::RenderScene scene;
    scene.setOverlayCamera2D(math::Float4x4::identity(),math::Float4x4::identity());
    entity::ParticleRenderSystem extraction;
    extraction.buildRenderScene(scene); CHECK(scene.items().empty());
    auto shifted=math::Float4x4::identity(); shifted(0,3)=-100;
    fixture.renderer->setOverlayCamera2DOverride(shifted,math::Float4x4::identity(),1u<<3);
    extraction.buildRenderScene(scene); CHECK(scene.items().size()==1);
    scene.clear();
    fixture.renderer->setOverlayCamera2DOverride(shifted,math::Float4x4::identity(),1u<<2);
    extraction.buildRenderScene(scene); CHECK(scene.items().empty());
    fixture.renderer->clearOverlayCamera2DOverride();
}
TEST_CASE(particle_showcase_replays_after_opening_delay) {
    Fixture fixture(true); CHECK_NOT_NULL(fixture.renderer); if (!fixture.renderer) return;
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        / "AYParticle/examples/ParticleShowcase.ayscene";
    CHECK(entity::loadScene(entity::World::instance(),path.string()));
    auto emitters=entity::World::instance().query<entity::ParticleEmitterComponent>();
    int emitterCount=0; for (auto* e:emitters) { (void)e; ++emitterCount; }
    CHECK_INT_EQ(emitterCount,3);
    entity::ParticleSimulationSystem simulation;
    entity::ParticleRenderSystem extraction;
    auto projection=math::Float4x4::identity(); projection(0,0)=projection(1,1)=1.0f/6;
    render::RenderScene scene;
    const auto extract=[&] {
        scene.clear(); scene.setOverlayCamera2D(math::Float4x4::identity(),projection);
        extraction.buildRenderScene(scene);
    };
    simulation.onUpdate(1.25f); extract();
    CHECK(scene.items().size()==1); // Initial short bursts have already expired.
    simulation.onUpdate(1); extract();
    CHECK(scene.items().size()==3); // Both additive effects replay at t=2.
    simulation.onUpdate(2); extract();
    CHECK(scene.items().size()==3); // Replay persists at t=4.
    int bursts=0;
    for (auto* e:emitters) {
        const auto* c=e->getComponent<entity::ParticleEmitterComponent>();
        if (c->burst) {
            CHECK(c->looping); CHECK_FLOAT_EQ(c->duration,2,0);
            CHECK(c->runtime->particles().size()==static_cast<size_t>(c->burst));
            ++bursts;
        }
    }
    CHECK_INT_EQ(bursts,2);
}
TEST_CASE(particle_resource_composition_scene_roundtrip_and_rendering) {
    Fixture fixture(true); CHECK_NOT_NULL(fixture.renderer); if(!fixture.renderer) return;
    auto asset=particle::combinedExplosion(); asset.looping=true;
    const auto path=(std::filesystem::temp_directory_path()/"ay_composite.ayparticle").string();
    CHECK(particle::saveEffectAsset(path,asset));
    auto* entity=entity::World::instance().createEntity(); entity->addComponent<entity::Transform>();
    auto* effect=entity->addComponent<entity::ParticleEffectComponent>(); effect->effectPath=path; effect->layer=3;
    entity::ParticleSimulationSystem simulation; simulation.onUpdate(0);
    CHECK(effect->runtime && effect->runtime->liveParticles()==61);
    entity::ParticleRenderSystem extraction; render::RenderScene scene;
    extraction.buildRenderScene(scene); CHECK(scene.items().size()==2);
    simulation.onUpdate(0.14f); scene.clear(); extraction.buildRenderScene(scene);
    CHECK(scene.items().size()==3); // Flash, sparks and delayed smoke share one entity.
    effect->pause(); const auto time=effect->runtime->time(); simulation.onUpdate(1);
    CHECK(effect->runtime->time()==time);
    effect->resume(); simulation.onUpdate(3);
    CHECK(effect->runtime->liveParticles()>0);
    auto changed=asset; changed.emitters[1].effect.burst=10;
    CHECK(particle::saveEffectAsset(path,changed)); resource::ResourceManager::instance().reloadResource(path);
    simulation.onUpdate(0); CHECK(effect->runtime->instances()[1].particles().size()==10);
    const auto scenePath=(std::filesystem::temp_directory_path()/"ay_composite.ayscene").string();
    CHECK(entity::saveScene(entity::World::instance(),scenePath));
    entity::World::instance().shutdown(); entity::World::instance().initialize();
    CHECK(entity::loadScene(entity::World::instance(),scenePath));
    bool found=false;
    for(auto* e:entity::World::instance().query<entity::ParticleEffectComponent>()) {
        auto* c=e->getComponent<entity::ParticleEffectComponent>();
        CHECK(c->effectPath==path); CHECK(c->runtime==nullptr); CHECK(c->error.empty()); found=true;
    }
    CHECK(found);
    resource::ResourceManager::instance().unloadResource(path);
    std::filesystem::remove(path); std::filesystem::remove(scenePath);
}
TEST_CASE(particle_composition_and_single_emitters_share_world_budget) {
    Fixture fixture;
    auto* single=emitter(burst(100)); (void)single;
    auto asset=particle::combinedExplosion();
    const auto path=(std::filesystem::temp_directory_path()/"ay_composite_budget.ayparticle").string();
    CHECK(particle::saveEffectAsset(path,asset));
    auto* e=entity::World::instance().createEntity(); e->addComponent<entity::Transform>();
    auto* group=e->addComponent<entity::ParticleEffectComponent>(); group->effectPath=path;
    entity::ParticleSimulationSystem simulation; simulation.maxParticles=120; simulation.onUpdate(0);
    CHECK(group->runtime!=nullptr); CHECK_INT_EQ(simulation.liveParticles,120);
    CHECK(single->runtime->particles().size()+group->runtime->liveParticles()==120);
    group->effectPath="missing/invalid.ayparticle"; simulation.onUpdate(0);
    CHECK(!group->error.empty()); CHECK(group->runtime->liveParticles()==0);
    group->effectPath=path; simulation.onUpdate(0);
    CHECK(group->error.empty()); CHECK(group->runtime->liveParticles()>0);
    resource::ResourceManager::instance().unloadResource(path); std::filesystem::remove(path);
}

TEST_CASE(typed_particle_component_sources_serialize_and_fail_closed) {
    Fixture fixture(false);auto* object=entity::World::instance().createEntity();object->addComponent<entity::Transform>();
    auto* emitter=object->addComponent<entity::ParticleEmitterComponent>();auto e=particle::leaves();
    e.flipX.probability=.75f;emitter->setEffect(e);CHECK(emitter->effect()==e);CHECK(emitter->prepare());
    const auto file=(std::filesystem::temp_directory_path()/"ay_typed_particle.ayscene").string();
    CHECK(entity::saveScene(entity::World::instance(),file));entity::World::instance().shutdown();entity::World::instance().initialize();
    CHECK(entity::loadScene(entity::World::instance(),file));
    for(auto* object:entity::World::instance().query<entity::ParticleEmitterComponent>()) {
        auto* loaded=object->getComponent<entity::ParticleEmitterComponent>();CHECK(loaded->effect()==e);CHECK(loaded->prepare());
        loaded->runtime->play();loaded->runtime->update(.1f);CHECK(!loaded->runtime->particles().empty());
        loaded->propertySources="corrupt";CHECK_FALSE(loaded->prepare());CHECK(!loaded->error.empty());CHECK(loaded->runtime->particles().empty());
    }
    std::filesystem::remove(file);
}
TEST_SUITE_END
