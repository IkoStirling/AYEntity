#include <AYEntity.h>
#include <AYEntity/ComponentFactory.h>
#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/components/HealthComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYModule/ModuleContext.h>
#include <AYModule/ModuleManager.h>
#include <AYTest.h>

#include <cstring>

namespace ayt::entity::test
{
namespace
{

class LateComponent final : public IComponent
{
public:
    const char* getName() const override { return "LateComponent"; }
};

} // namespace

TEST_SUITE(EntityComponentModuleTests)

TEST_CASE(module_registers_types_before_the_host_seals_the_registry)
{
    ComponentRegistry& registry = ComponentRegistry::instance();
    CHECK_FALSE(registry.isSealed());

    ayt::module::ModuleContext context;
    ayt::module::ModuleManager modules;
    CHECK_TRUE(modules.emplace<EntityComponentModule>(registry).succeeded());
    CHECK_TRUE(modules.resolve().succeeded());
    CHECK_TRUE(modules.registerTypes(context).succeeded());
    CHECK(modules.phase()
        == ayt::module::ModuleManagerPhase::TypesRegistered);

    CHECK_NOT_NULL(registry.find<Transform>());
    CHECK_NOT_NULL(registry.find("BlendSpaceComponent"));
    const ComponentDescriptor* health = registry.find<HealthComponent>();
    CHECK_NOT_NULL(health);
    CHECK(health->name == "HealthComponent");
    CHECK_TRUE(health->sceneSerializable);

    registry.seal();
    CHECK_TRUE(registry.isSealed());
    ComponentRegistryResult late = registerComponent<LateComponent>(
        registry,
        "test.LateComponent");
    CHECK_FALSE(late.succeeded());
    CHECK(late.code() == ComponentRegistryError::Sealed);

    CHECK_TRUE(modules.install(context).succeeded());
    CHECK(modules.phase() == ayt::module::ModuleManagerPhase::Installed);

    World& world = World::processWorld();
    world.initialize();
    Entity* entity = world.createEntity();
    CHECK_NOT_NULL(entity);
    CHECK_NOT_NULL(entity->addComponent<Transform>());
    HealthComponent* healthComponent = entity->addComponent<HealthComponent>();
    CHECK_NOT_NULL(healthComponent);
    CHECK(std::strcmp(
        ComponentFactory::registeredTypeName(*healthComponent),
        "HealthComponent") == 0);
    world.destroyEntity(entity);
    world.shutdown();

    modules.shutdown(context);
    CHECK(modules.phase() == ayt::module::ModuleManagerPhase::Shutdown);
}

TEST_SUITE_END

} // namespace ayt::entity::test
