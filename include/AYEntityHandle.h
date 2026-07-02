#pragma once
// AYEntityHandle.h - Entity handle

#include <AYCore.h>
#include <cstdint>

namespace ayt::entity
{

// =============================================================================
// EntityHandle - Entity safe handle
// =============================================================================
struct EntityHandle {
    uint32_t id = 0;
    uint32_t version = 0;

    bool isValid() const {
        return id != 0;
    }

    bool operator==(const EntityHandle& other) const {
        return id == other.id && version == other.version;
    }

    bool operator!=(const EntityHandle& other) const {
        return !(*this == other);
    }
};

// =============================================================================
// EntityHandlePool - Manages entity versions
// =============================================================================
class EntityHandlePool {
public:
    static EntityHandlePool& instance();

    EntityHandle allocate(uint32_t id);
    void release(const EntityHandle& handle);
    bool isValid(const EntityHandle& handle) const;
    void reset();

private:
    EntityHandlePool() = default;

    std::vector<uint32_t> _versions;
};

// =============================================================================
// Implementation
// =============================================================================
inline EntityHandlePool& EntityHandlePool::instance() {
    static EntityHandlePool pool;
    return pool;
}

inline EntityHandle EntityHandlePool::allocate(uint32_t id) {
    if (id >= _versions.size()) {
        _versions.resize(id + 1, 0);
    }
    EntityHandle h;
    h.id = id;
    h.version = _versions[id];
    return h;
}

inline void EntityHandlePool::release(const EntityHandle& handle) {
    if (handle.id < _versions.size()) {
        _versions[handle.id]++;
    }
}

inline bool EntityHandlePool::isValid(const EntityHandle& handle) const {
    if (handle.id >= _versions.size()) {
        return false;
    }
    return _versions[handle.id] == handle.version;
}

inline void EntityHandlePool::reset() {
    _versions.clear();
}

} // namespace ayt::entity