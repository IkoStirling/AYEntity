#include <AYEntity/ParametricSpriteAssembly2D.h>

#include <AYEntity/ParametricSpritePresenter2D.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/World.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TransformComponent.h>

#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace ayt::entity {
namespace {
void setError(std::string* error, std::string value) {
    if (error) *error = std::move(value);
}
}

Entity* ParametricSpriteAssembly2D::root() const noexcept {
    return world_ && world_->isInitialized()
        ? world_->getEntityByHandle(root_) : nullptr;
}

Entity* ParametricSpriteAssembly2D::layer(std::size_t index) const noexcept {
    return world_ && world_->isInitialized() && index < layers_.size()
        ? world_->getEntityByHandle(layers_[index]) : nullptr;
}

bool ParametricSpriteAssembly2D::create(World& world,
    const ay2d::ParametricSpriteRig2D& rig, Entity* spatialParent,
    std::string* error) {
    if (root_.isValid() || !world.isInitialized() || !rig.layerCount() ||
        (spatialParent && (spatialParent->getWorld() != &world ||
                           !spatialParent->isSpatial()))) {
        setError(error, "Empty assembly, active World and Spatial parent required");
        return false;
    }
    for (std::size_t i = 0; i < rig.layerCount(); ++i) {
        if (rig.layer(i).mesh) {
            setError(error, "Managed Sprite assembly requires rigid-only layers");
            return false;
        }
    }
    EntityHandle stagedRoot{};
    std::vector<EntityHandle> stagedLayers;
    std::vector<std::string> stagedNames;
    std::vector<std::int32_t> stagedParents;
    try {
        stagedLayers.reserve(rig.layerCount());
        stagedNames.reserve(rig.layerCount());
        stagedParents.reserve(rig.layerCount());
        auto* avatar = world.createEntity();
        if (!avatar) throw std::runtime_error("Cannot create puppet root");
        stagedRoot = world.getEntityHandle(avatar->getId());
        if (!avatar->addComponent<Transform>())
            throw std::runtime_error("Cannot attach puppet root Transform");
        avatar->setName(("PuppetRoot#" + std::to_string(avatar->getId())).c_str());
        std::string relationError;
        if (spatialParent && !avatar->setSpatialParent(
                spatialParent, ReparentMode::KeepLocal, &relationError))
            throw std::runtime_error(relationError);

        for (std::size_t i = 0; i < rig.layerCount(); ++i) {
            auto* entity = world.createEntity();
            if (!entity) throw std::runtime_error("Cannot create puppet layer");
            stagedLayers.push_back(world.getEntityHandle(entity->getId()));
            if (!entity->addComponent<Transform>() ||
                !entity->addComponent<SpriteComponent>() ||
                !entity->setOwner(avatar, &relationError))
                throw std::runtime_error(relationError.empty()
                    ? "Cannot attach puppet layer components" : relationError);
            const auto& layerName = rig.layer(i).name;
            entity->setName(("Puppet#" + std::to_string(avatar->getId()) +
                ":" + layerName).c_str());
            stagedNames.push_back(layerName);
            stagedParents.push_back(rig.layerParentIndex(i));
        }
        for (std::size_t i = 0; i < stagedLayers.size(); ++i) {
            auto* entity = world.getEntityByHandle(stagedLayers[i]);
            const auto parentIndex = stagedParents[i];
            auto* parent = parentIndex < 0 ? avatar
                : world.getEntityByHandle(stagedLayers[
                    static_cast<std::size_t>(parentIndex)]);
            if (!entity || !parent || !entity->setSpatialParent(
                    parent, ReparentMode::KeepLocal, &relationError))
                throw std::runtime_error(relationError.empty()
                    ? "Cannot attach puppet layer hierarchy" : relationError);
        }
    } catch (const std::exception& ex) {
        // Reverse order also covers a newly created child whose setOwner
        // failed before it could join the root's ownership closure.
        for (auto it = stagedLayers.rbegin(); it != stagedLayers.rend(); ++it) {
            if (auto* entity = world.getEntityByHandle(*it)) {
                try { world.destroyEntity(entity); } catch (...) {}
            }
        }
        if (auto* avatar = world.getEntityByHandle(stagedRoot)) {
            try { world.destroyEntity(avatar); } catch (...) {}
        }
        setError(error, ex.what());
        return false;
    }
    world_ = &world;
    root_ = stagedRoot;
    layers_.swap(stagedLayers);
    names_.swap(stagedNames);
    parents_.swap(stagedParents);
    if (!present(rig)) {
        std::string ignored;
        (void)destroy(&ignored);
        setError(error, "Cannot present initial puppet pose");
        return false;
    }
    if (error) error->clear();
    return true;
}

bool ParametricSpriteAssembly2D::present(
    const ay2d::ParametricSpriteRig2D& rig, std::uint32_t sortBase) {
    auto* avatar = root();
    if (!avatar || sortBase > 0x00FFFF00u ||
        rig.layerCount() != layers_.size()) return false;
    std::vector<ay2d::PuppetLayerPose2D> poses;
    if (!rig.evaluate(poses) || poses.size() != layers_.size()) return false;
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        auto* entity = layer(i);
        const auto& pose = poses[i];
        const auto parentIndex = rig.layerParentIndex(i);
        const auto* expectedParent = parentIndex < 0 ? avatar
            : layer(static_cast<std::size_t>(parentIndex));
        if (!entity || !expectedParent ||
            rig.layer(i).mesh ||
            rig.layer(i).name != names_[i] || parentIndex != parents_[i] ||
            entity->owner() != avatar || entity->spatialParent() != expectedParent ||
            !entity->getComponent<Transform>() ||
            !entity->getComponent<SpriteComponent>() ||
            !std::isfinite(pose.x) || !std::isfinite(pose.y) ||
            !std::isfinite(pose.rotation) || !std::isfinite(pose.scaleX) ||
            !std::isfinite(pose.scaleY) || !std::isfinite(pose.opacity) ||
            pose.scaleX <= 0 || pose.scaleY <= 0 ||
            pose.opacity < 0 || pose.opacity > 1 || pose.variantIndex < -1 ||
            (pose.variantIndex >= 0 && static_cast<std::size_t>(pose.variantIndex)
                >= rig.layer(i).variants.size())) return false;
    }
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        auto* entity = layer(i);
        if (!presentParametricSpriteLayer2D(rig, i, poses[i],
                *entity->getComponent<Transform>(),
                *entity->getComponent<SpriteComponent>(), sortBase))
            return false;
    }
    return true;
}

bool ParametricSpriteAssembly2D::destroy(std::string* error) {
    if (root_.isValid() && world_ && world_->isInitialized()) {
        if (auto* avatar = root()) {
            try { world_->destroyEntity(avatar); }
            catch (const std::exception& ex) {
                setError(error, ex.what());
                return false;
            }
        }
    }
    world_ = nullptr;
    root_ = {};
    layers_.clear();
    names_.clear();
    parents_.clear();
    if (error) error->clear();
    return true;
}

} // namespace ayt::entity
