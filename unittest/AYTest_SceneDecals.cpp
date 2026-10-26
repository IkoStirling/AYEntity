#include <AYTest.h>
#include <AYEntity.h>
#include <AYEntity/SceneDecals.h>
#include <AYEntity/RenderSystem.h>
#include <AYEntity/ComponentFactory.h>
#include <AYEntity/components/SceneDecalComponent.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYSerializer/SerializerCore.h>

#include <limits>

using namespace ayt;
TEST_SUITE(EntitySceneDecalTests)

TEST_CASE(scene_decal_values_and_paths_are_checked_at_serialization) {
    auto& world=entity::World::processWorld();world.shutdown();CHECK(world.initialize());
    auto* data=world.createDataEntity();
    CHECK(data->createComponent<entity::SceneDecalComponent>()==nullptr);
    auto* object=world.createSpatialEntity();
    auto* decal=object->createComponent<entity::SceneDecalComponent>();
    CHECK(decal != nullptr && decal->valid());
    decal->albedoPath="textures/stain.png";
    decal->normalPath="textures/stain-normal.png";
    decal->priority=7;
    auto writer=serializer::createSerializer(serializer::Format::Json);
    writer->beginObject(nullptr);
    entity::ComponentFactory::serializeComponent(*writer,*decal);
    writer->endObject();
    CHECK(writer->lastError().ok());
    auto reader=serializer::createSerializer(serializer::Format::Json);
    reader->deserialize(writer->output());reader->beginObject(nullptr);
    entity::SceneDecalComponent restored;
    CHECK(entity::ComponentFactory::deserializeComponent(*reader,
        "SceneDecalComponent",restored));
    reader->endObject();
    CHECK(reader->lastError().ok());
    CHECK(restored.albedoPath==decal->albedoPath && restored.priority==7);
    decal->normalPath="../escape.png";
    auto invalid=serializer::createSerializer(serializer::Format::Json);
    invalid->beginObject(nullptr);
    entity::ComponentFactory::serializeComponent(*invalid,*decal);
    invalid->endObject();
    CHECK_FALSE(invalid->lastError().ok());
    world.shutdown();
}

TEST_CASE(extraction_respects_region_priority_identity_and_projector_pose) {
    auto& world=entity::World::processWorld();world.shutdown();CHECK(world.initialize());
    CHECK(world.connectedSpaces().addRegion(1));
    CHECK(world.connectedSpaces().addRegion(2));
    for(int i=0;i<18;++i){
        auto* object=world.createSpatialEntity();
        CHECK(object->setSpaceRegion(1));
        auto* decal=object->createComponent<entity::SceneDecalComponent>();
        decal->priority=i;
        object->transform()->position={float(i),0,0};
    }
    auto* foreign=world.createSpatialEntity();CHECK(foreign->setSpaceRegion(2));
    foreign->createComponent<entity::SceneDecalComponent>()->priority=100;
    auto* invalid=world.createSpatialEntity();CHECK(invalid->setSpaceRegion(1));
    invalid->createComponent<entity::SceneDecalComponent>()->opacity=2;
    std::vector<entity::SceneDecalCandidate> output;
    const auto result=entity::collectSceneDecals(world,output,1,1);
    CHECK(result.authored==20 && result.invalid==1);
    CHECK(result.emitted==16 && result.overflow==2);
    CHECK(output.size()==16);
    CHECK_FLOAT_EQ(output.front().decal.localToWorld(0,3),17,0);
    CHECK_FLOAT_EQ(output.back().decal.localToWorld(0,3),2,0);
    const auto other=entity::collectSceneDecals(world,output,1,2);
    CHECK(other.emitted==1 && output.size()==1);
    CHECK(entity::collectSceneDecals(world,output,
        std::numeric_limits<float>::infinity(),1).emitted==0);
    world.shutdown();
}

TEST_CASE(render_system_publishes_and_revokes_scene_decal_state) {
    auto& world=entity::World::processWorld();world.shutdown();CHECK(world.initialize());
    const auto* previous=entity::World::activeWorld();entity::World::setActiveWorld(&world);
    render::RendererSubSystem::setWindowProvider({});
    render::RendererSubSystem::setBootstrapBackend(render::Backend::Noop);
    static int sentinel=0;
    render::RendererSubSystem::setBootstrapWindow(&sentinel,64,64);
    auto* subsystem=new render::RendererSubSystem();
    game::SubSystemRegistry::instance().registerSubSystem(subsystem);
    const bool initialized=subsystem->initialize();CHECK(initialized);
    if(initialized){
        {
            entity::RenderSystem system;system.onStart();
            auto* object=world.createSpatialEntity();
            object->createComponent<entity::SceneDecalComponent>();
            subsystem->update(0);
            CHECK(subsystem->renderer().deferredDecalsEnabled());
            CHECK(subsystem->renderer().deferredDecals().size()==1);
            CHECK(subsystem->setCameraViewports({{0,0,0,64,64,{}}}));
            subsystem->update(0);
            CHECK_FALSE(subsystem->renderer().deferredDecalsEnabled());
            CHECK(subsystem->setCameraViewports({}));
            subsystem->update(0);
            CHECK(subsystem->renderer().deferredDecalsEnabled());
            world.destroyEntity(object);
            subsystem->update(0);
            CHECK_FALSE(subsystem->renderer().deferredDecalsEnabled());
            CHECK(subsystem->renderer().deferredDecals().empty());
            object=world.createSpatialEntity();
            object->createComponent<entity::SceneDecalComponent>();
            subsystem->update(0);
            CHECK(subsystem->renderer().deferredDecalsEnabled());
            subsystem->clearSceneBuildersForOwner(&world);
        }
        CHECK_FALSE(subsystem->renderer().deferredDecalsEnabled());
        subsystem->shutdown();
    }
    game::SubSystemRegistry::instance().unregisterSubSystem("Renderer");
    world.shutdown();
    entity::World::setActiveWorld(const_cast<entity::World*>(previous));
}

TEST_SUITE_END
