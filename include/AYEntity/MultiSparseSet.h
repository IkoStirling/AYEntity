#pragma once

#include <AYEntity/IEntity.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace ayt::entity {

// One query membership per Entity, with any number of components behind it.
// Component ownership remains with Entity; this index never deletes pointers.
template<typename T>
class MultiSparseSet final : public IComponentStorage {
public:
    void* get(uint32_t) override { return nullptr; } // No implicit first instance.

    bool has(uint32_t entityId) const override {
        return _byEntity.contains(entityId);
    }

    void add(uint32_t entityId, void* component) override {
        auto [it, inserted] = _byEntity.try_emplace(entityId);
        if (inserted) _entityIds.push_back(entityId);
        it->second.push_back(static_cast<T*>(component));
    }

    void remove(uint32_t entityId) override {
        if (_byEntity.erase(entityId) == 0) return;
        _entityIds.erase(std::remove(_entityIds.begin(), _entityIds.end(), entityId),
                         _entityIds.end());
    }

    bool removeInstance(uint32_t entityId, void* component) override {
        auto it = _byEntity.find(entityId);
        if (it == _byEntity.end()) return false;
        auto& values = it->second;
        const auto found = std::find(values.begin(), values.end(), component);
        if (found == values.end()) return false;
        values.erase(found);
        if (values.empty()) remove(entityId);
        return true;
    }

    size_t size() const override { return _entityIds.size(); }
    void clear() override { _byEntity.clear(); _entityIds.clear(); }
    const std::vector<uint32_t>& getEntityIds() const override { return _entityIds; }

    void forEach(std::function<void(uint32_t, void*)> callback) override {
        for (uint32_t entityId : _entityIds) {
            for (T* component : _byEntity.at(entityId)) callback(entityId, component);
        }
    }

private:
    std::unordered_map<uint32_t, std::vector<T*>> _byEntity;
    std::vector<uint32_t> _entityIds;
};

} // namespace ayt::entity
