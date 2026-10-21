#include <AYTest.h>
#include <AYEntity.h>
#include <AYEntity/SceneLighting.h>
#include <AYEntity/RenderSystem.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYEntity/components/LightComponent.h>
#include <AYRenderer/RenderScene.h>
#include <AYSerializer/SerializerCore.h>
#include <AYEntity/ComponentFactory.h>
#include <limits>
#include <set>

using namespace ayt;
TEST_SUITE(EntitySceneLightingTests)
TEST_CASE(scene_light_defaults_validation_and_spatial_requirement)
{
    entity::LightComponent light;CHECK(light.valid());CHECK(light.enabled);CHECK(light.range==10);
    light.intensity=std::numeric_limits<float>::quiet_NaN();CHECK_FALSE(light.valid());light.intensity=1;
    light.innerAngleDegrees=31;CHECK_FALSE(light.valid());light.innerAngleDegrees=20;
    light.type=1;light.castShadow=true;CHECK_FALSE(light.valid());light.castShadow=false;CHECK(light.valid());
    auto& world=entity::World::processWorld();world.shutdown();CHECK(world.initialize());auto* data=world.createDataEntity();CHECK(data->createComponent<entity::LightComponent>()==nullptr);
    auto* spatial=world.createSpatialEntity();CHECK(spatial->createComponent<entity::LightComponent>());world.shutdown();
}
TEST_CASE(extraction_uses_world_pose_scale_free_direction_and_world_meter_range)
{
    auto& world=entity::World::processWorld();world.shutdown();CHECK(world.initialize());auto* parent=world.createSpatialEntity();parent->transform()->setPosition(2,3,4);
    parent->transform()->setScale(3,2,4);parent->transform()->rotation=math::FQuaternion::fromAxisAngle({0,1,0},1.57079632679f);
    auto* object=world.createSpatialEntity();CHECK(object->setSpatialParent(parent));object->transform()->setPosition(0,1,0);
    auto* light=object->createComponent<entity::LightComponent>();light->type=2;light->range=7;light->intensity=3;
    render::SceneLights output;const auto result=entity::collectSceneLighting(world,output);
    CHECK(result.authored==1&&result.emitted==1);CHECK(output.count==1);const auto& v=output.lights[0];
    CHECK(v.type==render::LightType::Spot);CHECK_FLOAT_EQ(v.position.x,2,1e-5f);CHECK_FLOAT_EQ(v.position.y,5,1e-5f);
    CHECK_FLOAT_EQ(v.position.z,4,1e-5f);CHECK_FLOAT_EQ(v.spotDirection.x,1,1e-5f);CHECK_FLOAT_EQ(v.spotDirection.z,0,1e-5f);
    CHECK_FLOAT_EQ(v.range,7,1e-5f);CHECK_FLOAT_EQ(v.coneCosInner,std::cos(20*.01745329251994329577f),1e-5f);
    world.shutdown();
}
TEST_CASE(extraction_is_bounded_deterministically_ordered_and_reports_disabled_invalid_overflow)
{
    auto& world=entity::World::processWorld();world.shutdown();CHECK(world.initialize());std::set<std::string> identities;
    for(int i=0;i<10;++i){auto* object=world.createSpatialEntity();identities.insert(object->persistentId());
        auto* light=object->createComponent<entity::LightComponent>();light->type=1;light->priority=0;
        object->transform()->setPosition(float(i),0,0);}
    auto* high=world.createSpatialEntity();auto* priority=high->createComponent<entity::LightComponent>();priority->priority=10;priority->color={.2f,.3f,.4f};priority->intensity=2;
    auto* off=world.createSpatialEntity();off->createComponent<entity::LightComponent>()->enabled=false;
    auto* invalid=world.createSpatialEntity();invalid->createComponent<entity::LightComponent>()->range=-1;
    render::SceneLights output;const auto result=entity::collectSceneLighting(world,output);
    CHECK(result.authored==13&&result.disabled==1&&result.invalid==1&&result.emitted==8&&result.overflow==3);
    CHECK(output.count==8);CHECK(output.lights[0].type==render::LightType::Directional);CHECK_FLOAT_EQ(output.lights[0].color.y,.6f,1e-5f);
    auto iterator=identities.begin();for(unsigned i=1;i<8;++i,++iterator){auto* original=world.findEntityByPersistentId(*iterator);CHECK(original);
        if(original)CHECK_FLOAT_EQ(output.lights[i].position.x,original->transform()->position.x,1e-5f);}
    world.shutdown();
}
TEST_CASE(disabled_or_invalid_authored_lights_are_explicit_empty_and_no_light_preserves_host_policy)
{
    auto& world=entity::World::processWorld();world.shutdown();CHECK(world.initialize());render::SceneLights output;output.add(render::Light::directional({0,-1,0},{1,1,1}));
    auto result=entity::collectSceneLighting(world,output);CHECK(result.authored==0);CHECK(output.count==0);
    auto* object=world.createSpatialEntity();auto* light=object->createComponent<entity::LightComponent>();light->enabled=false;
    result=entity::collectSceneLighting(world,output);CHECK(result.authored==1&&result.disabled==1&&output.count==0);
    light->enabled=true;result=entity::collectSceneLighting(world,output,std::numeric_limits<float>::infinity());CHECK(result.invalid==1&&output.count==0);
    world.shutdown();
}
TEST_CASE(light_scene_serialization_round_trips_and_refuses_invalid_values)
{
    auto& world=entity::World::processWorld();world.shutdown();CHECK(world.initialize());auto* object=world.createSpatialEntity();auto* light=object->createComponent<entity::LightComponent>();
    light->type=2;light->color={.1f,.4f,.8f};light->range=17;light->innerAngleDegrees=15;light->outerAngleDegrees=35;light->priority=8;
    auto writer=serializer::createSerializer(serializer::Format::Json);writer->beginObject(nullptr);
    entity::ComponentFactory::serializeComponent(*writer,*light);writer->endObject();CHECK(writer->lastError().ok());
    auto reader=serializer::createSerializer(serializer::Format::Json);reader->deserialize(writer->output());reader->beginObject(nullptr);
    entity::LightComponent restored;CHECK(entity::ComponentFactory::deserializeComponent(*reader,"LightComponent",restored));reader->endObject();
    CHECK(reader->lastError().ok());CHECK(restored.type==2&&restored.range==17&&restored.outerAngleDegrees==35&&restored.priority==8);
    light->range=-1;auto bad=serializer::createSerializer(serializer::Format::Json);bad->beginObject(nullptr);
    entity::ComponentFactory::serializeComponent(*bad,*light);bad->endObject();CHECK_FALSE(bad->lastError().ok());world.shutdown();
}
TEST_CASE(registered_render_system_publishes_owned_lighting_and_clears_it_after_component_removal)
{
    auto& world=entity::World::processWorld();world.shutdown();CHECK(world.initialize());
    const auto* previous=entity::World::activeWorld();entity::World::setActiveWorld(&world);
    render::RendererSubSystem::setWindowProvider({});render::RendererSubSystem::setBootstrapBackend(render::Backend::Noop);
    static int sentinel=0;render::RendererSubSystem::setBootstrapWindow(&sentinel,64,64);
    auto* renderer=new render::RendererSubSystem();game::SubSystemRegistry::instance().registerSubSystem(renderer);
    const bool initialized=renderer->initialize();CHECK(initialized);
    if(initialized){
        entity::RenderSystem system;system.onStart();auto* object=world.createSpatialEntity();
        auto* component=object->createComponent<entity::LightComponent>();component->type=1;component->intensity=5;
        renderer->update(0);const auto* lights=renderer->renderScene().sceneLights();CHECK(lights&&lights->count==1);
        if(lights)CHECK(lights->lights[0].type==render::LightType::Point);
        const auto snapshot=renderer->renderScene();component->enabled=false;renderer->update(0);
        lights=renderer->renderScene().sceneLights();CHECK(lights&&lights->count==0);
        world.destroyEntity(object);renderer->update(0);CHECK(renderer->renderScene().sceneLights()==nullptr);
        CHECK(snapshot.sceneLights()&&snapshot.sceneLights()->count==1);CHECK(snapshot.sceneLights()->lights[0].intensity==5);
        renderer->clearSceneBuildersForOwner(&world);renderer->shutdown();
    }
    game::SubSystemRegistry::instance().unregisterSubSystem("Renderer");world.shutdown();
    entity::World::setActiveWorld(const_cast<entity::World*>(previous));
}
TEST_SUITE_END
