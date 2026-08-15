# AYEntity Design

> **变更记录（2026-07）**：引擎集成、`bootstrapModule`、`SparseSet` 指针语义 — 见 [§15](#15-引擎集成与模块引导2026-07)。  
> **变更记录（2026-07-09）**：Simulation / Presentation 分轨（`SystemLane`）— 见 [§14](#14-simulation-vs-presentation-systemlane)；总览见 [`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../ENGINE-DETERMINISM-ARCHITECTURE.md)。

## 1. 概述

AYEntity 是 AY Engine 的**实体组件系统（Entity-Component-System, ECS）**，负责：
- 游戏对象（Entity）的创建、销毁、管理
- 组件（Component）的数据存储与查询
- 系统（System）的自动发现与调度
- 与其他模块（网络、脚本、渲染）的集成

### 1.1 设计目标

- **Hybrid 架构**：Entity 是对象，组件是数据，System 是逻辑
- **数据/行为分离**：数据组件纯结构体，行为组件实现接口
- **高性能查询**：Sparse Set 存储，支持 O(1) 组件访问
- **自动系统调度**：通过宏自动注册，无需手动添加
- **可扩展**：支持脚本组件、网络复制组件等扩展
- **与引擎集成**：作为子系统集成到 AYGameLoop

### 1.2 在引擎中的位置

```
┌─────────────────────────────────────────────────────────────────┐
│                      Engine Modules                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    AYGameLoop                             │   │
│  │              (主循环，驱动系统更新)                        │   │
│  └────────────────────────┬────────────────────────────────┘   │
│                           │                                      │
│                           ▼                                      │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    AYEntity                               │   │
│  │               (实体组件系统中枢)                           │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐               │   │
│  │  │  World   │  │  Entity  │  │  System   │               │   │
│  │  │ (管理器)  │  │ (对象)   │  │ (系统)   │               │   │
│  │  └──────────┘  └──────────┘  └──────────┘               │   │
│  └────────────────────────┬────────────────────────────────┘   │
│                           │                                      │
│       ┌───────────────────┼───────────────────┐                 │
│       │                   │                   │                 │
│       ▼                   ▼                   ▼                 │
│  ┌───────────┐      ┌───────────┐      ┌───────────┐        │
│  │  Render   │      │  Physics  │      │  Script   │        │
│  │  System   │      │  System   │      │  System   │        │
│  └───────────┘      └───────────┘      └───────────┘        │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 1.3 与其他模块的关系

| 模块 | 关系 | 集成方式 |
|------|------|----------|
| **AYGameLoop** | 驱动方 | EntitySubSystem 在 GameLoop 中 update |
| **AYScript** | 使用方 | ScriptComponent 桥接脚本到 Entity |
| **AYNetwork** | 使用方 | 通过 IReplicable 接口复制组件状态 |
| **AYResource** | 使用方 | 实体持有关卡/资产引用 |
| **AYRenderer** | 使用方 | `RenderSystem` 查询 Transform + MeshComponent，提交 `RenderScene` |
| **AYPhysics** | 使用方 | 查询 Transform + RigidBody 组件物理模拟（Jolt 路径，见 AYPhysics §8.3） |
| **Determinism** | 架构约束 | `SystemLane` 分轨；DET-04 落地 `SimTransformComponent` — 见 [§14](#14-simulation-vs-presentation-systemlane) |

---

## 2. 核心概念

### 2.1 Entity（实体）

```
Entity = ID + Name + 组件容器
```

- **ID**: 唯一标识符，用于快速索引
- **Name**: 名称（可选，用于调试/查找）
- **Components**: 附加的组件集合

### 2.2 Component（组件）

两种类型的组件：

| 类型 | 说明 | 示例 |
|------|------|------|
| **Data Component** | 纯数据，无逻辑 | `Transform`, `Health`, `Mesh` |
| **Behavior Component** | 有逻辑，实现 `IComponent` 接口 | `HealthComponent`, `AIController` |

### 2.3 System（系统）

```
System = 查询条件 + 更新逻辑
```

- 系统声明自己需要的组件类型
- 系统在每一帧被调用，处理匹配的实体

### 2.4 World（世界）

```
World = Entity 容器 + Component 存储 + System 管理
```

- 管理所有活跃实体
- 提供查询接口
- 调度系统更新

---

## 3. 核心接口

### 3.1 IComponent - 组件基类（行为组件用）

```cpp
class IComponent {
public:
    virtual ~IComponent() = default;
    
    // 组件名称
    virtual const char* getName() const = 0;
    
    // 生命周期
    virtual void onAttach(Entity* entity) {}
    virtual void onDetach() {}
    virtual void onUpdate(float dt) {}
    virtual void onStart() {}
};
```

### 3.2 Entity - 实体对象

```cpp
class Entity {
public:
    // 创建/销毁（工厂方法）
    static Entity* create();
    static void destroy(Entity* e);
    
    // 基础属性
    uint32_t getId() const { return _id; }
    const char* getName() const { return _name.c_str(); }
    void setName(const char* name) { _name = name; }
    
    // 组件操作
    template<typename T, typename... Args>
    T* addComponent(Args&&... args);
    
    template<typename T>
    T* getComponent();
    
    template<typename T>
    bool hasComponent() const;
    
    template<typename T>
    void removeComponent();
    
    // 字符串接口（用于脚本/编辑器）
    IComponent* addComponentByName(const char* typeName);
    IComponent* getComponentByName(const char* typeName);
    bool hasComponentByName(const char* typeName) const;
    void removeComponentByName(const char* typeName);
    
    // 查询
    std::vector<IComponent*> getComponents() const;
    bool isValid() const { return _id != INVALID_ID; }
    
    // 世界引用
    World* getWorld() const { return _world; }

private:
    uint32_t _id = INVALID_ID;
    std::string _name;
    World* _world = nullptr;
};
```

### 3.3 World - 世界管理器

```cpp
class World {
public:
    static World& instance();
    
    // ===== 生命周期 =====
    bool initialize();
    void shutdown();
    void update(float dt);
    
    // ===== 实体操作 =====
    Entity* createEntity();
    void destroyEntity(Entity* e);
    Entity* findEntity(const char* name) const;
    Entity* findEntity(uint32_t id) const;
    
    // 获取所有实体（用于调试/编辑器）
    std::vector<Entity*> getAllEntities() const;
    
    // ===== 查询 =====
    // 编译时模板查询
    template<typename... T>
    auto query();
    
    // 运行时字符串查询（用于编辑器/工具）
    std::vector<Entity*> queryByNames(const std::vector<const char*>& componentNames);
    
    // ===== 系统调度 =====
    template<typename T>
    void registerSystem(int32_t priority = 0);
    
    // ===== 组件注册（内部使用）=====
    template<typename T>
    static void registerComponentType(const char* name);
    
private:
    std::vector<EntityHandle> _entityPool;
    std::vector<std::unique_ptr<ISystem>> _systems;
    
    // 组件存储
    std::unordered_map<size_t, std::unique_ptr<IComponentStorage>> _componentStorages;
};
```

### 3.4 ISystem - 系统接口

```cpp
class ISystem {
public:
    virtual ~ISystem() = default;
    
    virtual const char* getName() const = 0;
    virtual void onUpdate(float dt) = 0;
    virtual void onStart() {}
    
    // 优先级（决定更新顺序）
    int32_t getPriority() const { return _priority; }
    
protected:
    int32_t _priority = 0;
};
```

### 3.5 IComponentStorage - 组件存储接口

```cpp
class IComponentStorage {
public:
    virtual ~IComponentStorage() = default;
    
    virtual void* get(uint32_t entityId) = 0;
    virtual bool has(uint32_t entityId) const = 0;
    virtual void add(uint32_t entityId, void* component) = 0;
    virtual void remove(uint32_t entityId) = 0;
    virtual size_t size() const = 0;
    virtual void clear() = 0;
    
    // 遍历（用于系统更新）
    virtual void forEach(std::function<void(uint32_t entityId, void* component)> callback) = 0;
};
```

---

## 4. Sparse Set 实现

### 4.1 核心数据结构（2026-07：指针存储）

组件在 `Entity::addComponent` 中 **heap 分配**，`SparseSet` 存储 **`T*` 非拥有指针**，
与 `Entity::_components` 指向**同一实例**。`getComponent<T>()` 与 `addComponent` 返回值一致。

```cpp
template<typename T>
class SparseSet : public IComponentStorage {
private:
    std::vector<T*> _dense;                   // 组件指针（非拥有）
    std::vector<uint32_t> _sparse;            // Entity ID → Dense Index
    std::vector<uint32_t> _inverse;           // Dense Index → Entity ID
    
public:
    static constexpr uint32_t INVALID_INDEX = UINT32_MAX;
    
    void* get(uint32_t entityId) override {
        if (entityId >= _sparse.size()) return nullptr;
        uint32_t index = _sparse[entityId];
        return (index != INVALID_INDEX) ? static_cast<void*>(_dense[index]) : nullptr;
    }
    
    void add(uint32_t entityId, void* component) override {
        if (entityId >= _sparse.size()) {
            _sparse.resize(entityId + 1, INVALID_INDEX);
        }
        if (_sparse[entityId] != INVALID_INDEX) return;
        
        _sparse[entityId] = static_cast<uint32_t>(_dense.size());
        _inverse.push_back(entityId);
        _dense.push_back(static_cast<T*>(component));  // 存指针，不拷贝
    }
    
    void remove(uint32_t entityId) override {
        // swap-remove；不 delete（Entity 拥有生命周期）
        // ...
    }
};
```

> **历史问题（已修复）**：早期按值 `_dense.push_back(std::move(*ptr))` 导致
> `addComponent` 返回的指针与 `getComponent` 不一致；Demo 中修改 `meshPath` 后
> `RenderSystem` 查询到空路径。2026-07 改为指针语义。

### 4.2 访问模式

```
Entity ID:     0    1    2    3    4    5
Sparse:      [ 0 ] [ 1 ] [ - ] [ 2 ] [ - ] [ 3 ]
              │     │          │          │
              ▼     ▼          ▼          ▼
Dense:      [Comp0][Comp1][Comp3][Comp5]  (实际数据紧凑存储)
Inverse:    [ 0 ] [ 1 ] [ 3 ] [ 5 ]     (稠密索引 → Entity ID)

访问 Entity 2 的组件：
1. 检查 Sparse[2] = -1 (INVALID_INDEX)
2. 无组件

访问 Entity 3 的组件：
1. Sparse[3] = 2
2. Dense[2] = Comp3
```

### 4.3 批量遍历

```cpp
// 系统更新时高效遍历所有组件
void MovementSystem::onUpdate(float dt) {
    auto& storage = World::instance().getStorage<Transform>();
    
    storage.forEach([dt](uint32_t entityId, void* comp) {
        auto* transform = static_cast<Transform*>(comp);
        transform->position += transform->velocity * dt;
    });
}
```

---

## 5. 查询系统

### 5.1 编译时模板查询（C++23 fold expression）

```cpp
template<typename... Components>
class Query {
public:
    class Iterator {
    public:
        Iterator(uint32_t id, World* w) : _id(id), _world(w) {}

        bool operator!=(const Iterator& other) const {
            return _id != other._id;
        }

        Entity* operator*() const {
            return _world->findEntity(_id);
        }

        Iterator& operator++() {
            do {
                _id++;
            } while (_id < MAX_ENTITIES && !hasAllComponents());
            return *this;
        }

    private:
        bool hasAllComponents() const {
            auto* e = _world->findEntity(_id);
            return e && (e->hasComponent<Components>() && ...);
        }

        uint32_t _id;
        World* _world;
    };

    Query(World* w) : _world(w), _first(findFirst()) {}

    Iterator begin() { return Iterator(_first, _world); }
    Iterator end() { return Iterator(MAX_ENTITIES, _world); }

private:
    uint32_t findFirst() const {
        for (uint32_t id = 1; id < MAX_ENTITIES; id++) {
            auto* e = _world->findEntity(id);
            if (e && (e->hasComponent<Components>() && ...)) {
                return id;
            }
        }
        return MAX_ENTITIES;
    }

    World* _world;
    uint32_t _first;
};
```

**特点**：
- 使用 C++23 fold expression `(e->hasComponent<Components>() && ...)` 消除模板递归歧义
- 无需 `matchImpl` 等递归辅助函数
- 简洁高效

### 5.2 使用示例

```cpp
// 注册系统时声明需要的组件
class MovementSystem : public ISystem {
public:
    const char* getName() const override { return "Movement"; }
    void onUpdate(float dt) override;
    
private:
    Query<Transform, Velocity> _query;  // 匹配同时有 Transform 和 Velocity 的实体
};

void MovementSystem::onUpdate(float dt) {
    for (auto* entity : _query) {
        auto* transform = entity->getComponent<Transform>();
        auto* velocity = entity->getComponent<Velocity>();
        transform->position += velocity->value * dt;
    }
}
```

### 5.3 运行时字符串查询（编辑器用）

```cpp
// 编辑器属性面板查询
std::vector<Entity*> World::queryByNames(const std::vector<const char*>& componentNames) {
    std::vector<Entity*> result;
    
    for (auto* entity : getAllEntities()) {
        bool match = true;
        for (auto* name : componentNames) {
            if (!entity->hasComponentByName(name)) {
                match = false;
                break;
            }
        }
        if (match) {
            result.push_back(entity);
        }
    }
    
    return result;
}

// 使用
auto entities = world->queryByNames({"Transform", "Health"});
```

---

## 6. 宏与自动注册

### 6.1 组件声明宏

```cpp
// 数据组件（纯结构体）
struct Transform {
    Vector3 position{0, 0, 0};
    Quaternion rotation{1, 0, 0, 0};
    Vector3 scale{1, 1, 1};
};

// 行为组件声明（在命名空间内使用）
#define AY_COMPONENT(T) \
    static_assert(std::is_base_of_v<ayt::entity::IComponent, T>, #T " must inherit IComponent"); \
    namespace { \
        struct T##_Registrar { \
            T##_Registrar() { \
                ayt::entity::World::registerComponentType<T>(#T); \
            } \
        }; \
        static T##_Registrar g_registrar; \
    }

// 使用（在 ayt::entity 命名空间内）
class HealthComponent : public IComponent {
public:
    const char* getName() const override { return "Health"; }
    int hp = 100;
    int maxHp = 100;
};
AY_COMPONENT(HealthComponent);
```

### 6.2 系统声明宏

```cpp
#define AY_SYSTEM(T, priority) \
    static_assert(std::is_base_of_v<ayt::entity::ISystem, T>, #T " must inherit ISystem"); \
    namespace { \
        struct T##_Registrar { \
            T##_Registrar() { \
                ayt::entity::World::registerSystem<T>(priority); \
            } \
        }; \
        static T##_Registrar g_registrar; \
    }
```

### 6.3 使用示例

```cpp
// ===== 组件 =====

struct Transform {
    Vector3 position{0, 0, 0};
    Quaternion rotation{1, 0, 0, 0};
    Vector3 scale{1, 1, 1};
};

struct Velocity {
    Vector3 value{0, 0, 0};
};

class HealthComponent : public IComponent {
public:
    const char* getName() const override { return "Health"; }
    int hp = 100;
    int maxHp = 100;
};
AY_COMPONENT(HealthComponent);

// ===== 系统 =====

class MovementSystem : public ISystem {
public:
    const char* getName() const override { return "Movement"; }
    void onUpdate(float dt) override {
        for (auto* entity : _query) {
            auto* transform = entity->getComponent<Transform>();
            auto* velocity = entity->getComponent<Velocity>();
            transform->position += velocity->value * dt;
        }
    }
    
private:
    Query<Transform, Velocity> _query;
};
AY_SYSTEM(MovementSystem, 100);

class HealthSystem : public ISystem {
public:
    const char* getName() const override { return "Health"; }
    void onUpdate(float dt) override {
        for (auto* entity : _query) {
            auto* health = entity->getComponent<HealthComponent>();
            if (health->hp <= 0) {
                entity->destroy();
            }
        }
    }
    
private:
    Query<HealthComponent> _query;
};
AY_SYSTEM(HealthSystem, 200);
```

---

## 7. 与 AYGameLoop 集成

### 7.1 EntitySubSystem

```cpp
class EntitySubSystem : public ISubSystem {
public:
    const char* getName() const override { return "Entity"; }
    
    bool initialize() override {
        World::instance().initialize();
        return true;
    }
    
    void shutdown() override {
        World::instance().shutdown();
    }
    
    void update(float dt) override {
        World::instance().update(dt);
    }
    
private:
    // World 已经在 EntitySubSystem 构造时创建
};
```

### 7.2 自动注册

```cpp
// AYGameLoop 初始化时自动注册
REGISTER_SUBSYSTEM(EntitySubSystem, {}, 0);
```

---

## 8. 脚本组件集成

> **⚠️ 预占位说明**：ScriptComponent 当前仅为接口定义，`IScriptBridge` 为空接口。
> 后续需根据 AYScript 模块的实际实现进行重构，补充真实的脚本桥接逻辑。

### 8.1 ScriptComponent

```cpp
class ScriptComponent : public IComponent {
public:
    const char* getName() const override { return _scriptName.c_str(); }
    
    void onAttach(Entity* entity) override {
        _entity = entity;
        // 调用脚本的 onStart
        if (_bridge) {
            _bridge->call(_scriptName + ".onStart", entity);
        }
    }
    
    void onUpdate(float dt) override {
        if (_bridge) {
            _bridge->call(_scriptName + ".onUpdate", dt);
        }
    }
    
    void onDetach() override {
        if (_bridge) {
            _bridge->call(_scriptName + ".onDestroy");
        }
        _entity = nullptr;
    }
    
    void setScript(const char* name) { _scriptName = name; }
    
private:
    std::string _scriptName;
    Entity* _entity = nullptr;
    IAYScriptBridge* _bridge = nullptr;  // 从 ScriptSubSystem 获取
};
```

### 8.2 脚本中定义组件

```lua
-- PlayerAI.lua
PlayerAI = {
    target = nil,
    speed = 5.0,
    
    onStart = function(self, entity)
        self.entity = entity
        self.transform = entity:getComponent("Transform")
    end,
    
    onUpdate = function(self, dt)
        if self.target then
            local pos = self.transform.position
            pos.x = pos.x + self.target.x * self.speed * dt
            self.transform.position = pos
        end
    end,
    
    onDestroy = function(self)
        print("PlayerAI destroyed")
    end
}
```

### 8.3 使用

```cpp
// 游戏代码
Entity* player = Entity::create();
player->setName("Player");

// 添加脚本组件
auto* script = player->addComponent<ScriptComponent>();
script->setScript("PlayerAI");

// 或通过字符串
player->addComponentByName("PlayerAI");
```

---

## 9. 网络复制集成

> **⚠️ 预占位说明**：NetworkComponent 当前仅为接口定义，未真正集成 `IReplicable`。
> 后续需根据 AYNetwork 模块的 ReplicationManager 实现进行重构，补充真实的网络复制逻辑。

### 9.1 可网络复制组件

```cpp
// 组件实现 IReplicable 接口
class NetworkedComponent : public IComponent, public IReplicable {
public:
    uint32_t getNetId() const override { return _netId; }
    void setNetId(uint32_t id) override { _netId = id; }
    
    void replicate(BitStream& stream) override {
        // 序列化
    }
    
    void onReplicate(const BitStream& stream) override {
        // 反序列化
    }
    
private:
    uint32_t _netId = INVALID_NET_ID;
};

// 或通过宏自动实现
AY_COMPONENT_WITH_REPLICATION(Transform)
```

### 9.2 ReplicationManager 集成

```cpp
// 在 NetworkSubSystem 中
class NetworkSubSystem : public ISubSystem {
    void registerNetworkedComponent(const char* componentName) {
        // 注册到 ReplicationManager
        _replicationManager->registerType(componentName);
    }
};
```

---

## 10. 目录结构

```
AYEntity/
├── design.md
├── CMakeLists.txt
│
├── interface/
│   ├── AYEntity/IEntity.h              # 主接口
│   ├── IComponent.h             # 组件接口
│   ├── AYEntity/IEntity.h                # 实体接口
│   ├── AYEntity/World.h                 # 世界管理器接口
│   ├── ISystem.h                # 系统接口
│   └── IComponentStorage.h      # 组件存储接口
│
├── include/
│   ├── AYEntity.h               # 主入口
│   ├── ComponentStorage.h       # SparseSet 实现
│   ├── Entity.h                 # 实体实现
│   ├── AYEntity/World.h                  # 世界管理器实现
│   ├── AYEntity/EntityHandle.h           # 实体句柄
│   │
│   └── components/               # 常用组件
│       ├── Transform.h
│       ├── Health.h
│       ├── AYResource/assetsImpl/Mesh.h
│       ├── RigidBody.h
│       └── ...
│
└── src/
    ├── AYEntitySubSystem.cpp     # 子系统实现
    ├── World.cpp
    ├── Entity.cpp
    └── ComponentStorage.cpp
```

---

## 11. 构建系统集成

### 11.1 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)

project(AYEntity)

add_library(${PROJECT_NAME} SUBSYSTEM)

target_sources(${PROJECT_NAME} PRIVATE
    src/AYEntitySubSystem.cpp
    src/World.cpp
    src/Entity.cpp
    src/ComponentStorage.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    AYCore
    AYGameLoop
)

# 组件预编译选项
option(AY_ENTITY_PRECOMPILE_COMPONENTS "Precompile common components" ON)

if(AY_ENTITY_PRECOMPILE_COMPONENTS)
    target_sources(${PROJECT_NAME} PRIVATE
        include/components/Transform.h
        include/components/Health.h
        include/components/AYResource/assetsImpl/Mesh.h
    )
endif()
```

---

## 12. 实现优先级

### Phase 1: 核心框架
- [x] IComponent / IEntity / IWorld / ISystem 接口
- [x] SparseSet 组件存储
- [x] Entity 创建/销毁/查询
- [x] World 管理器
- [x] 自动注册宏

### Phase 2: 常用组件
- [x] Transform 组件
- [x] Health 组件（行为组件）
- [x] Mesh 组件
- [x] RigidBody 组件

### Phase 3: 系统调度
- [x] Query 查询系统
- [x] 编译时模板查询
- [x] 系统自动注册（`AY_SYSTEM` 宏；静态库场景见 §15 显式 bootstrap）
- [x] 优先级调度
- [x] `RenderSystem` + AYRenderer 集成（2026-07）

### Phase 3.5: 渲染集成（2026-07）
- [x] `RenderSystem`：`Transform` + `MeshComponent` → `RenderScene`
- [x] `EntitySubSystem` 注册到 GameLoop
- [x] `bootstrapModule()` 显式注册（MSVC 静态库）
- [x] 组件反射：`AY_PROPERTY` + `AY_FINALIZE_REGISTRATION_METADATA`

### Phase 4: 脚本集成
- [x] ScriptComponent（**预占位** - 当前接口定义，待 AYScript 实现后重构）
- [ ] 与 AYScript 桥接

### Phase 5: 网络集成
- [x] NetworkComponent（**预占位** - 当前接口定义，待 AYNetwork 实现后重构）
- [ ] 与 AYNetwork ReplicationManager 配合

### Phase 6: 确定性 Sim 轨（按需，DET-04）
- [ ] `SystemLane` 元数据（Present / Sim / Bridge）
- [ ] `SimTransformComponent` + `SimToPresentBridge`
- [ ] Sim 系统稳定遍历顺序文档化

> 不阻塞 Phase 0–2。触发条件与 DET 工作包见 [`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../ENGINE-DETERMINISM-ARCHITECTURE.md) §7–§9。

---

## 13. 与工业级引擎对比

| 功能 | AYEntity | Unity | Unreal | O3DE |
|------|----------|-------|--------|------|
| ECS 架构 | ✅ Hybrid | ✅ Hybrid | ✅ Hybrid | ✅ Hybrid |
| Sparse Set | ✅ 已实现 | ✅ | ❌ | ❌ |
| 自动系统注册 | ✅ 已实现 | ❌ | ❌ | ❌ |
| 脚本组件 | 规划 | ✅ | ❌ | ✅ (Lua) |
| 网络复制 | 规划 | ✅ | ✅ | ✅ |
| 查询方式 | ✅ 模板+字符串 | 模板 | 模板 | 模板 |
| Fold Expression | ✅ C++23 | ❌ | ❌ | ❌ |

---

## 14. Simulation vs Presentation (`SystemLane`)

> **权威文档**：[`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../ENGINE-DETERMINISM-ARCHITECTURE.md)  
> 本节定义 ECS 侧的**分轨契约**；不要求当前代码立即实现 `SystemLane` 枚举。

### 14.1 动机

引擎 presentation 层（渲染、蒙皮动画、Editor）继续使用 native `Float32`。
可选的**确定性仿真**（帧同步、输入回放、rollback）在独立的 **Sim 轨**运行，通过 Bridge 向 presentation 提供插值后的 `TransformComponent`。

两套世界共用 `World` 与实体 ID，但 **System 所属轨道**与**可写组件**必须分离，避免日后把 Jolt / GPU / 无序遍历引入 lockstep 路径。

### 14.2 轨道定义

```cpp
enum class SystemLane : uint8_t {
    Present,  // 表现：float，可变 dt，允许 Job 并行
    Sim,      // 仿真：未来 Fixed；fixedUpdate / simFrame；单线程或确定性有序阶段
    Bridge,   // Sim → Present：读 Sim 状态 + alpha，写 float Transform
};
```

| 轨道 | 现有 System 示例 | 时间源 | 可写组件（当前 / 未来） |
|------|------------------|--------|-------------------------|
| **Present** | `AnimationSystem`, `RenderSystem`, `SkinnedMeshRenderSystem` | `update(dt)` | `TransformComponent` (float), `MeshComponent`, … |
| **Sim** | （未来）movement、det 碰撞、Gameplay `System` host | `fixedUpdate(fixedDt)` + `simFrame` | `SimTransformComponent` (DET-04), 玩法状态 |
| **Bridge** | （未来）`SimToPresentBridge` | 每 presentation 帧 | 只写 float `TransformComponent` |

**调度**：Sim 轨由 `AYGameLoop::fixedUpdate` 驱动（见 [`AYGameLoop/design.md`](../AYGameLoop/design.md)）；Present 轨由 `update` + `FrameInterpolator` 驱动。

### 14.3 新增 System 时的审查清单

在 code review / design 中声明 `SystemLane`（可先写在 `design.md` 或 PR 描述，代码元数据 DET-04 再补）：

1. 该逻辑是否参与**跨端一致的仿真**？→ **Sim**
2. 是否只影响画面 / 编辑器 / 音频？→ **Present**
3. 是否把 Sim 状态映射到渲染？→ **Bridge**（且仅此一类应写 float `Transform`）

### 14.4 Sim 轨禁止项（DET-01 之前即生效）

即使尚未引入 `Fixed32`，Sim 轨代码也不得：

- 用 `std::unordered_map` / `unordered_set` 的迭代顺序驱动玩法分支
- 使用未 seed 的 `rand()`、`random_device`、墙钟 `time()` 影响结果
- 从 `AYTask` 并行写同一实体的 Sim 组件
- 调用 **Jolt**（`AYPhysics`）或 GPU readback 作为玩法依据

### 14.5 组件命名约定（未来）

| 组件 | 数值空间 | 消费者 |
|------|----------|--------|
| `TransformComponent` | `Float32` | Render、Editor、非 lockstep 网络复制 |
| `SimTransformComponent` | `FixedVec3`（DET-01） | Sim 系统、lockstep checksum |
| 玩法 hitbox / 受击判定 | Sim 代理体 | **不要**在 lockstep 中采样蒙皮后的骨骼矩阵 |

命中判定应使用 Sim 代理（胶囊 / AABB），由动画在 Present 轨驱动视觉，与 [`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../ENGINE-DETERMINISM-ARCHITECTURE.md) §5.3 一致。

### 14.6 与网络 / 回放的关系

| 网络模型 | ECS 用法 |
|----------|----------|
| **状态复制**（`AYNetwork` P0） | 可复制 float `TransformComponent`；服务器 Jolt 为权威 |
| **Lockstep / 输入回放** | 仅 Sim 组件参与 checksum；Present 轨本地插值 |

详见 [`AYNetwork/design.md`](../AYNetwork/design.md) 与 [`AYExtension/design.md`](../AYExtension/design.md) §8.1。

---

## 15. 引擎集成与模块引导（2026-07）

### 15.1 渲染链路

```
GameLoop::processVariableUpdates
  → EntitySubSystem::update → World::update → RenderSystem::onStart/onUpdate
  → (demo) onUpdate listener：创建实体、更新 Transform
GameLoop::submitRenderCommands
  → RendererSubSystem::renderFrame
  → RenderSystem::buildRenderScene → RenderScene
  → Renderer::render
```

`RenderSystem` 在 `onStart` 向 `RendererSubSystem` 注册 `SceneBuildCallback`；
每帧由 Renderer 子系统回调填充 `RenderScene`。

### 15.2 `bootstrapModule()`（静态库必需）

MSVC 静态库会丢弃未被引用的 `.obj`（`REGISTER_SUBSYSTEM` / `AY_SYSTEM` 静态初始化 TU 可能未链接）。
因此提供显式、幂等的模块引导 API（`include/AYEntity/EntityModule.h`）：

```cpp
void bootstrapModule();              // 入口：依次调用下列三者
void registerEntityComponents();     // Transform、HealthComponent 类型名
void registerRenderSystem();         // World::registerSystem<RenderSystem>
void registerEntitySubSystem();      // GameLoop::registerSubSystem<EntitySubSystem>
```

**调用时机**：`GameLoop::run()` 之前一次（见 `AYEngineIntegration_Demo`）。

> 不再使用 link-anchor（`extern const int`）方案：namespace 内 `const` 内部链接 + 符号解析 fragile。

### 15.3 组件序列化

| 组件 | 反射宏 | 注册位置 |
|------|--------|----------|
| `Transform` | `AY_PROPERTY` + `AY_FINALIZE_REGISTRATION_METADATA` | 组件头文件 |
| `HealthComponent` | 同上 | 组件头文件 |
| `MeshComponent` | 无序列化字段 | `AY_COMPONENT` 或 `addComponent` 时自动 storage |

`src/AYEntityReflection.cpp` 负责 `World::registerComponentType`（World 查询用），
**不**负责 `SerializerFor`（由 AYSerializer 默认偏特化处理）。

Win32：`Transform` 名称与 GDI 宏冲突时使用 `(Transform)` 括号形式。

### 15.4 源文件布局（当前）

```
AYEntity/
├── include/
│   ├── AYEntity/EntityModule.h
│   ├── AYEntity/RenderSystem.h
│   ├── AYEntity/World.h
│   └── components/
├── src/
│   ├── AYEntityModule.cpp
│   ├── AYEntitySubSystem.cpp
│   ├── AYEntityReflection.cpp
│   ├── AYRenderSystem.cpp
│   ├── AYWorld.cpp
│   └── AYEntity.cpp
└── design.md
```

### 15.5 变更摘要（2026-07）

| 项 | 说明 |
|---|---|
| SparseSet | 按值拷贝 → 指针存储；修复 RenderSystem 读不到 meshPath |
| 模块引导 | `bootstrapModule()` 替代 link-anchor |
| 序列化 | 组件仅用 `AY_FINALIZE_REGISTRATION_METADATA` |
| RenderSystem | 诊断日志（matched / skip / submitted / sceneItems） |
| **P2.1 (2026-07-27)** | **BlendSpace 1D/2D Blend Tree:** `BlendSpaceComponent` + `BlendSpaceSystem@430` + `SkeletonComponent::skinMatricesBlendSpace` + AnimationSystem memcpy pick-non-null；见 §15.7 |

### 15.6 GL-01 系统 tick 顺序契约

`World::registerSystem<T>(priority)` 按 priority 升序排序；`World::update(dt)` 依次
调 `onUpdate`。`bootstrapModule()` 必须在以下顺序内注册系统：

| 优先级 | 系统 | 责任 |
|--------|------|------|
| 0–399 | （未占用） | 留给调试 / 测试 CounterSystem 等 |
| 405 | `OrthoCameraUpdateSystem` (CM-3) | 把 OrthoCameraComponent 写到渲染侧相机源 |
| 430 | `BlendSpaceSystem` (P2.1) | 驱动 BlendSpace1D/2D,写 `SkeletonComponent::skinMatricesBlendSpace` |
| 450 | `AnimationSystem` | tick AnimationPlayer,刷新 `SkeletonComponent::skinMatrices`(BlendSpace 非空时 memcpy pick 非空) |
| 460 | `TilemapAnimationTickSystem` (CM-5) | 按 QPC 墙钟推进每 path 动画表,tick 幂等(同 path 多实体同帧只推进一次) |
| 500 | `SkinnedMeshRenderSystem` | 注册 scene-builder,把 skinned 实体写进 RenderScene |
| 500 | `RenderSystem` | 注册 scene-builder,把非 skinned 实体写进 RenderScene |
| 510 | `TilemapRenderSystem` / `SpriteRenderSystem` (CM-3) | 注册 scene-builder,把 2D 实体写进 RenderScene;tile 经动画 resolve 后取 UV |
| 600+ | （未占用） | 留给 Physics / Audio / Script / 工具系统 |

**契约**：`AnimationSystem` 必须早于所有 render 系统（priority 450 < 500），
否则渲染端会读到上一帧的 bone matrices,快方向切换时会出现 1 帧延迟。
同样地，`BlendSpaceSystem` 必须早于 `AnimationSystem`（priority 430 < 450）,
否则 AnimationSystem 的 memcpy pick 看不到本帧的 BlendSpace-base skin matrices,
回退到 AnimationComponent 的单 clip 路径,BlendSpace 的工作就丢了。
2D 车道同序：`OrthoCameraUpdateSystem` 405 < 510、`TilemapAnimationTickSystem` 460
< 510 —— tick 先于 render 消费,渲染侧读到的永远是本帧 resolved 的 tileId
（`TilemapAnimationRuntime::resolve` 的 `resolved[]` 在 tick 内同步刷新）；
否则会画出上一帧的帧（1 帧动画延迟）。

**验证**：`unittest/SkinnedAnimationTest.cpp::animation_system_priority_before_render_systems`
在每次构建时跑 `bootstrapModule()` → 枚举 `World::systemCount()` → 断言
三个 priority 值与上表一致。`unittest/AYTest_BlendSpaceSystem.cpp::blend_space_system_priority_before_animation_system`
额外断言 BlendSpaceSystem 430 < AnimationSystem 450。改 priority 是破坏性变更,必须同时更新本表 + 单测。

### 15.7 P2.1 — BlendSpace 1D / 2D Blend Tree（2026-07-27）

`BlendSpace1D` / `BlendSpace2D` 是 AYAnimation 提供的线性单纯形 BlendTree（UE BlendSpace / Unity AnimationBlendTree 1D/2D 等价物）。
本节只记 ECS 集成层；纯算法细节（2D 单纯形算法、tangent-space quaternion blend、bounding-rect heuristic）见
[AYAnimation/BlendSpace.h](../AYAnimation/include/AYAnimation/BlendSpace.h) 的文件头注释。

**组件**（`include/AYEntity/components/BlendSpaceComponent.h`）：

- `BlendSpaceEntry`：单个 sample point 的 spec — `samplePosition`（1D 取 x、2D 取 xy）、`clipPath`、`playRate`、`looping`、`blendSpaceIndex`。
- `BlendSpaceComponent`：`is2D` 切 1D/2D、`entries[]`（≥1 才 `isValid()`）、`sampleInput`、`playRate`、`looping`。

**系统**（`include/AYEntity/BlendSpaceSystem.h`，priority 430）：

- `onUpdate(dt)` 遍历 `World::query<SkeletonComponent, BlendSpaceComponent>()`；
  对每个有效实体：懒加载每个 entry 的 `clipPath`（`_clipCache` 路径缓存，N 个实体共享 clip 只 parse 一次）→
  调 `BlendSpace1D::setSkeleton / setParameter / tick / evaluate`（或 2D 版本）→
  把 per-bone parent-local TRS 提升为 world × inverseBind 矩阵 → memcpy 到
  `SkeletonComponent::skinMatricesBlendSpace`。
- 复用 `AssetBoneCache`（P1.7 引入）做 `(ISkeleton*, boneName) → boneIdx` 的跨 player 缓存。

**SkeletonComponent 扩展**：

- 新增 `Float4x4* skinMatricesBlendSpace = nullptr;` 字段（与既有 `skinMatrices` 平级，独立生命周期）。
- `BlendSpaceSystem` 在懒加载后第一次 tick 时分配；同一 entity 销毁时 dtor 释放。
- AnimationSystem 在 priority 450 memcpy pick：**`skinMatricesBlendSpace != nullptr` 时用它替换既有 `skinMatrices`**，
  作为渲染端读到的"权威 base pose"。这意味着同一 entity 可以同时挂 `BlendSpaceComponent`（base）+ `AnimationComponent.additiveLayers[]`（additive on top），
  AnimationSystem 的 Phase 1b additive 逻辑不变，自动在 BlendSpace base 上累加。

**正交-fields 模型（设计原则）**：

- BlendSpaceSystem 写 `skinMatricesBlendSpace`，AnimationSystem 写 `skinMatrices`，两者不互相覆盖。
- 加法层只走 AnimationSystem 的 Phase 1b，BlendSpaceSystem 不触碰。
- 同一 entity 移除 `BlendSpaceComponent` → `skinMatricesBlendSpace == nullptr` → AnimationSystem 自动回退
  AnimationComponent 单 clip 路径，无需切换 component。

**测试覆盖**：

- 单元：`AYRuntime/AYAnimation/unittest/AYTest_BlendSpace.cpp` — 12 个 case（1D boundary clamp / 2D heuristic / 编辑器 triangulation / nearest-vertex / library-mode / shared skeleton lifecycle）。
- ECS 集成：`AYRuntime/AYEntity/unittest/AYTest_BlendSpaceSystem.cpp` — 6 个 case（priority 契约 / empty-entries skip / skeleton-not-loaded defer / 1D 路径写入 skinMatrices / 2D 路径 / 无变更不重 bind）。
- AYAnimation_UnitTests 471/471 PASS（459 旧 + 12 新）；AYEntityTest_BlendSpaceSystem 57/57 PASS（用 `runSuite("BlendSpaceSystemTests")` 隔离 AYEntityTest 已存在的 ComponentTest::network_component AV flake）。

**Bootstrap 现状**：P2.1 阶段 `bootstrapModule()` **未自动注册** `BlendSpaceSystem` —— 引擎集成侧还在对齐 `registerBlendSpaceSystem()` 的入口时机。
单测里显式调 `registerBlendSpaceSystem()`，未来 §15.2 的 `bootstrapModule()` 改成"无条件注册 BlendSpaceSystem" 时直接补一行即可。

---

## 16. 参考

- [Engine determinism architecture](../../ENGINE-DETERMINISM-ARCHITECTURE.md) — dual-layer Sim/Present, DET-01..08
- [Flecs ECS](https://github.com/SanderMertens/flecs)
- [Entt ECS](https://github.com/skypjack/entt)
- [Unity Entity Component System](https://docs.unity3d.com/Packages/com.unity.entities@latest/)
- [Unreal Gameplay Architecture](https://docs.unrealengine.com/en-US/ProgrammingAndScripting/GameplaySystems/networking/)
- [O3DE Game Entity](https://o3de.org/docs/user-guide/components/)