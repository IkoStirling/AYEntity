#include <AYEntity/EntityRenderIntegrationModule.h>

#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/EntityAnimationIntegrationModule.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYEntity/RenderSystem.h>
#include <AYEntity/PerspectiveCameraUpdateSystem.h>
#include <AYEntity/CameraRuntime.h>
#include <AYEntity/SkinnedMeshRenderSystem.h>
#include <AYEntity/World.h>
#include <AYEntity/WorldLifecycle.h>
#include <AYEntity/components/MeshComponent.h>
#include <AYEntity/components/LightComponent.h>
#include <AYEntity/components/SceneDecalComponent.h>
#include <AYRenderer/RendererRuntimeModule.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYIO/PathSafety.h>

#include <cstring>
#include <string>

namespace ayt::entity
{

// One finalizer in the integration TU; Core stays independent of light schema.
AY_FINALIZE_REGISTRATION_METADATA(LightComponent)
AY_FINALIZE_REGISTRATION_METADATA(SceneDecalComponent)

namespace
{
bool validSceneDecal(const SceneDecalComponent& value) {
    return value.valid()
        && (value.albedoPath.empty() || ayt::io::path::isPortableRelative(value.albedoPath))
        && (value.normalPath.empty() || ayt::io::path::isPortableRelative(value.normalPath));
}

bool hasSystemNamed(const World& world, const char* name)
{
    for (size_t i = 0; i < world.systemCount(); ++i) {
        const char* existing = world.getSystemNameAt(i);
        if (existing != nullptr && std::strcmp(existing, name) == 0) {
            return true;
        }
    }
    return false;
}

void clearWorldRenderCallbacks(World& world) noexcept
{
    if (auto* renderer = ayt::render::RendererSubSystem::findRegistered()) {
        renderer->clearSceneBuildersForOwner(&world);
    }
}

void ensureWorldLifecycleBridge()
{
    static int ownerToken = 0;
    (void)registerWorldBeforeShutdownCallback(
        &ownerToken, &clearWorldRenderCallbacks);
}

} // namespace

ComponentRegistryResult registerEntityRenderComponents(
    ComponentRegistry& registry)
{
    const auto mesh=registerSceneComponent<MeshComponent>(registry,"MeshComponent","Mesh","Rendering");
    if(!mesh) return mesh;
    auto light=detail::makeComponentDescriptor<LightComponent>("LightComponent","Light","Rendering");
    light.requiresSpatial=true;light.sceneSerializable=true;
    light.serialize=[](ayt::serializer::ISerializer& writer,const IComponent& base){
        auto& value=const_cast<LightComponent&>(static_cast<const LightComponent&>(base));
        if(!value.valid()){writer.reportError(ayt::serializer::SerializeError::Code::InvalidInput,"Invalid Scene light values");return;}
        ayt::serializer::SerializerForReflect<LightComponent>::applyFields(writer,value);
    };
    light.deserialize=[](ayt::serializer::ISerializer& reader,IComponent& base){
        auto& value=static_cast<LightComponent&>(base);
        ayt::serializer::SerializerForReflect<LightComponent>::applyReadFields(reader,value);
        if(!value.valid()) reader.reportError(ayt::serializer::SerializeError::Code::InvalidInput,"Invalid Scene light values");
    };
    const auto lightResult=registry.registerComponent(std::move(light));
    if(!lightResult) return lightResult;
    auto decal=detail::makeComponentDescriptor<SceneDecalComponent>(
        "SceneDecalComponent","Scene Decal","Rendering");
    decal.requiresSpatial=true;decal.sceneSerializable=true;
    decal.serialize=[](ayt::serializer::ISerializer& writer,const IComponent& base){
        auto& value=const_cast<SceneDecalComponent&>(static_cast<const SceneDecalComponent&>(base));
        if(!validSceneDecal(value)){
            writer.reportError(ayt::serializer::SerializeError::Code::InvalidInput,
                "Invalid Scene decal values or texture paths");return;
        }
        ayt::serializer::SerializerForReflect<SceneDecalComponent>::applyFields(writer,value);
    };
    decal.deserialize=[](ayt::serializer::ISerializer& reader,IComponent& base){
        auto& value=static_cast<SceneDecalComponent&>(base);
        ayt::serializer::SerializerForReflect<SceneDecalComponent>::applyReadFields(reader,value);
        if(!validSceneDecal(value)) reader.reportError(
            ayt::serializer::SerializeError::Code::InvalidInput,
            "Invalid Scene decal values or texture paths");
    };
    return registry.registerComponent(std::move(decal));
}

void registerEntityRenderSystems()
{
    registerCameraRuntimeSystem();
    ensureWorldLifecycleBridge();
    World& world = World::instance();
    if (!hasSystemNamed(world, "PerspectiveCameraUpdateSystem")) {
        registerPerspectiveCameraUpdateSystem();
    }
    if (!hasSystemNamed(world, "SkinnedMeshRenderSystem")) {
        registerSkinnedMeshRenderSystem();
    }
    if (!hasSystemNamed(world, "RenderSystem")) {
        registerRenderSystem();
    }
}

EntityRenderIntegrationModule::EntityRenderIntegrationModule()
    : _descriptor{
          .id = std::string(kEntityRenderIntegrationModuleId),
          .displayName = "AYEntity Render Integration",
          .version = "0.1.0",
          .dependencies = {
              ayt::module::ModuleDependency::required(
                  std::string(kEntityRuntimeModuleId)),
              ayt::module::ModuleDependency::required(
                  std::string(kEntityAnimationIntegrationModuleId)),
              ayt::module::ModuleDependency::required(
                  std::string(ayt::render::kRendererRuntimeModuleId))}}
{
}

const ayt::module::ModuleDescriptor&
EntityRenderIntegrationModule::descriptor() const noexcept
{
    return _descriptor;
}

ayt::module::ModuleResult EntityRenderIntegrationModule::registerTypes(
    ayt::module::IModuleContext& context)
{
    auto* registry = context.findServiceAs<ComponentRegistry>(
        kComponentRegistryModuleService);
    if (registry == nullptr) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            "ComponentRegistry service is unavailable for "
            "AYEntity.RenderIntegration");
    }
    const ComponentRegistryResult result =
        registerEntityRenderComponents(*registry);
    if (!result) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            result.message());
    }
    return ayt::module::ModuleResult::success();
}

ayt::module::ModuleResult EntityRenderIntegrationModule::install(
    ayt::module::IModuleContext&)
{
    registerEntityRenderSystems();
    return ayt::module::ModuleResult::success();
}

void EntityRenderIntegrationModule::shutdown(
    ayt::module::IModuleContext&) noexcept
{
    // The process-lifetime callback is code-only and resolves Renderer at
    // invocation time. It remains valid for legacy bootstrap users too.
}

} // namespace ayt::entity
