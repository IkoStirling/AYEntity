#pragma once
// AYSparseSet.h - Sparse Set implementation

#include <AYCore.h>
#include <IAYEntity.h>
#include <vector>
#include <algorithm>
#include <functional>

namespace ayt::entity
{

// =============================================================================
// SparseSet - High performance component storage
// =============================================================================
template<typename T>
class SparseSet : public IComponentStorage {
public:
    static constexpr uint32_t INVALID_INDEX = UINT32_MAX;

    SparseSet() = default;
    ~SparseSet() override = default;

    void* get(uint32_t entityId) override {
        if (entityId >= _sparse.size()) return nullptr;
        uint32_t index = _sparse[entityId];
        return (index != INVALID_INDEX) ? static_cast<void*>(_dense[index]) : nullptr;
    }

    bool has(uint32_t entityId) const override {
        if (entityId >= _sparse.size()) return false;
        return _sparse[entityId] != INVALID_INDEX;
    }

    void add(uint32_t entityId, void* component) override {
        if (entityId >= _sparse.size()) {
            _sparse.resize(entityId + 1, INVALID_INDEX);
        }

        if (_sparse[entityId] != INVALID_INDEX) return;

        _sparse[entityId] = static_cast<uint32_t>(_dense.size());
        _inverse.push_back(entityId);
        _dense.push_back(static_cast<T*>(component));
    }

    void remove(uint32_t entityId) override {
        if (entityId >= _sparse.size()) return;
        uint32_t index = _sparse[entityId];
        if (index == INVALID_INDEX) return;

        uint32_t lastEntityId = _inverse.back();
        _dense[index] = _dense.back();
        _sparse[lastEntityId] = index;
        _inverse[index] = lastEntityId;

        _dense.pop_back();
        _inverse.pop_back();
        _sparse[entityId] = INVALID_INDEX;
    }

    size_t size() const override {
        return _dense.size();
    }

    void clear() override {
        _dense.clear();
        _inverse.clear();
        std::fill(_sparse.begin(), _sparse.end(), INVALID_INDEX);
    }

    void forEach(std::function<void(uint32_t entityId, void* component)> callback) override {
        for (size_t i = 0; i < _dense.size(); ++i) {
            callback(_inverse[i], _dense[i]);
        }
    }

    T* getComponent(uint32_t entityId) {
        return static_cast<T*>(get(entityId));
    }

    const std::vector<uint32_t>& getEntityIds() const { return _inverse; }
    const std::vector<T*>& getDense() const { return _dense; }

private:
    std::vector<T*> _dense;
    std::vector<uint32_t> _sparse;
    std::vector<uint32_t> _inverse;
};

// =============================================================================
// SparseSetFactory
// =============================================================================
class SparseSetFactory {
public:
    template<typename T>
    static std::unique_ptr<SparseSet<T>> create() {
        return std::make_unique<SparseSet<T>>();
    }
};

} // namespace ayt::entity