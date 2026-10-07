# AYEntity Design

## 2026-10-05 — 生产 shader 清单闭包

SkinnedMeshRenderSystem 的三段 rigid fallback 源仅机械迁移到 Renderer 的共享源码头，
运行时与离线工具均引用同一份常量；不复制测试 shader，也不更改场景提交/骨骼算法。
Renderer 63项内置清单新增ParticleUnlit与rigid_skin_fallback，避免严格游戏模式首次使用失效。
源头是内部实现支持，不提升为游戏侧推荐 API；EntityRenderIntegration 编译及既有蒙皮用例验证。
SkinnedAnimationTests 21用例/99断言通过；迁移三段raw字节与原常量逐字相同，API/骨骼算法未变。

> **2026-10-03 — PerspectiveCamera3D**：Core 拥有可序列化的 Multiple 相机组件和
> renderer-independent selection/evaluation，RenderIntegration 拥有 World-scoped provider。
> Renderer 在几何提取前调用 provider；Edit/Host 覆盖优先，不改原 Scene 组件。
> active/priority/Entity id/component id 选主，Transform 插值驱动姿态、scale 忽略。
> 非有限/零 quaternion pose 不选，非法镜头参数安全修正；Scene 在显式字段完成后
> 补建缺失 Transform。相机 identity 纳入 World/Entity generation/组件 id，切换与
> requestCameraCut 失效 TAA、Motion 和 AutoExposure 历史。World teardown 在系统销毁前
> 撤销 provider；调用在 owner thread，不是 ECS 并发读取接口。测试见
> unittest/AYTest_PerspectiveCamera.cpp；尚未验收 split-screen、层过滤或真实 GPU 镜头切换。

> **2026-10-02 — 组件身份与装载顺序**：组件 ID 恢复先检查实体全部槽位的冲突，
> 再修改目标，失败不消耗恢复机会。Scene 在每个实体显式组件读取完成后按文件
> 顺序执行 `afterSceneDeserialize`，避免 Sprite/Camera 补建 Transform 遮蔽后续
> 显式数据；真正重复的 Single 组件仍拒绝。Actor 优先初始化资产声明的 Transform
> 和槽位 ID，只有未声明时使用旧版确定性 ID；快照按实际组件类型确认根 Transform，
> 同一 ID 贯穿继承、字段覆盖与 Scene 往返。根 Transform 不允许通过继承删除。

> **2026-09-29 — 外部骨骼姿势控制权**：`SkeletonComponent::externalPoseOwner`
> 是主线程、非序列化的独占 lease，非空时 AnimationSystem、BlendSpaceSystem 和
> StateMachineSystem 在加载、参数同步、tick、Notify 与姿势写入之前跳过对象。
> AYSequence 使用独立采样器，不替换或重置原 player；原状态机、BlendSpace 与
> 播放时间冻结，释放后从原状态继续。该字段不是 setter/线程锁，其他 writer
> 也必须遵守 lease。所有者负责在 World teardown 前释放；组件 ABI magic 为 SKC4。

> **2026-09-09 — WorldLit2D 第四刀（Shadow authoring）**：
> `SpriteComponent` 与 `TilemapComponent` 新增可序列化 `castShadow`，
> 默认开启以保持 WorldLit 的直觉行为。RenderSystem 显式生成
> `ShadowFlags`：WorldLit 始终接收现有 Deferred shadow，是否投射
> 由组件开关决定；SceneOverlay 固定为 `None`，不会进入世界阴影。
> Tilemap chunk 的实际网格边界由 AYRenderer 上传阶段记录，因此大块
> 地图不再被当成一个以原点为中心的单位立方体。

> **2026-09-09 — WorldLit2D Tilemap 第三刀（Perspective visibility）**：
> WorldLit Tilemap 不再全图 fail-open 提交。每个 chunk 使用主相机的未抖动
> view/projection、Tilemap 完整 world transform 与引擎 LH `[0,1]` 六个齐次
> 裁剪面做保守相交测试；prefetch margin 会扩张待测 chunk。屏外、相机后方及
> far-plane 外的 chunk 不创建 mesh、不提交 DrawItem，并继续由既有 LRU 管理
> 已驻留网格。相机尚未设置、矩阵/边界非有限时仍安全 fail-open。当前实现会
> 线性扫描有限 chunk 网格；若超大地图需要更低 CPU 成本，可在此契约上追加
> quadtree/row-range broad phase，无需再改材质或 Pass 路由。

> **2026-09-09 — WorldLit2D Tilemap authoring 第二刀**：`TilemapComponent`
> 现可显式选择 WorldLit，并序列化 normal/roughness/emissive 路径与
> metallic/roughness/AO/emissive/alpha-cutoff 参数。Tilemap 保留 chunk mesh
> 的 baked atlas UV，并把 Nearest/Linear/4-tap/9-tap 质量传给 GBuffer；材质
> 缓存与 chunk 几何缓存分离，Inspector 标量修改不会复制常驻网格。颜色贴图
> 以 sRGB、数据贴图以 linear 加载，可选贴图失败时 fail-close；异常 atlas
> filter 会钳制到 Linear，避免越界。WorldLit 不再套用 Ortho overlay 的层掩码
> 与可见块剔除；该第二刀遗留的 3D chunk 可见性已由上方第三刀关闭。

> **2026-09-09 — WorldLit2D Sprite authoring 第一刀**：`SpriteComponent`
> 追加可序列化的 `renderDomain`、normal/roughness/emissive 路径与
> metallic/roughness/AO/emissive/alpha-cutoff 参数。默认值仍为 Overlay；
> `renderDomain=1` 时 `SpriteRenderSystem` 用 3D 世界变换提交 WorldLit payload，
> 按颜色/数据语义加载 sRGB/linear 贴图并创建共享 `Material2D`。WorldLit
> 不再被 OrthoCamera overlay 范围错误剔除；无效域值安全退回 Overlay。
> 当前仅覆盖 Sprite cutout，Tilemap 与 blend 在后续阶段扩展。

> **2026-09-02 — Core / Integration split**: `AYEntityCore` no longer links
> Renderer, Animation, Physics, Script, Network, Resource or EventSystem.
> Feature components, systems and the physics bridge live in explicit
> `AYEntity*Integration` targets. `AYEntity` remains a full compatibility
> facade only; new composition roots must select integrations individually.

> **2026-09-01 — AY2D production ECS path**: `AYEntity` is the sole
> production owner of engine-scene 2D placement. The priority chain is camera
> 405 → visibility streaming 430 → tile animation 460 → tile/sprite render 510.
> Tilemaps submit visible indexed chunk meshes, and `TilemapRenderStats` exposes
> draw reduction and residency. `ayt::ay2d::World2D` remains a standalone
> CPU/tool path; a logical tilemap must not be live in both ownership paths.

> **2026-09-01 — DET-04 shipped**: code-level `SystemLane` metadata now splits
> fixed Sim, interpolation Bridge and variable-rate Present execution.
> `SimTransformComponent` plus `SimToPresentBridgeSystem` closes the fixed
> translation → float presentation path and is driven by `EntitySubSystem`.

> **变更记录（2026-07）**：引擎集成、`bootstrapModule`、`SparseSet` 指针语义 — 见 [§15](#15-引擎集成与模块引导2026-07)。  
> **变更记录（2026-07-09）**：Simulation / Presentation 分轨（`SystemLane`）— 见 [§14](#14-simulation-vs-presentation-systemlane)；总览见 [`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../AYDocs/ENGINE-DETERMINISM-ARCHITECTURE.md)。

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
| **AYScript** | 独立运行时 | `AYEntityScriptIntegration` 注册 ScriptComponent；Core 不链接 AYScript |
| **AYNetwork** | 独立运行时 | `AYEntityNetworkIntegration` 注册 NetworkComponent；Core 不链接 AYNetwork |
| **AYResource** | Integration 依赖 | Animation/Render/2D integration 持有资产引用；Core 不链接 AYResource |
| **AYRenderer** | Integration 依赖 | `AYEntityRenderIntegration` / `AYEntity2DIntegration` 提交表现数据 |
| **AYPhysics** | Integration 依赖 | `AYEntityPhysicsIntegration` 持有固定步 ECS ↔ Physics 桥 |
| **Determinism** | 架构约束 | `SystemLane` 分轨；DET-04 落地 `SimTransformComponent` — 见 [§14](#14-simulation-vs-presentation-systemlane) |

### 1.4 编译目标边界

`AYEntityCore` 是所有场景与 headless Host 的最小依赖，只包含 World、Entity、
Core component registry、序列化、Entity SubSystem 和确定性 Sim/Bridge。
动画、渲染、2D、物理、脚本、网络分别由独立 integration target 追加组件类型、
System 或 SubSystem。`AYEntity` 目标仅聚合当前启用的全部 integration，供旧代码
兼容；它不能作为新底层模块的默认依赖。

World 销毁前的跨模块清理通过 `WorldLifecycle` 回调扩展点完成。Core 只保存函数
回调，不包含 Renderer 头文件；Render/2D integration 自行释放对应 World 的
scene builder。

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

### 5.1 编译时模板查询（最小稀疏集驱动）

```cpp
template<typename... Components>
class Query {
public:
    explicit Query(World* world) : _world(world) {
        // Cache all requested component storages and choose the smallest
        // dense entity-id array as the candidate set.
        initialize();
    }

    // Iterator walks _candidateIds and advances past ids for which
    // matches(entityId) is false.

private:
    void initialize();

    bool matches(uint32_t entityId) const {
        if (_world->findEntity(entityId) == nullptr) return false;
        for (const IComponentStorage* storage : _storages) {
            if (storage == nullptr || !storage->has(entityId)) return false;
        }
        return true;
    }

    World* _world = nullptr;
    std::array<IComponentStorage*, sizeof...(Components)> _storages{};
    const std::vector<uint32_t>* _candidateIds = nullptr;
};
```

**特点**：
- 不再扫描 `1..MAX_ENTITIES`；遍历成本由最稀有的请求组件数量决定
- 查询构造时缓存组件存储，迭代时通过 `SparseSet::has()` 做 O(1) 交集判断
- `World::findEntity(id)` 使用稳定 ID 槽位 O(1) 定位；销毁实体留下空槽，ID 不复用
- 迭代期间不得直接增删查询涉及的组件；结构变更应延迟到迭代结束后执行

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

### 5.4 固定步物理同步

`EntitySubSystem` 在固定步两侧维护持久绑定缓存，避免每个 tick 重建临时散列表：

- `FixedPrePhysics` 查询 `Transform + RigidBodyComponent`，刷新 `entityId -> binding` 与 `(dimension, bodyHandle) -> entityId`；过期条目通过 epoch 清理。
- `EntityToPhysics` 只在位置或旋转相对上次成功提交发生变化时写入物理命令队列。队列拒绝不会更新提交缓存，因此下一 tick 会重试，并按 tick 聚合告警。
- `FixedPhysics` 仍由 AYPhysics 的 `stepAndWait` 完成固定步屏障，保证随后读取的是本 tick 已发布的快照。
- `FixedPostPhysics` 直接用带 2D/3D 维度的 body key 查找绑定，并将快照姿态、速度写回组件，不再往返查询所有实体。

2D 与 3D 的 `BodyHandle` 数值空间彼此独立，因此快照中的 `PhysicsDimension` 必须参与绑定键，不能只用 handle 数值匹配。

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
- [x] `SystemLane` 元数据（Present / Sim / Bridge）
- [x] `SimTransformComponent` + `SimToPresentBridgeSystem`
- [x] Sim 系统稳定遍历顺序文档化（使用 `getAllEntities()` / 显式 ID 排序）

> DET-04 于 2026-09-01 完成。第六阶段已完成注册状态本地 Sim session、checkpoint/replay；后续网络输入、通用 rollback 协议与确定性碰撞仍分别属于 DET-05–07。工作包见 [`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../AYDocs/ENGINE-DETERMINISM-ARCHITECTURE.md) §7–§9。

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

> **权威文档**：[`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../AYDocs/ENGINE-DETERMINISM-ARCHITECTURE.md)
> 本节定义 ECS 侧已经落地的**分轨契约**；公开枚举位于
> `interface/AYEntity/IEntity.h`，调度入口位于 `World.h`。

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
| **Sim** | movement、det 碰撞、Gameplay `System` host | `fixedUpdate(fixedDt)` + GameLoop `simTick` | `SimTransformComponent`, 玩法状态 |
| **Bridge** | `SimToPresentBridgeSystem` | 每 presentation 帧的 `interpolationAlpha` | 只写 float `TransformComponent` |

**调度**：`EntitySubSystem` 在 `FixedPrePhysics` 调用 `World::fixedUpdate`，只串行执行 Sim；在 `World` 阶段调用 `World::updatePresentation(dt, alpha)`，先执行 Bridge、再执行 Present。旧 `World::update(dt)` 保留并等价于 `updatePresentation(dt, 1)`。

### 14.3 新增 System 时的审查清单

新增 System 必须在 `registerSystem<T>(priority, lane)` 或
`AY_SYSTEM_IN_LANE` 中声明 `SystemLane`；省略时为兼容旧系统默认 Present：

1. 该逻辑是否参与**跨端一致的仿真**？→ **Sim**
2. 是否只影响画面 / 编辑器 / 音频？→ **Present**
3. 是否把 Sim 状态映射到渲染？→ **Bridge**（且仅此一类应写 float `Transform`）

### 14.4 Sim 轨禁止项（DET-01 之前即生效）

即使尚未引入 `Fixed32`，Sim 轨代码也不得：

- 用 `std::unordered_map` / `unordered_set` 的迭代顺序驱动玩法分支
- 使用未 seed 的 `rand()`、`random_device`、墙钟 `time()` 影响结果
- 从 `AYTask` 并行写同一实体的 Sim 组件
- 调用 **Jolt**（`AYPhysics`）或 GPU readback 作为玩法依据

Sim 系统由 `World` 串行执行。同一 lane 内按 priority 排序，同优先级保持
注册顺序。需要遍历实体时使用按单调实体 ID 返回的 `getAllEntities()`，或先
显式按 ID 排序；不得以 SparseSet swap-remove 后的 `Query` 顺序决定玩法结果。

### 14.5 组件与 Bridge 契约

| 组件 | 数值空间 | 消费者 |
|------|----------|--------|
| `TransformComponent` | `Float32` | Render、Editor、非 lockstep 网络复制 |
| `SimTransformComponent` | Q16.16 `FixedVec3` 平移 | Sim 系统、lockstep checksum |
| `DetSimTransformComponent` | 软件 binary32 `DetVec3` 平移（新逻辑推荐） | Sim 系统、按字段 bits 的 checkpoint/checksum |
| 玩法 hitbox / 受击判定 | Sim 代理体 | **不要**在 lockstep 中采样蒙皮后的骨骼矩阵 |

`World::fixedUpdate` 在每个固定步执行 Sim 系统前保存所有 SimTransform 的
`previousPosition`。Bridge 对前后位置按表现帧 alpha 插值，并保留
legacy `Transform` 的 rotation/scale；下述 Det 路线可显式接管旋转，缩放仍属表现层。

2026-10-07 DET-04F：Core 注册独立 `DetSimTransformComponent`（运行时、
非 sceneSerializable/editorAddable）；旧类型与格式不变。两类状态都在
Sim 开始前统一 snapshot。`World::fixedUpdate(DetFloat32)` 验证正且有限，
失败在任何 snapshot/onStart 前抛 invalid_argument；对 `IDeterministicSystem`
直接 typed dispatch，同 priority 保留注册顺序，其他 Sim 系统用 native adapter。
旧 `fixedUpdate(float)` 继续可用；IDeterministicSystem 的 final onUpdate 仅
位复制 dt。2026-10-07 后续接入：标准 GameLoop 提供 FixedTimestep 比率
输入，Entity adapter 优先使用 FrameContext.fixedStep 的 DetFloat32；只有
手工 legacy context 为空时回退 float。Host 墙钟预算仍只决定 tick 数量。

新组件的 setter/translate 对非有限输入/结果失败且无 mutation/revision；
restore 按下述 v2 schema 校验全部 profile、位置/旋转字段与标记后一起更新，保留历史。
Snapshot 是字段数据，不是新增文件格式；外部保存约定字节序和输入/RNG
schema。`importFixed` 软件 nearest-even 转换 raw/65536，可能损失 Q16.16
低位；需显式移除旧权威。Bridge 遇到双 Sim 类型或无效 Det 字段不发布；
native double 插值避免极值差溢出，仅用于 Present，不参与权威校验。
可选 quaternion 旋转见下述第四阶段扩展；scale/physics/script/网络确定性集成尚未实现。

命中判定应使用 Sim 代理（胶囊 / AABB），由动画在 Present 轨驱动视觉，与 [`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../AYDocs/ENGINE-DETERMINISM-ARCHITECTURE.md) §5.3 一致。

### 14.5.1 浮点旋转扩展（第四阶段，2026-10-07）

`DetSimTransformComponent` 默认只拥有平移。`setRotation` 对有限非零输入
做软件归一化，首次启用初始化两组旋转、无历史；World 既有 begin-step
入口在每 tick 的 Sim writer 之前复制已启用的 rotation。`rotateLocal` 使用
rotation * delta 的归一化合成，不经过 native 类型；`disableRotation` 清理
历史并释放权威。成功写增加共享 revision，失败不修改任何字段。

Snapshot v2 与 scalar profile 分开编号，包含 rotation profile 1、两组 XYZW
bits 与 enabled/history 标记。restore 在任何赋值前验证所有版本、有限位置、
非零有限旋转（包括休眠字段）及“有历史必已启用”，随后原位恢复 bits，
避免重归一化破坏回放。旧 v1 五项平移字段显式解码进 fresh v2 Snapshot；
Fixed import 不含旋转，清回 identity/disabled。没有新增持久化文件格式。

Bridge 先校验完整 Det pose，任何非法旋转都阻止部分 Present 写入；以
software nlerp（归一化端点、最短半球）采样启用的旋转，再位复制到 Transform，
同步 previousRotation 并关闭二次插值。alpha 仍是表现输入，不写 Sim。
未启用旋转时保留现有 Present 旋转。外部不得并行改字段或加入竞争 writer。

验收：16 项 DetFloatSim、5 项实际 Host，旋转历史/接管/禁用、失败原子性、
v2 恢复与 legacy 解码、Bridge 无反馈、256 步逐字段回放；数学 oracle 和
平台状态见[第四阶段](../../AYDocs/DETERMINISTIC-FLOAT-STAGE4.md)。

### 14.5.2 软件角速度积分（第五阶段，2026-10-07）

已启用旋转的组件提供 `integrateAngularVelocityLocal/World`。omega 是
`DetVec3` radians/秒，dt 是有限非负 `DetFloat32`；分别右乘/左乘软件
半角 delta。算法与 overflow 规则见 AYMath README 的积分 profile 1。
验证在写入之前完成，失败保留 rotation、previous、flags、revision；成功
只写当前 rotation 并增加一次 revision，tick 统一维护历史。零 dt/omega
仍执行归一化并成功增加 revision，不创建新的历史。

Snapshot 继续使用 v2 的 pose 字段及 scalar/rotation profile，积分没有
改变现存状态的编码。创建状态所用 `kDetMathProfileVersion` 与
`kAngularIntegrationProfileVersion` 应写在 replay/session 的输入契约中。
这是恒定角速度单步运动，不提供 torque、角加速度或刚体动力学。
Core 20 项、Host 5 项验证乘序、非法输入原子性，以及 256/128 步真实
角速度运动检查点回放；见[第五阶段](../../AYDocs/DETERMINISTIC-FLOAT-STAGE5.md)。

### 14.5.3 注册状态会话与输入回放（第六阶段，2026-10-07）

`AYEntity::Determinism` 是依赖 Core + AYReplay 的显式 integration，不给 Core 添加
Replay 依赖。`DeterministicSession` 独占 World Sim/结构推进；现存标准 Host Sim
责任须替换后才能接入（第七阶段提供显式标准 adapter）。单线程、World 晚于 session，关闭 World 会撤销旧 owner。
callback 只访问临时 DetTickContext，不使用隐藏可变捕获或外部副作用。

受管实体仅包含 DetSimTransform、runtime-only DetSimState 和表现 Transform。
自定义权威字段使用注册的 uint64 word blocks，schema/version/field ID 稳定；
不是任意 Component codec。系统按 priority/ID，actor 按 SimEntityId，input/event
按 source/sequence；下一 tick 交付事件，结构命令 tick 末按调用序执行。
销毁 Sim ID 不复用。原生 ID、UUID、RTTI、Transform/alpha 和渲染对象不参与状态比较。

seal 固定 manifest 中的应用/内容/输入版本、软件 dt、数值 profiles、schemas、
systems、RNG seeds 和 global 集。checkpoint 捕获 pose/history/revision/flags、
actor/global words、RNG state/inc、pending events、retired IDs、nextTick。
无效 input/restore 在写入前拒绝；系统错误 fault 后须显式恢复。恢复保持 pose bits，
可重建 Entity，因此按 Sim ID 重新获取表现指针。边界拒绝外部 Sim 写入，不能沙箱
任意 native callback。禁止 callback 修改 World/组件生命周期。

`.rpl` adapter 使用普通 event 0x20001..0x20005 的 manifest/input/witness/checkpoint/
completion，避开旧 raw checkpoint 歧义，Reader 自建 checkpoint 索引。所有内部
payload 显式 LE、有界、带 FNV-1a64；诊断比较完整字段。writer 每 tick 存完整 witness，
reader 恢复最近 checkpoint 后以同一 advance 验证到 seek 目标。单段 256 MiB，最多
100000 records，不实现 delta、分段或 peer 输入收集。缺 seal/未知/损坏记录拒绝。

测试验证 invalid restore 原子性、ID 重建/退役、事件顺序、fault、World teardown、
文件诊断，以及 10000 tick 独立进程录制/实时/回放/5000 checkpoint 继续；反转
注册顺序、改变 Present rate/alpha 与宿主舍入模式，Debug/Release 位型一致。
完整契约与限制见[第六阶段](../../AYDocs/DETERMINISTIC-SESSION-STAGE6.md)。

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
| 510 | `TilemapRenderSystem` / `SpriteRenderSystem` (CM-3) | 注册 scene-builder,把 2D 实体写进 RenderScene；tile 经动画 resolve 后按可见层、图集与 tint 分批，语义阴影单独透明提交 |
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

Tilemap runtime v3 不再要求一个组件只绑定一张规则图集。系统先按 layer 顺序读取
resolved Tile ID，再用资源内精确 sourceRect 生成 UV，并以 `(atlasId, tint)` 为最小
draw batch；层级通过相邻 sortingKey 保持稳定顺序。四分格 shadow mask 生成独立透明
几何，颜色取资源内 `0xRRGGBBAA`，因此 Edit 与 Play 消费同一份烘焙数据。v1/v2 资源
继续走组件 `atlasPath/atlasTexturePath` 的旧规则网格分支。

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

### 15.8 物理参数桥 + ColliderComponent（2026-08-19）

**背景**：RigidBodyComponent 的 `_bodyHandle` 之前没有任何桥接代码创建 body（只有外部场景/手动赋值），
mass / friction / restitution / velocity 字段是死字段。本次补齐 create-time 参数桥，并把碰撞体从无到有建立起来。

**组件**（`include/AYEntity/components/ColliderComponent.h`）：

- `ColliderShapeSpec`：单个形状 spec — `shape`（Box/Sphere/Capsule，枚举值镜像 `ayt::physics::ColliderShape` 但独立，桥接用 switch 映射）、
  `halfExtents`（Box）、`radius`/`height`（Sphere/Capsule）、`isTrigger`、`friction`/`restitution`（material override，Unity 语义挂 collider）。
  **无 offset 字段**：AYPhysics `ColliderDesc` 尚无形状局部变换（Jolt 后端 offset 恒为 identity），等物理层支持再加。
- `ColliderComponent`：`shapes[]`（一个组件 = 一个 body 的全部形状；复合形状 = vector 多条目，避免 per-type 单实例存储改 multi-map）、
  `revision`（与 `Transform::revision` 同语义：mutator（addShape/removeShape/clearShapes）自增；直接改 vector 绕过 →
  桥接层 deep-compare 兜底）。

**桥**（由 `AYEntityPhysicsIntegration` 目标实现）：

- `EntitySubSystem` 只推进 World；独立的 `EntityPhysicsBridge` SubSystem 持有桥，
  并声明 FixedPrePhysics / FixedPostPhysics 阶段。测试用 `setPhysicsManager()`
  注入，生产走 `PhysicsSubSystem::findRegistered()`。
- **FixedPrePhysics `syncEntityToPhysics()`**：
  1. body 无效且 `SyncMode != None` → 用组件字段构造 `RigidbodyDesc`（type 由 static/kinematic 映射、mass、velocity、friction/restitution、pose）→ `createRigidbody` → `setBodyHandle` 写回。失败保持 binding 下 tick 重试。
  2. collider 同步：`revision` 变更或 deep-compare 不等 → destroy 旧 collider 句柄 → 按 `shapes[]` 顺序重建。
  3. pose 提交（EntityToPhysics，revision 快路径 + poseEquals 兜底，与 2859aa2 相同）。
  4. epoch sweep：实体销毁 / 组件移除 → **destroy 自己创建的 body + collider**（修复此前 body 泄漏）。
- **FixedPostPhysics `syncPhysicsToEntity()`**：PhysicsToEntity snapshot → Transform `applySimulationPose` + velocity 写回（原逻辑不动）。
- **所有权规则**：桥创建的 body（`ownsBody`）在 sweep / manager 切换时销毁；外部预置句柄（场景作者）只收养不销毁。
  `shutdown()` 只清表不销毁（PhysicsManager 自身 shutdown 会销毁后端 world 的全部 body，避免子系统关停顺序导致悬垂）。

**注册点**（全部位于 Physics integration）：

- `EntityPhysicsIntegrationModule::registerTypes()` 注册 RigidBody 与 Collider；
  `AY_FINALIZE_REGISTRATION_METADATA(ColliderShapeSpec)` 必须先于 ColliderComponent。
- `ComponentRegistry` 的 scene callbacks 提供 `.ayscene` factory 与序列化入口，
  Core 不再维护 Collider 的硬编码 wire entry。
- 编译期注意：bridge 实现 TU 必须自行 include `<AYPhysics/PhysicsManager.h>` 等实体头 —— `EntityPhysicsBridge.h` 只做前置声明。

**测试**（1132/1132 PASS）：

- `ComponentTest.cpp`：默认值 / revision 自增语义 / 直接 vector 写不 bump（兜底文档化）。
- `EntityPhysicsBridgeTest.cpp`（8 case，Mock 后端命令/载荷捕获 + Null 行为路径）：
  body 创建参数映射（mass/friction/restitution/velocity/pose 逐字段断言 payload）、static/kinematic type 映射、
  `SyncMode::None` 不创建、collider 创建与幂等（二次 sync 零新命令）、revision bump 重建、直接 vector 写兜底重建、
  空 shapes 零 collider、实体销毁连带 body+collider 销毁、外部句柄收养不销毁、2D 路径。
- `AYTest_SceneSerializer.cpp`：ColliderComponent `.ayscene` round-trip（vector-of-struct + enum 序列化首例）。

### 15.9 ComponentRegistry、IModule 与 Core/Integration 拆分（2026-09-02）

组件类型改为启动期显式注册：`ComponentRegistry` 以稳定名称保存 C++ 类型、
编辑器分类、动态增删回调和可选 `.ayscene` 序列化回调。注册表在所有
`IModule::registerTypes()` 完成后由 Host 调用 `seal()`；封存后拒绝新增或
修改描述符，编辑器可以安全枚举并持有描述符指针。

`Entity::addComponent<T>()` 不再按需注册。每个 World 首次创建 T 的
SparseSet 时验证一次 T 已注册，之后 add/get/query 继续直接访问存储。这样既
保留未注册类型的 fail-fast 行为，也不把注册判断放进常规组件访问热路径。

`ComponentFactory` 和场景序列化不再维护第二张硬编码表，而是统一查询
`ComponentRegistry`。场景写入使用注册表中的规范名称，不再依赖实例
`getName()`；因此 `HealthComponent` 的运行时名称 `Health` 也会稳定写成
线缆名 `HealthComponent`。场景保存和读取本身不执行注册，避免在运行期
重新打开类型集合；Host 必须在进入场景生命周期前完成注册和封存。

`EntityComponentModule` 只注册 Transform、SimTransform、Health 等 Core 类型；
`EntityRuntimeModule` 只接管 Entity SubSystem 与 Core systems。Animation、
Render、2D、Physics、Script、Network 的类型和行为由对应
`Entity*IntegrationModule` 注册，因此未选择某项能力时既没有模块节点，也没有
静态库依赖。

旧的 `registerEntityComponents()`、`bootstrapEntityCore()` 与
`bootstrapModule()` 继续复用同一注册表，其中 `bootstrapModule()` 属于完整 facade
兼容路径。生产 Host 在所有模块 `registerTypes()` 后封存上下文中的同一注册表，
类型模块不得在模块路径中自行回退到另一份进程单例。

### 2D 粒子碰撞面的表现层数据流

`AYEntityParticleIntegration` 注册可序列化的
`ParticleSurface2DComponent`。`ParticleSimulationSystem` 每个 Present 帧
从 `Transform + ParticleSurface2DComponent` 建立临时世界 XY 矩形查询：
Ground 查询返回落点上方出生高度以内的最高表面；Sweep 查询返回运动线段
首先进入的 `solid` 矩形。场景查询对象仅在本帧粒子更新期间存活，粒子
运行时只保存选中的高度和标签，不持有 Entity 或组件指针。命中光斑由
表现系统持有，最多 256 个，同时每个粒子实例每帧最多报告 256 次命中。

这些区域与物理碰撞体分离，运行在可变表现帧；其命中事件不驱动确定性
Gameplay。当前矩形只使用 Transform 平移，不使用旋转或缩放。俯视模式
将 XY 作为固定落点，另用视觉高度推进，因此粒子的 XY 初速度与重力必须
为零。场景保存几何配置，不保存运行时粒子、命中事件或光斑。

---

## 16. 参考

- [Engine determinism architecture](../../AYDocs/ENGINE-DETERMINISM-ARCHITECTURE.md) — dual-layer Sim/Present, DET-01..08
- [Flecs ECS](https://github.com/SanderMertens/flecs)
- [Entt ECS](https://github.com/skypjack/entt)
- [Unity Entity Component System](https://docs.unity3d.com/Packages/com.unity.entities@latest/)
- [Unreal Gameplay Architecture](https://docs.unrealengine.com/en-US/ProgrammingAndScripting/GameplaySystems/networking/)
- [O3DE Game Entity](https://o3de.org/docs/user-guide/components/)

### 14.5.4 标准 Host/Scene 会话 adapter（第七阶段，2026-10-07）

`AYEntity::DeterminismHost` 依赖 Determinism + Application + Scene + EventSystem，
通过晚声明函数建立，不进入 All，避免 Application→Entity→Application 依赖环。
Core 的 IEntitySimulationDriver 是基础设施：标准 Entity 单个 shared driver 返回
nullopt 选择 legacy Sim，true 提交成功，false 映射 GameLoop 固定步 Blocked。
绑定期间 Entity.requiresOwnerThread 为 true，phase layer 固定在 Host caller 线程。
Controller 的 Scene recipe 显式选择 ordinary / Live / Record / Replay，只有一个
推进 owner；没有故障 fallback。固定 step 比例与 bits、Host simTick 在输入采集前验证。
Live/Record 输入编码后经 Session.advance，Replay 经同一内核校验。

World 持有借用 shutdown observer，驱动/Controller 的 shared implementation 保活。
shutdown 先交换并通知 observer，健康 Writer finish，然后释放 actor/session，最后
World 撤销 owner 和存储。订阅借用 weak state，模块结束移除自己的服务和 driver。
Scene current 变更由 EventBus 和标准边界解析；旧会话先 close，再 configure 新会话。
同 Scene 内容重载需要 stop/restart，禁止 callback 重入生命周期和控制操作。

GameLoop Blocked 不提交固定 tick，暂停并清除欠账；真实 phase failure 仍 fatal。
Live restore / Replay seek 都先暂停；Record 恢复拒绝，存在目的文件拒绝覆盖。
partial callback fault 必须显式恢复；I/O 失败不能撤销已执行 tick。host simTick 与
presentation frame index 只作调度/采集元数据，权威身份为 session.nextTick。
完整行为和限制见[第七阶段](../../AYDocs/DETERMINISTIC-HOST-STAGE7.md)。

### 14.5.5 类型化注册字段（第八阶段，2026-10-07）

DetTypedStateSchema initial 的封闭 variant 类型映射到显式固定 wire code，
不以 variant index、RTTI 或 sizeof 作为持久身份。Session 拷贝 descriptor，
legacy/typed 共用 schema ID 空间；逻辑 field ID 保持稳定，多分量字段保存
2/3/4 canonical lanes。DetSimState 的 words 继续作为内部显式持久化存储，
业务 typed access 返回值副本并校验 field type；typed blocks 禁止 raw span。
字节 codec/初始 defaults/回调访问共用验证规则。字段写入先预检后全量替换；
callback 非法 typed access 设置 sticky fault，即使 catch 也不能提交该 tick。

含 typed fields 的 manifest 2 保存每 field 的 type 和 default lanes，legacy-only
仍生成原 manifest 1；checkpoint envelope 1/.rpl v2 不变，无隐式迁移。
decode 完整解析有界 manifest、profile、field/default 和 actor/global shapes，
最后才替换输出；restore 进一步要求封存 manifest 完全一致，再原子预检恢复。
diff 将每个 lane 映射回逻辑 field ID + zero-based lane；scalar 为 lane 0。
整数固定-width bit pattern 零扩展到 u64，binary32 只用低 32 bits，bool 0/1。
非有限 binary32 拒绝但 signed zero/subnormal 保留；raw quaternion 只要求有限，
rotation 入口继续负责旋转有效性。EntityRef 是可空/未解析 stable ID，非 ownership。
128 logical fields/schema，最多 512 lanes，仍同时受 manifest/checkpoint byte budgets 限制。
Windows typed Host 10k 四进程/Debug-Release 和 legacy golden 验收见[第八阶段](../../AYDocs/DETERMINISTIC-STATE-STAGE8.md)。

### 14.5.6 软件 Float32 2D 碰撞（第九阶段，2026-10-07）

实现位于既有 Determinism target，不增加 Core 的依赖。Math DetGeometry2D
负责 value-only closed-ray/strict-sweep profile 1；Entity 安装 typed body fields
及全局 policy/pair-history。所有 actor/schema/system 身份稳定，双向 mask、
min.x/ID broadphase、canonical pairs、time/axis/ID 窄相次序明确。
每 tick scratch 先解 kinematic/static AABB，法线轴速度清零、切线滑动，接触
重建后最多 16 ULP 向外调整并复检；无 initial penetration/depenetration。
计算 final positive trigger overlap，再 merge 旧/新有序 pair 生成下一 tick 事件。
容量和语义验证先于本系统 pose/history 提交；emit 总 budget、前序 callbacks 和
kernel history 仍按 session fault/explicit restore 契约处理，不声称全 tick 自动回滚。
Typed restore 验证表示；profile/policy/跨字段 history/body 语义在 callback 前检查。
无持久 proxy 缓存，checkpoint 直接保存 typed bodies/globals 与 pending events。
24-byte LE event profile/phase/u64 pair 是 session payload，现有 manifest 2 /
checkpoint envelope 1 / replay v2 不变。默认 schema 4/5、system 30、event 0x30001；
geometry/collision profiles 和 policy defaults 入 manifest，改变拒绝跨配置恢复。
64 active bodies / 61 trigger pairs 硬上限，XY translation only；未来动态刚体、
旋转/3D shapes、连续触发 crossing 和网络/脚本需另立阶段。验收见第九阶段文档。

### 14.5.7 语义校验与访问成本（第十阶段）

Typed schema 注册时编译 DetStateLayout，字段二分定位，读取不分配 defaults。
边界 outside-write 检查直接比较注册 bits/container，诊断 API 保持首字段/lane。
纯只读 validator ID/version 表在 manifest 3 的 global table 后、FNV 前编码；
u32 count + sorted (u32 id,u32 version)，1..64，非零唯一 ID/version。
无 validator 时保留 manifest 1/2；manifest 3 总是包含 typed/Word type codes。
表示验证后执行 callback，异常/拒绝包含 validator ID；Session 重入拒绝是 sticky。
tick 在 scratch checkpoint 应用 spawn/despawn/events/nextTick 再验证，合格后提交
World 结构与 last witness；前序 callbacks 的字段写入仍需显式 restore。
不可把可访问的 const checkpoint 当作 C++ 沙箱；纯函数/版本维护是调用方契约。

### 14.5.8 网络输入/状态屏障（第十一阶段）

DetLockstepBarrier 是 std-only 固定 roster 协议；Session adapter 保持 Core 不依赖
Network，INetwork sendTo helper 由使用者显式链接。握手包含初始 tick/hash、
完整 manifest、规范 roster 和 window/buffer 参数。每条消息 header 是 magic
0x4c534441/profile1/sessionId64/epoch32/member32/kind32/tick64/bodySize32，
末尾 FNV64；kind 1 Hello、2 Input、3 Hash。LE、48-byte envelope、有界解析。
current input + hash barrier 防止缺员/分歧继续；输入 source 必须属于成员，合帧
重走 Session canonical validation。Hash 是 full checkpoint FNV，不是认证。
history/future/buffer 限制固定；retain+resend 修复乱序/重复/应用丢包，不无限缓存。
配置和 handshake 不进入 gameplay checkpoint；恢复 agreed state 后新 epoch 重建。
Wall clock/connection/member admission/timeout 层独立，输入与完整 state 复用 replay。
