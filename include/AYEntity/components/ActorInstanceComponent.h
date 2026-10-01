#pragma once

#include <AYEntity/IEntity.h>
#include <AYEntity/components/ScriptComponent.h>

#include <string>
#include <unordered_map>
#include <variant>

namespace ayt::entity {

// Scene identity and authoring deltas for an Actor class instance. The class
// expands into ordinary ECS components when a Scene is loaded.
#define AY_CURRENT_CLASS ActorInstanceComponent
struct ActorInstanceComponent : IComponent {
    const char* getName() const override { return "ActorInstanceComponent"; }
    AY_PROPERTY(std::string, classPath, kAttrSerialize)
    AY_PROPERTY(std::string, instanceId, kAttrSerialize)
    AY_PROPERTY(std::string, componentOverridesJson, kAttrSerialize)
    AY_PROPERTY(std::string, propertyOverridesJson, kAttrSerialize)

    // Set by the scene loader or explicit spawner; never written to a Scene.
    std::string assetsRoot;
    // Baseline at expansion time, for computing authored deltas if the class
    // file changes while this Scene remains open. Not serialized.
    std::string classDefaultsJson;
    bool runtimeBindingAttempted = false;

    ActorInstanceComponent()
        : componentOverridesJson("{}"), propertyOverridesJson("{}") {}
};
#undef AY_CURRENT_CLASS

using ActorPropertyValue = std::variant<double, bool, std::string>;

// Runtime-only script receiver. Its fields are presented to Logia as a Lua
// table, then copied back after each lifecycle call. No C++ subclass is needed.
struct ActorScriptComponent final : ScriptComponent {
    const char* getName() const override { return "ActorScriptComponent"; }
    std::unordered_map<std::string, ActorPropertyValue> properties;
};

} // namespace ayt::entity
