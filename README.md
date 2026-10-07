# AYEntity

2026-10-05：RenderIntegration 的 rigid/diagnostic fallback shader 源移到 Renderer 共享
FallbackMeshShaderSources.h；运行时与默认离线清单引用同一份字节，发布游戏不临时编译。
未改变皮肤/材质路径选择或 gameplay facade；粒子默认源也由 Renderer 清单覆盖。

AYEntity 是 AY Engine 的实体组件系统。`AYEntityCore` 负责 Entity/Component
存储、System 调度与场景序列化；动画、渲染、2D、物理、脚本和网络绑定均为
显式选择的 integration target，不再成为 Core 的反向依赖。

完整组件测试按 `AY_ENTITY_HAS_NETWORK_INTEGRATION` 编译网络组件用例。
测试目标显式接收 Script/Network 开关，与兼容 facade 的私有编译定义一致，
确保完整配置实际执行两类组件用例，裁剪配置验证 Script 拒绝行为。
关闭网络时其余组件与 2D/物理集成测试继续运行，不要求不存在的网络库。
关闭 Script 集成时，组件用例验证工厂明确拒绝未注册的 ScriptComponent，
并继续运行其余用例；意外的空组件不会导致测试程序解引用崩溃。

## 3D 游戏相机（2026-10-03）

`Transform + PerspectiveCameraComponent` 是推荐入口；使用 `createComponent` 显式实例 API。
Core 注册可保存的 FOV（度）、near/far、active、priority，支持 Headless 求值和 Scene 往返。
安装 RenderIntegration 后自动选最高优先级活动相机，在提取几何前同步；同分按 Entity id
和持久组件 id 排序。Transform 的 scale 不影响镜头，朝向为 LH / Y-up / 本地 +Z。
Editor/Host 覆盖优先，清除覆盖恢复游戏相机；无有效相机回退默认镜头。
瞬移调用 `requestCameraCut()`；换 World/相机/覆盖来源会重置时域历史。
不含 split-screen、3D layer mask 或镜头混合。纯 CPU 回归入口 `AYEntity_PerspectiveCameraTests`。

## 公开接口

组件实例 ID 在同一 Entity 内唯一；恢复 ID 时，冲突或已恢复后的改号均被拒绝。
Scene 先读取实体全部显式组件，再执行依赖补建回调，Sprite/Camera 与 Transform
在文件中的顺序不影响装载。Actor schema 3 保留显式 Transform 的槽位 ID；
仅当类未声明 Transform 时才补建兼容旧资产的默认槽位，继承不能删除根 Transform。

`AYEntity_IdentityTests` 覆盖注册表、Actor 和组件身份场景回归；启用
`AY_ENABLE_ENTITY_2D_SCHEMA` 即可在关闭 Renderer/AY2D/Particle 的配置中运行。

```cpp
#include <AYEntity.h>
#include <AYEntity/IEntity.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYEntity/EntityRenderIntegrationModule.h>
#include <AYEntity/EntityPhysicsIntegrationModule.h>
#include <AYEntity/components/TransformComponent.h>
```

入口头文件位于模块根目录；抽象接口位于 `interface/AYEntity/`，其余公开头位于 `include/AYEntity/`。

## CMake 目标与依赖边界

| 目标 | 内容 | 额外依赖 |
|---|---|---|
| `AYEntityCore` / `AYEntity::Core` | World、Entity、注册表、序列化、Entity SubSystem、Sim/Bridge | AYModule、AYCore、AYGameLoop、AYReflect、AYSerializer、AYMath |
| `AYEntityAnimationIntegration` | 动画组件与系统 | AYAnimation、AYResource、AYEventSystem |
| `AYEntityRenderIntegration` | Mesh/SkinnedMesh 表现系统 | AYRenderer、Animation integration、AYResource |
| `AYEntity2DIntegration` | Tilemap/Sprite/OrthoCamera 表现系统 | AYRenderer、AYResource |
| `AYEntityParticleIntegration` | 粒子播放、2D 场景碰撞面与命中光斑 | AYParticle、AYRenderer、AYResource |
| `AYEntityPhysicsIntegration` | 物理组件与固定步双向桥 | AYPhysics |
| `AYEntityScriptIntegration` | ScriptComponent 类型注册 | 无具体脚本运行时依赖 |
| `AYEntityNetworkIntegration` | NetworkComponent 类型注册 | 无具体网络运行时依赖 |
| `AYEntity` / `AYEntity::All` | 旧调用方的完整兼容 facade | 当前构建中启用的全部 integration |

新模块应链接 `AYEntityCore` 和自己确实使用的 integration；只有旧 Demo 或明确
需要完整表面的产品才链接 `AYEntity`。

## 2D 粒子碰撞面

给带 `Transform` 的实体添加 `ParticleSurface2DComponent`，用 `halfExtent`
在世界 XY 平面标记矩形。`ground` 允许俯视粒子按 `height` 选择最高落点；
`solid` 允许横版粒子沿 XY 运动轨迹碰撞。`collisionMask` 过滤粒子，
`surfaceTag` 随命中事件返回。场景会保存这些配置，但矩形本身不渲染；
Sprite/Tilemap 美术和碰撞区域需要分别配置。当前仅支持轴对齐矩形，
不应用 Transform 的旋转或缩放，也不作为游戏物理碰撞体。

粒子系统每个表现帧读取场景碰撞面，CPU 粒子命中后可消失或反弹，
并在启用时显示短暂光斑。`AYEntity_ParticleTests` 和
`AYEntity_ParticleCollisionSmoke` 覆盖场景装载、落点选择及扫掠命中。

## 2D Sprite 序列帧

规则网格 Sprite Sheet 使用 `SpriteComponent + SpriteAnimationComponent`。动画格从
纹理左上角开始，按从左到右、再从上到下编号；组件提供列数、行数、起始格、帧数、
统一帧时长、Loop/Once 和播放开关。`SpriteAnimationSystem` 在 Sprite 渲染系统之前
更新 UV，因此本帧推进会在同一表现帧可见。暂停保留帧内时间，Once 停在末帧；场景
重载只恢复作者参数，不恢复上次运行进度。

该组件刻意只覆盖最常见的规则网格 MVP。非规则区域、逐帧时长、动画事件、命名
片段与状态机应由后续独立 clip 资产描述，而不是继续扩张组件字段。

ECS 结构、Simulation/Presentation 分轨和引导流程见 [design.md](design.md)。

## 确定性 Simulation / Presentation 分轨

DET-04 已提供三类系统通道：`SystemLane::Sim` 只由固定步进入口驱动，
`SystemLane::Bridge` 在每个表现帧接收插值系数，现有系统默认属于
`SystemLane::Present` 并继续接收可变 `dt`：

```cpp
world.registerSystem<MovementSystem>(100, SystemLane::Sim);
world.fixedUpdate(fixedDt);
world.updatePresentation(frameDt, interpolationAlpha);
```

为实体同时添加 `SimTransformComponent` 与 `Transform` 后，内置
`SimToPresentBridgeSystem` 会把相邻固定帧的 Q16.16 位置插值为表现层浮点
位置。`World` 会在每次 Sim 步开始前统一保存 `previousPosition`，所以同一
步内多个 Sim 系统写位置不会破坏插值起点。

`SimTransformComponent` 保留旧 Q16.16 平移。新确定性逻辑可选择下面的
软件 binary32 组件；旋转/缩放的确定性契约尚待扩展。Sim 系统需要按实体 ID 稳定遍历：可使用
`World::getAllEntities()`，不要依赖 SparseSet swap-remove 后的 `Query` 顺序。

### 软件 binary32 Sim 平移

`DetSimTransformComponent` 是独立运行时组件，使用 AYMath `DetVec3`，
由 Core 类型注册安装；不写 `.ayscene`、不作为编辑器可添加的作者组件。
与 `Transform` 同时添加后，既有 Core Bridge 自动发布插值平移，保留
表现层 rotation/scale，不写回 Sim。每实体只能有一个 Sim 平移权威。

```cpp
using D = ayt::math::DetFloat32;
using V = ayt::math::DetVec3;
class MovementSystem final : public ayt::entity::IDeterministicSystem {
public:
    const char* getName() const override { return "Movement"; }
    void onDeterministicUpdate(D dt) override {
        for (auto* entity : ayt::entity::World::instance().getAllEntities())
            if (auto* sim = entity->getComponent<ayt::entity::DetSimTransformComponent>())
                (void)sim->translate(V::fromInts(3, 0, 0) * dt);
    }
};
// 在已初始化且 Core 类型已注册的活动 World 中组装：
auto& world = ayt::entity::World::instance();
auto* entity = world.createEntity();
entity->addComponent<ayt::entity::Transform>();
auto* sim = entity->addComponent<ayt::entity::DetSimTransformComponent>();
sim->setPosition(V::fromInts(1, 2, 3));
world.registerSystem<MovementSystem>(100, ayt::entity::SystemLane::Sim);
// 只有负责 tick 的自定义宿主调用；不要重复 tick 标准 Host 已拥有的 World：
world.fixedUpdate(D::fromInt(1) / D::fromInt(60));
```

typed dt 必须有限且正；非法输入在 snapshot/onStart/写入前抛
`std::invalid_argument`。`IDeterministicSystem` 收到原始 DetFloat32 dt；
其他旧 Sim 系统走显式 native adapter。标准 EntitySubSystem 仍传 native
float dt，经 `IDeterministicSystem::onUpdate` 位复制进入 typed 回调；需要
精确 tick 比率的玩法应由约定整数比率构造 dt，或由拥有 tick 的宿主使用
typed World 入口。类型接入不保证第三方物理/旧系统自动确定化。

`setPosition/translate` 返回 bool，非有限值或溢出不写入、不推进 revision。
`snapshot/restore` 保存前后 position bits、revision、history 标志及 scalar
profile；未知 profile/非有限字段恢复失败且不改变状态。保存时显式编码
字段和字节序，外部输入/RNG/system 状态由宿主同时 checkpoint。
`importFixed` 明确以软件舍入将 Q16.16 转为 binary32，复制历史与 revision，
不修改旧组件；大坐标可能丢失低位。完成后移除旧组件，并升级回放 schema。

Bridge 跳过同时具有两类 Sim 权威或存在非有限 Det 历史/位置的实体；alpha
仅用于表现，clamp 到 0..1，NaN 取 0。Sequencer 拒绝两种 Sim 权威，包括
播放后动态添加的组件。`AYEntity_DeterministicTests` 仅链接 Core，覆盖
旧 lane、typed tick、检查点重放、迁移与 Bridge；完整 AYEntityTest 也含新用例。

## 显式组件注册

组件必须在创建 World 存储前注册。模块在 `registerTypes()` 阶段调用
`registerComponent<T>()`；需要进入 `.ayscene` 和编辑器动态增删链路的类型
调用 `registerSceneComponent<T>()`。所有模块完成后由 Host 封存注册表：

```cpp
#include <AYApplication/EngineModuleRuntime.h>

ayt::app::EngineModuleRuntime runtime(host);

if (auto result = runtime.modules().emplace<
        ayt::entity::EntityComponentModule>(); !result) {
    // report result.message()
}
if (auto result = runtime.modules().emplace<
        ayt::entity::EntityRuntimeModule>(); !result) {
    // report result.message()
}

if (auto result = runtime.prepare(); result) {
    runtime.context().componentRegistry().seal();
    result = runtime.install();
}
```

`Entity::addComponent<T>()` 不再隐式注册类型；它只在每个 World 第一次为
该类型创建存储时验证注册状态，因此普通 add/get/query 不增加注册分支。
场景保存和读取也不会触发隐式注册，Host 必须先完成上述启动阶段。
`EntityComponentModule` 从当前 `IModuleContext` 获取 Host 选定的注册表，只注册
Core 组件；`EntityRuntimeModule` 只安装 Entity SubSystem 与 Core systems。
Presentation、Physics、Script 和 Network 必须由对应的
`Entity*IntegrationModule` 节点显式加入。`bootstrapEntityCore()` /
`bootstrapModule()` 继续作为旧调用方和新建 Scene World 的兼容入口。
