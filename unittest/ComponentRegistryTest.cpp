#include <AYEntity.h>
#include <AYEntity/ComponentFactory.h>
#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYEntity/components/HealthComponent.h>
#include <AYTest.h>

#include <cstring>

namespace ayt::entity::test
{
namespace
{

class RegistryProbeComponent final : public IComponent
{
public:
    const char* getName() const override { return "RegistryProbeComponent"; }
};

class RegistryOtherComponent final : public IComponent
{
public:
    const char* getName() const override { return "RegistryOtherComponent"; }
};

class ExplicitUseComponent final : public IComponent
{
public:
    const char* getName() const override { return "ExplicitUseComponent"; }
};

} // namespace

TEST_SUITE(ComponentRegistryTests)

TEST_CASE(registration_is_explicit_idempotent_and_sealable)
{
    ComponentRegistry registry;

    ComponentRegistryResult first = registerComponent<RegistryProbeComponent>(
        registry,
        "test.RegistryProbe",
        "Registry Probe",
        "Tests");
    CHECK_TRUE(first.succeeded());
    CHECK(registry.size() == 1);

    const ComponentDescriptor* descriptor =
        registry.find<RegistryProbeComponent>();
    CHECK_NOT_NULL(descriptor);
    CHECK(descriptor->name == "test.RegistryProbe");
    CHECK(descriptor->displayName == "Registry Probe");
    CHECK(descriptor->category == "Tests");
    CHECK_TRUE(descriptor->editorAddable);
    CHECK_FALSE(descriptor->sceneSerializable);
    CHECK(registry.find("test.RegistryProbe") == descriptor);

    ComponentRegistryResult duplicate = registerComponent<RegistryProbeComponent>(
        registry,
        "test.RegistryProbe",
        "Registry Probe",
        "Tests");
    CHECK_TRUE(duplicate.succeeded());
    CHECK(registry.size() == 1);

    ComponentRegistryResult duplicateName =
        registerComponent<RegistryOtherComponent>(
            registry,
            "test.RegistryProbe",
            "Other",
            "Tests");
    CHECK_FALSE(duplicateName.succeeded());
    CHECK(duplicateName.code() == ComponentRegistryError::DuplicateName);

    ComponentRegistryResult duplicateType =
        registerComponent<RegistryProbeComponent>(
            registry,
            "test.RegistryProbeAlias",
            "Registry Probe",
            "Tests");
    CHECK_FALSE(duplicateType.succeeded());
    CHECK(duplicateType.code() == ComponentRegistryError::DuplicateType);

    registry.seal();
    CHECK_TRUE(registry.isSealed());
    ComponentRegistryResult afterSeal = registerComponent<RegistryOtherComponent>(
        registry,
        "test.RegistryOther");
    CHECK_FALSE(afterSeal.succeeded());
    CHECK(afterSeal.code() == ComponentRegistryError::Sealed);
}

TEST_CASE(typed_add_checks_registration_only_when_creating_storage)
{
    ComponentRegistry& registry = ComponentRegistry::instance();
    CHECK_FALSE(registry.isSealed());
    CHECK(registry.find<ExplicitUseComponent>() == nullptr);

    World& world = World::processWorld();
    world.initialize();
    Entity* entity = world.createEntity();
    CHECK_NOT_NULL(entity);
    CHECK(entity->addComponent<ExplicitUseComponent>() == nullptr);

    ComponentRegistryResult registration =
        registerComponent<ExplicitUseComponent>(
            registry,
            "test.ExplicitUse",
            "Explicit Use",
            "Tests");
    CHECK_TRUE(registration.succeeded());
    CHECK_NOT_NULL(entity->addComponent<ExplicitUseComponent>());
    CHECK_NOT_NULL(entity->addComponent<ExplicitUseComponent>());

    world.destroyEntity(entity);
    world.shutdown();
}

TEST_CASE(factory_uses_registry_canonical_name)
{
    HealthComponent health;
    CHECK(std::strcmp(health.getName(), "Health") == 0);
    const char* registeredName =
        ComponentFactory::registeredTypeName(health);
    CHECK_NOT_NULL(registeredName);
    CHECK(std::strcmp(registeredName, "HealthComponent") == 0);
    CHECK_TRUE(ComponentFactory::isSceneSerializable(registeredName));
}

TEST_SUITE_END

} // namespace ayt::entity::test
