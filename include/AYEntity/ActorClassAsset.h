#pragma once

#include <string>
#include <vector>

namespace ayt::entity {

class Entity;
struct ActorInstanceComponent;

struct ActorComponentDefault {
    std::string type;
    std::string payloadJson;
    // Stable slot identity within an Actor class and each instantiated Entity.
    std::string instanceId;
    std::string displayName;
};

struct ActorClassAsset {
    std::string id;
    // Optional asset-root-relative parent class. Data defaults are inherited;
    // a child script replaces the parent's behavior as a whole.
    std::string parentPath;
    // Asset-root-relative .logia path. Empty inherits the parent's behavior
    // when parentPath is set; otherwise the class has no behavior.
    std::string scriptPath;
    // Object of primitive property defaults, keyed by Logia self field name.
    std::string propertiesJson = "{}";
    std::vector<ActorComponentDefault> components;
    // Schema 3 uses component instance IDs. Legacy callers may still supply a
    // registered type name for the one slot of that type.
    std::vector<std::string> removedComponents;
    std::vector<std::string> removedProperties;
};

/// Parse a schema 1, 2 or 3 `.act` document in memory, including component
/// registration checks. Use this for editor diagnostics before saving.
bool parseActorClassAsset(const std::string& source,
                          ActorClassAsset& out, std::string* error = nullptr);
/// Read and validate a loose `.act` or legacy `.ayactor` authoring asset. The caller provides
/// an absolute path within its project's asset root.
bool loadActorClassAsset(const std::string& absolutePath,
                         ActorClassAsset& out, std::string* error = nullptr);
bool saveActorClassAsset(const std::string& absolutePath,
                         const ActorClassAsset& asset,
                         std::string* error = nullptr);
/// Resolve parent data from the same asset root, detect cycles and return
/// effective component/property defaults. Schema 1 assets load as root classes.
bool resolveActorClassAsset(const std::string& assetsRoot,
                            const std::string& classPath,
                            ActorClassAsset& out,
                            std::string* error = nullptr);
/// Infer a conventional `Assets` or published `Content` ancestor of a Scene.
/// Pass an explicit asset root to Scene::load for custom directory names.
std::string assetsRootForScene(const std::string& scenePath);
bool resolveActorClassPath(const std::string& assetsRoot,
                           const std::string& classPath,
                           std::string& absolutePath,
                           std::string* error = nullptr);
bool resolveActorScriptPath(const std::string& assetsRoot,
                            const std::string& scriptPath,
                            std::string& absolutePath,
                            std::string* error = nullptr);

/// Add the class defaults and an instance identity to a new ECS Entity.
/// `classPath` is relative to `assetsRoot` and survives Scene serialization.
bool instantiateActorClass(Entity& entity, const ActorClassAsset& asset,
                           const std::string& classPath,
                           const std::string& assetsRoot,
                           std::string* error = nullptr);

// Called after ActorInstanceComponent was deserialized from a Scene.
bool expandActorInstance(Entity& entity, ActorInstanceComponent& instance,
                         std::string* error = nullptr);

// Compare expanded components to class defaults; persist only changed JSON
// fields, added components and removals in the instance metadata.
bool captureActorOverrides(const Entity& entity,
                           ActorInstanceComponent& instance,
                           std::string* error = nullptr);

} // namespace ayt::entity
