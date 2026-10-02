#include <AYEntity/ActorClassAsset.h>
#include <AYAssetFormat/AssetFormat.h>

#include <AYEntity/ComponentFactory.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/World.h>
#include <AYEntity/components/ActorInstanceComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYIO/File.h>
#include <AYSerializer/SerializerCore.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace ayt::entity {
namespace {

using Json = nlohmann::json;
constexpr std::uintmax_t kMaxActorBytes = 1024u * 1024u;
constexpr std::size_t kMaxActorInheritanceDepth = 32u;

void fail(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
}

bool identifier(const std::string& value)
{
    if (value.empty() || !(std::isalpha(static_cast<unsigned char>(value[0]))
                           || value[0] == '_')) return false;
    return std::all_of(value.begin() + 1, value.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_';
    });
}

std::string slotId(const ActorComponentDefault& component) {
    return component.instanceId.empty()
        ? legacyComponentInstanceId(component.type) : component.instanceId;
}

std::string removedSlotId(const std::string& value) {
    return isValidComponentInstanceId(value)
        ? value : legacyComponentInstanceId(value);
}

bool portablePath(const std::string& value, const std::string& suffix)
{
    if (value.empty() || value.find('\\') != std::string::npos
        || value.find(':') != std::string::npos
        || value.front() == '/' || value.back() == '/') return false;
    const std::filesystem::path path(value);
    const bool extensionMatches = suffix == ayt::asset_format::suffix(ayt::asset_format::Id::ActorClass)
        ? ayt::asset_format::matchesPath(value, ayt::asset_format::Id::ActorClass)
        : path.extension() == suffix;
    if (path.is_absolute() || !extensionMatches) return false;
    for (const auto& part : path) {
        if (part == "." || part == ".." || part.empty()) return false;
    }
    return true;
}

bool resolveAssetPath(const std::string& assetsRoot,
                      const std::string& relative,
                      const std::string& suffix,
                      std::string& absolutePath, std::string* error)
{
    if (assetsRoot.empty() || !portablePath(relative, suffix)) {
        fail(error, "Actor asset path must be asset-root-relative " + suffix);
        return false;
    }
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(assetsRoot, ec);
    if (ec) {
        fail(error, "cannot resolve Actor asset root");
        return false;
    }
    const auto target = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec || std::mismatch(root.begin(), root.end(),
                            target.begin(), target.end()).first != root.end()) {
        fail(error, "Actor asset escapes asset root");
        return false;
    }
    absolutePath = target.string();
    if (error) error->clear();
    return true;
}

bool validate(const ActorClassAsset& asset, std::string* error)
{
    if (!identifier(asset.id)) {
        fail(error, "Actor class id must be a Logia identifier");
        return false;
    }
    if (!asset.scriptPath.empty() && !portablePath(asset.scriptPath, ".logia")) {
        fail(error, "Actor script must be an asset-root-relative .logia path");
        return false;
    }
    if (!asset.parentPath.empty() && !portablePath(asset.parentPath, ".act")) {
        fail(error, "Actor parent must be an asset-root-relative .act path");
        return false;
    }
    if (asset.parentPath.empty()
        && (!asset.removedComponents.empty() || !asset.removedProperties.empty())) {
        fail(error, "Root Actor class cannot remove inherited defaults");
        return false;
    }
    try {
        const Json properties = Json::parse(asset.propertiesJson);
        if (!properties.is_object()) throw std::runtime_error("properties is not an object");
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            if (!identifier(it.key()) || it.key() == "transform"
                || !(it.value().is_boolean()
                || it.value().is_number() || it.value().is_string())) {
                throw std::runtime_error("properties must have identifier keys and primitive values");
            }
        }
        std::unordered_set<std::string> types;
        std::unordered_set<std::string> ids;
        for (const auto& component : asset.components) {
            const auto* descriptor = ComponentRegistry::instance().find(component.type);
            if (descriptor == nullptr || !descriptor->sceneSerializable
                || component.type == "ActorInstanceComponent") {
                throw std::runtime_error("unsupported Actor component: "
                    + component.type);
            }
            if (!isValidComponentInstanceId(slotId(component))
                || !ids.insert(slotId(component)).second
                || (descriptor->multiplicity == ComponentMultiplicity::Single
                    && !types.insert(component.type).second)) {
                throw std::runtime_error("duplicate or invalid Actor component identity: "
                    + component.type);
            }
            if (!Json::parse(component.payloadJson).is_object()) {
                throw std::runtime_error("Actor component payload must be an object: "
                    + component.type);
            }
        }
        std::unordered_set<std::string> removals;
        for (const auto& removed : asset.removedComponents) {
            const std::string id = removedSlotId(removed);
            if (removed == "Transform" || removed == "ActorInstanceComponent"
                || !isValidComponentInstanceId(id) || ids.contains(id)
                || !removals.insert(id).second) {
                throw std::runtime_error("duplicate or invalid removed Actor component: " + removed);
            }
        }
        std::unordered_set<std::string> propertyRemovals;
        for (const auto& name : asset.removedProperties) {
            if (!identifier(name) || properties.contains(name)
                || !propertyRemovals.insert(name).second) {
                throw std::runtime_error("duplicate or invalid removed Actor property: " + name);
            }
        }
    } catch (const std::exception& ex) {
        fail(error, ex.what());
        return false;
    }
    if (error) error->clear();
    return true;
}

bool compatibleProperty(const Json& base, const Json& value)
{
    return (base.is_number() && value.is_number())
        || (base.is_boolean() && value.is_boolean())
        || (base.is_string() && value.is_string());
}

bool validatePropertyOverrides(const ActorClassAsset& asset,
                               const std::string& encoded,
                               std::string* error)
{
    try {
        const Json defaults = Json::parse(asset.propertiesJson);
        const Json overrides = Json::parse(encoded);
        if (!overrides.is_object()) throw std::runtime_error("Actor property overrides must be an object");
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            if (!defaults.contains(it.key())
                || !compatibleProperty(defaults[it.key()], it.value())) {
                throw std::runtime_error("invalid Actor property override: " + it.key());
            }
        }
    } catch (const std::exception& ex) {
        fail(error, ex.what());
        return false;
    }
    return true;
}

bool readComponent(Entity& entity, const std::string& type,
                   const std::string& payload, const std::string& instanceId,
                   const std::string& displayName, std::string* error)
{
    const auto* descriptor = ComponentRegistry::instance().find(type);
    if (!descriptor || !descriptor->sceneSerializable) {
        fail(error, "unsupported Actor component: " + type);
        return false;
    }
    IComponent* component = instanceId.empty()
        ? nullptr : entity.findComponentById(instanceId);
    if (component) {
        const char* existingType = ComponentFactory::registeredTypeName(*component);
        if (!existingType || type != existingType) {
            fail(error, "Actor component ID is bound to another type: " + instanceId);
            return false;
        }
    }
    if (!component) component = ComponentFactory::addComponent(entity, type.c_str());
    if (!component) {
        fail(error, "cannot add Actor component: " + type);
        return false;
    }
    if (!instanceId.empty()
        && !entity.setComponentInstanceId(component, instanceId)) {
        fail(error, "invalid Actor component instance ID: " + instanceId);
        return false;
    }
    if (!entity.setComponentDisplayName(component, displayName)) return false;
    auto reader = ayt::serializer::createSerializer(
        ayt::serializer::Format::Json);
    reader->deserialize(payload);
    reader->beginObject(nullptr);
    const bool ok = ComponentFactory::deserializeComponent(
        *reader, type.c_str(), *component);
    reader->endObject();
    if (!ok || !reader->lastError().ok()) {
        fail(error, "cannot read Actor component: " + type);
        return false;
    }
    ComponentFactory::afterSceneDeserialize(entity, type.c_str(), *component);
    return true;
}

bool writeComponent(const IComponent& component, Json& out)
{
    auto writer = ayt::serializer::createSerializer(
        ayt::serializer::Format::Json);
    writer->beginObject(nullptr);
    ComponentFactory::serializeComponent(*writer, component);
    writer->endObject();
    if (!writer->lastError().ok()) return false;
    try { out = Json::parse(writer->output()); }
    catch (...) { return false; }
    return out.is_object();
}

bool collectSerializableComponents(const Entity& entity, Json& out,
                                   std::string* error)
{
    out = Json::object();
    for (IComponent* component : entity.getComponents()) {
        if (!component) continue;
        const char* type = ComponentFactory::registeredTypeName(*component);
        if (!type || std::string(type) == "ActorInstanceComponent"
            || !ComponentFactory::isSceneSerializable(type)) continue;
        Json payload;
        if (!writeComponent(*component, payload)) {
            fail(error, "cannot capture Actor component: " + std::string(type));
            return false;
        }
        const auto* instance = entity.componentInstance(component);
        if (!instance || !isValidComponentInstanceId(instance->id)) {
            fail(error, "Actor component has no stable instance ID: " + std::string(type));
            return false;
        }
        out[instance->id] = {{"$type", type},
                             {"$displayName", instance->displayName},
                             {"data", std::move(payload)}};
    }
    if (!out.contains(legacyComponentInstanceId("Transform"))) {
        fail(error, "Actor is missing Transform");
        return false;
    }
    return true;
}

bool applyDefaults(Entity& entity, const ActorClassAsset& asset,
                   std::string* error)
{
    auto* transform = entity.addComponent<Transform>();
    if (!transform || !entity.setComponentInstanceId(
            transform, legacyComponentInstanceId("Transform"))) {
        fail(error, "Actor requires registered Transform component");
        return false;
    }
    for (const auto& entry : asset.components) {
        if (!readComponent(entity, entry.type, entry.payloadJson,
                           slotId(entry), entry.displayName, error)) return false;
    }
    return true;
}

bool applyOverrides(Entity& entity, const std::string& encoded,
                    std::string* error)
{
    try {
        const Json overrides = Json::parse(encoded);
        if (!overrides.is_object()) throw std::runtime_error("Actor overrides must be an object");
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            const bool legacyKey = !isValidComponentInstanceId(it.key());
            const std::string id = legacyKey
                ? legacyComponentInstanceId(it.key()) : it.key();
            const Json& change = it.value();
            if (!change.is_object())
                throw std::runtime_error("invalid Actor override: " + it.key());
            const std::string op = change.value("op", std::string{});
            IComponent* existing = entity.findComponentById(id);
            const char* existingType = existing
                ? ComponentFactory::registeredTypeName(*existing) : nullptr;
            const std::string type = legacyKey ? it.key()
                : change.value("type", existingType ? std::string(existingType) : std::string{});
            const auto* descriptor = ComponentRegistry::instance().find(type);
            if (!descriptor || !descriptor->sceneSerializable
                || type == "ActorInstanceComponent"
                || (existingType && type != existingType)) {
                throw std::runtime_error("invalid Actor override type: " + it.key());
            }
            if (op == "remove") {
                if (type == "Transform" || !existing) {
                    throw std::runtime_error("cannot remove Actor root component: " + type);
                }
                if (!entity.removeComponentById(id)) return false;
            } else if (op == "add") {
                if (!change.contains("value") || !change["value"].is_object()
                    || !readComponent(entity, type, change["value"].dump(),
                                      id, change.value("displayName", std::string{}),
                                      error)) return false;
            } else if (op == "patch") {
                if (!change.contains("value") || !change["value"].is_array())
                    throw std::runtime_error("invalid patch: " + type);
                Json base;
                if (!existing || !writeComponent(*existing, base)) {
                    throw std::runtime_error("missing patch base: " + type);
                }
                const Json patched = base.patch(change["value"]);
                const auto* instance = entity.componentInstance(existing);
                const std::string displayName = change.value("displayName",
                    instance ? instance->displayName : std::string{});
                if (!readComponent(entity, type, patched.dump(), id,
                                   displayName, error)) return false;
            } else {
                throw std::runtime_error("unknown Actor override operation: " + op);
            }
        }
    } catch (const std::exception& ex) {
        fail(error, ex.what());
        return false;
    }
    return true;
}

std::string makeInstanceId()
{
    static std::atomic<std::uint64_t> serial{1};
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(8) << random()
        << std::setw(8) << random() << std::setw(16) << serial.fetch_add(1);
    return out.str();
}

bool mergeActorClass(const ActorClassAsset& parent,
                     const ActorClassAsset& child,
                     ActorClassAsset& out, std::string* error)
{
    try {
        ActorClassAsset result = parent;
        result.id = child.id;
        result.parentPath.clear();
        result.removedComponents.clear();
        result.removedProperties.clear();
        if (!child.scriptPath.empty()) result.scriptPath = child.scriptPath;

        Json properties = Json::parse(parent.propertiesJson);
        const Json childProperties = Json::parse(child.propertiesJson);
        for (const auto& name : child.removedProperties) {
            if (!properties.contains(name)) {
                throw std::runtime_error("Actor removes unknown inherited property: " + name);
            }
            properties.erase(name);
        }
        for (auto it = childProperties.begin(); it != childProperties.end(); ++it) {
            if (properties.contains(it.key())
                && !compatibleProperty(properties[it.key()], it.value())) {
                throw std::runtime_error("Actor changes inherited property type: " + it.key());
            }
            properties[it.key()] = it.value();
        }
        result.propertiesJson = properties.dump();

        for (const auto& removed : child.removedComponents) {
            const std::string id = removedSlotId(removed);
            const auto found = std::find_if(result.components.begin(),
                result.components.end(), [&](const auto& item) { return slotId(item) == id; });
            if (found == result.components.end()) {
                throw std::runtime_error("Actor removes unknown inherited component: " + removed);
            }
            result.components.erase(found);
        }
        for (const auto& component : child.components) {
            const auto found = std::find_if(result.components.begin(),
                result.components.end(), [&](const auto& item) {
                    return slotId(item) == slotId(component);
                });
            if (found == result.components.end()) {
                result.components.push_back(component);
            } else {
                if (found->type != component.type) {
                    throw std::runtime_error("Actor changes an inherited component type: "
                        + slotId(component));
                }
                Json payload = Json::parse(found->payloadJson);
                payload.merge_patch(Json::parse(component.payloadJson));
                found->payloadJson = payload.dump();
                if (!component.displayName.empty())
                    found->displayName = component.displayName;
            }
        }
        if (!validate(result, error)) return false;
        out = std::move(result);
        return true;
    } catch (const std::exception& ex) {
        fail(error, ex.what());
        return false;
    }
}

bool resolveActorRecursive(const std::string& assetsRoot,
                           const std::string& classPath,
                           std::unordered_set<std::string>& active,
                           ActorClassAsset& out, std::string* error)
{
    std::string absolute;
    if (!resolveActorClassPath(assetsRoot, classPath, absolute, error)) return false;
    if (active.size() >= kMaxActorInheritanceDepth) {
        fail(error, "Actor inheritance exceeds 32 classes: " + classPath);
        return false;
    }
    if (!active.insert(absolute).second) {
        fail(error, "Actor inheritance cycle at " + classPath);
        return false;
    }
    ActorClassAsset child;
    if (!loadActorClassAsset(absolute, child, error)) {
        active.erase(absolute);
        return false;
    }
    if (child.parentPath.empty()) {
        out = std::move(child);
        active.erase(absolute);
        return true;
    }
    ActorClassAsset parent;
    const bool resolved = resolveActorRecursive(assetsRoot, child.parentPath,
                                               active, parent, error)
        && mergeActorClass(parent, child, out, error);
    active.erase(absolute);
    return resolved;
}

} // namespace

bool parseActorClassAsset(const std::string& source,
                          ActorClassAsset& out, std::string* error)
{
    if (source.size() > kMaxActorBytes) {
        fail(error, "Actor class exceeds 1 MiB");
        return false;
    }
    try {
        const Json wire = Json::parse(source);
        const int schema = wire.value("schemaVersion", 0);
        if (!wire.is_object() || wire.value("type", std::string{}) != "ay.actorClass"
            || (schema != 1 && schema != 2 && schema != 3)) {
            throw std::runtime_error("unsupported Actor class type or schema");
        }
        ActorClassAsset candidate;
        candidate.id = wire.at("id").get<std::string>();
        if (schema >= 2) {
            candidate.parentPath = wire.value("parent", std::string{});
            candidate.removedComponents = wire.value("removeComponents",
                std::vector<std::string>{});
            if (schema == 2) {
                for (auto& removed : candidate.removedComponents)
                    removed = removedSlotId(removed);
            }
            candidate.removedProperties = wire.value("removeProperties",
                std::vector<std::string>{});
        }
        candidate.scriptPath = wire.value("script", std::string{});
        candidate.propertiesJson = wire.value("properties", Json::object()).dump();
        if (wire.contains("components")) {
            if (!wire["components"].is_array()) throw std::runtime_error("components must be an array");
            for (const Json& item : wire["components"]) {
                if (!item.is_object()) throw std::runtime_error("component must be an object");
                ActorComponentDefault entry;
                entry.type = item.at("$type").get<std::string>();
                entry.instanceId = schema >= 3
                    ? item.at("$instanceId").get<std::string>()
                    : legacyComponentInstanceId(entry.type);
                entry.displayName = schema >= 3
                    ? item.value("$displayName", std::string{}) : std::string{};
                Json payload = item;
                payload.erase("$type");
                payload.erase("$instanceId");
                payload.erase("$displayName");
                entry.payloadJson = payload.dump();
                candidate.components.push_back(std::move(entry));
            }
        }
        if (!validate(candidate, error)) return false;
        out = std::move(candidate);
    } catch (const std::exception& ex) {
        fail(error, ex.what());
        return false;
    }
    if (error) error->clear();
    return true;
}

bool loadActorClassAsset(const std::string& absolutePath,
                         ActorClassAsset& out, std::string* error)
{
    if (!ayt::asset_format::matchesPath(absolutePath, ayt::asset_format::Id::ActorClass)
        || !ayt::io::File::exists(absolutePath)) {
        fail(error, "Actor class file is missing or has the wrong extension");
        return false;
    }
    const auto attributes = ayt::io::File::queryAttributes(absolutePath);
    if (attributes.size > kMaxActorBytes) {
        fail(error, "Actor class exceeds 1 MiB");
        return false;
    }
    return parseActorClassAsset(ayt::io::File::readAllText(absolutePath),
                                out, error);
}

bool saveActorClassAsset(const std::string& absolutePath,
                         const ActorClassAsset& asset, std::string* error)
{
    if (!ayt::asset_format::isCanonicalPath(absolutePath, ayt::asset_format::Id::ActorClass)
        || !validate(asset, error)) return false;
    try {
        Json wire = {{"type", "ay.actorClass"}, {"schemaVersion", 3},
                     {"id", asset.id}, {"script", asset.scriptPath},
                     {"properties", Json::parse(asset.propertiesJson)}};
        if (!asset.parentPath.empty()) wire["parent"] = asset.parentPath;
        if (!asset.removedComponents.empty()) {
            wire["removeComponents"] = Json::array();
            for (const auto& removed : asset.removedComponents)
                wire["removeComponents"].push_back(removedSlotId(removed));
        }
        if (!asset.removedProperties.empty())
            wire["removeProperties"] = asset.removedProperties;
        wire["components"] = Json::array();
        for (const auto& entry : asset.components) {
            Json payload = Json::parse(entry.payloadJson);
            payload["$type"] = entry.type;
            payload["$instanceId"] = slotId(entry);
            payload["$displayName"] = entry.displayName;
            wire["components"].push_back(std::move(payload));
        }
        const std::string encoded = wire.dump(2) + '\n';
        if (encoded.size() > kMaxActorBytes
            || !ayt::io::File::atomicWrite(
                absolutePath, encoded.data(), encoded.size())) {
            fail(error, "cannot write Actor class file");
            return false;
        }
    } catch (const std::exception& ex) {
        fail(error, ex.what());
        return false;
    }
    if (error) error->clear();
    return true;
}

std::string assetsRootForScene(const std::string& scenePath)
{
    std::error_code ec;
    auto path = std::filesystem::absolute(scenePath, ec).parent_path();
    if (ec) return {};
    while (!path.empty()) {
        std::string name = path.filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "assets" || name == "content") return path.string();
        const auto parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

bool resolveActorClassPath(const std::string& assetsRoot,
                           const std::string& classPath,
                           std::string& absolutePath, std::string* error)
{
    return resolveAssetPath(assetsRoot, classPath, ".act",
                            absolutePath, error);
}

bool resolveActorScriptPath(const std::string& assetsRoot,
                            const std::string& scriptPath,
                            std::string& absolutePath, std::string* error)
{
    return resolveAssetPath(assetsRoot, scriptPath, ".logia",
                            absolutePath, error);
}

bool resolveActorClassAsset(const std::string& assetsRoot,
                            const std::string& classPath,
                            ActorClassAsset& out, std::string* error)
{
    std::unordered_set<std::string> active;
    return resolveActorRecursive(assetsRoot, classPath, active, out, error);
}

bool instantiateActorClass(Entity& entity, const ActorClassAsset& asset,
                           const std::string& classPath,
                           const std::string& assetsRoot, std::string* error)
{
    std::string absolute;
    if (!resolveActorClassPath(assetsRoot, classPath, absolute, error)
        || !validate(asset, error)
        || entity.hasComponent<ActorInstanceComponent>()) return false;
    ActorClassAsset effective = asset;
    if (!asset.parentPath.empty()
        && !resolveActorClassAsset(assetsRoot, classPath, effective, error)) return false;
    auto* instance = entity.addComponent<ActorInstanceComponent>();
    if (!instance) {
        fail(error, "ActorInstanceComponent is not registered");
        return false;
    }
    instance->classPath = classPath;
    instance->instanceId = makeInstanceId();
    instance->assetsRoot = assetsRoot;
    if (!applyDefaults(entity, effective, error)) return false;
    Json defaults;
    if (!collectSerializableComponents(entity, defaults, error)) return false;
    instance->classDefaultsJson = defaults.dump();
    return true;
}

bool expandActorInstance(Entity& entity, ActorInstanceComponent& instance,
                         std::string* error)
{
    ActorClassAsset asset;
    if (!resolveActorClassAsset(instance.assetsRoot, instance.classPath,
                                asset, error)) return false;
    if (!validatePropertyOverrides(asset, instance.propertyOverridesJson, error))
        return false;
    if (instance.instanceId.empty()) instance.instanceId = makeInstanceId();
    if (!applyDefaults(entity, asset, error)) return false;
    Json defaults;
    if (!collectSerializableComponents(entity, defaults, error)) return false;
    instance.classDefaultsJson = defaults.dump();
    return applyOverrides(entity, instance.componentOverridesJson, error);
}

bool captureActorOverrides(const Entity& entity,
                           ActorInstanceComponent& instance,
                           std::string* error)
{
    ActorClassAsset asset;
    if (!resolveActorClassAsset(instance.assetsRoot, instance.classPath,
                                asset, error)) return false;
    if (!validatePropertyOverrides(asset, instance.propertyOverridesJson, error))
        return false;
    Json defaults;
    if (instance.classDefaultsJson.empty()) {
        World* baselineWorld = entity.getWorld();
        if (!baselineWorld) {
            fail(error, "Actor entity has no World");
            return false;
        }
        Entity* baseline = baselineWorld->createEntity();
        if (!baseline) return false;
        const bool collected = applyDefaults(*baseline, asset, error)
            && collectSerializableComponents(*baseline, defaults, error);
        baselineWorld->destroyEntity(baseline);
        if (!collected) return false;
    } else {
        try { defaults = Json::parse(instance.classDefaultsJson); }
        catch (const std::exception& ex) {
            fail(error, ex.what());
            return false;
        }
        if (!defaults.is_object()) {
            fail(error, "Actor default snapshot is invalid");
            return false;
        }
    }
    Json current;
    if (!collectSerializableComponents(entity, current, error)) return false;
    std::unordered_set<std::string> latestIds{legacyComponentInstanceId("Transform")};
    for (const auto& component : asset.components) latestIds.insert(slotId(component));
    Json overrides = Json::object();
    for (auto entry = defaults.begin(); entry != defaults.end(); ++entry) {
        const std::string id = entry.key();
        const Json& base = entry.value();
        const auto it = current.find(id);
        if (it == current.end()) overrides[id] = {{"op", "remove"},
                                                   {"type", base.at("$type")}};
        else if (it.value() != base) {
            if (it.value().at("$type") != base.at("$type")) {
                fail(error, "Actor component ID changed type: " + id);
                return false;
            }
            if (!latestIds.contains(id)) {
                overrides[id] = {{"op", "add"},
                                 {"type", it.value().at("$type")},
                                 {"displayName", it.value().at("$displayName")},
                                 {"value", it.value().at("data")}};
            } else {
                overrides[id] = {{"op", "patch"},
                                 {"type", it.value().at("$type")},
                                 {"value", Json::diff(base.at("data"),
                                                       it.value().at("data"))}};
                if (it.value().at("$displayName") != base.at("$displayName"))
                    overrides[id]["displayName"] = it.value().at("$displayName");
            }
        }
    }
    for (auto entry = current.begin(); entry != current.end(); ++entry) {
        const std::string id = entry.key();
        if (!defaults.contains(id)) {
            overrides[id] = {{"op", "add"},
                             {"type", entry.value().at("$type")},
                             {"displayName", entry.value().at("$displayName")},
                             {"value", entry.value().at("data")}};
        }
    }
    instance.componentOverridesJson = overrides.dump();
    if (error) error->clear();
    return true;
}

} // namespace ayt::entity
