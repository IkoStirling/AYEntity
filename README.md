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
| `AYEntityDeterminism` / `AYEntity::Determinism` | 注册状态 Sim 会话、检查点、逐 tick 校验与 .rpl 回放/seek | AYEntityCore、AYReplay |
| `AYEntityDeterminismHost` / `AYEntity::DeterminismHost` | 标准 Host/Scene 会话、Live/Record/Replay、暂停恢复及收尾 | Determinism、AYApplication、AYScene、AYEventSystem |
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
软件 binary32 组件；旋转已提供可选权威，缩放保持表现字段。Sim 系统需要按实体 ID 稳定遍历：可使用
`World::getAllEntities()`，不要依赖 SparseSet swap-remove 后的 `Query` 顺序。

### 软件 binary32 Sim 平移

`DetSimTransformComponent` 是独立运行时组件，使用 AYMath `DetVec3`，
由 Core 类型注册安装；不写 `.ayscene`、不作为编辑器可添加的作者组件。
与 `Transform` 同时添加后，既有 Core Bridge 自动发布插值平移，保留
表现层 scale，不写回 Sim。旋转默认不接管；`setRotation(DetQuaternion)` 成功后
接管旋转，`rotateLocal` 更新局部旋转，Bridge 发布最短半球 nlerp。
`integrateAngularVelocityLocal/World(omega,dt)` 在局部/世界坐标积分恒定角速度
（radians/秒，dt≥0）；需先启用旋转，失败不修改字段，成功增加 revision。
`disableRotation` 释放旋转权威。每实体只能有一个 Sim Transform 权威。
Snapshot v2 保存两组位置/旋转、scalar/rotation profile 与启用/历史标记；
旧平移字段须显式解码进新 Snapshot，不能按旧内存布局恢复。函数/积分
算法 profile 另存于输入/session manifest，见[第五阶段](../../AYDocs/DETERMINISTIC-FLOAT-STAGE5.md)。

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
其他旧 Sim 系统走显式 native adapter。标准 EntitySubSystem 现在直接使用
`FrameContext::fixedStep` 的 DetFloat32；GameLoop 默认软件比率 1/60，
可通过 `IGameLoop::setFixedTimestepRatio` 在启动前或帧间配置。手工 legacy
context 没有 fixedStep 时才回退到 float World 入口。不要额外推进标准
Host 已拥有的 World；物理/旧系统仍需独立满足确定性契约。

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

## 确定性会话与回放（第六阶段）

显式链接 `AYEntity::Determinism`，创建 `DeterministicSession`，注册固定 word schemas、
全局字段、RNG streams 与稳定 ID callbacks，再添加 SimEntityId actor 并 seal。
callback 的 `DetTickContext` 提供 software dt、canonical input、pose、registered words、
globals、RNG、下一 tick 事件和 tick 末 spawn/despawn。系统按 priority/ID、实体按
Sim ID 执行。检查点覆盖完整注册状态；恢复可重建原生 Entity handles。

会话要求已初始化且无旧 Sim 系统/权威的 World，只有一个 tick owner；
标准 Host 使用显式 `AYEntity::DeterminismHost` 接管，custom owner 自行替换 World Sim 推进。受管 actor 只允许 DetSimTransform、DetSimState
和表现 Transform，不自动保存任意原生游戏组件。隐藏 mutable callback captures、
native math、外部效果与 callback 内 World/组件生命周期操作不满足契约。
Present/Bridge 可独立更新且不得改 Sim 字段。World 须晚于 session 销毁。

`DetReplayWriter` 保存 manifest、输入、完整 post-tick witness、周期 checkpoint 和
completion seal；Reader 严格校验后通过同一 advance 路径回放/seek，给出首个
tick/entity/schema/field 分歧。单段 256 MiB，未 finish 文件拒绝；I/O 失败不回滚
已执行 tick。构建目标包含 `AYEntity_DeterministicSessionTests/Worker`，CTest 的
`AYEntity_DeterministicSessionProcesses` 比较四个独立进程的 10000 tick 与完整状态。
接入示例、容量、软件 profiles 和失败语义见[第六阶段](../../AYDocs/DETERMINISTIC-SESSION-STAGE6.md)。

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

## 标准 Host 确定性 Scene 接入（第七阶段）

显式链接 `AYEntity::DeterminismHost`，将 `DeterministicHostIntegrationModule(options)`
加入标准模块图（依赖 Entity.Runtime），从 `deterministicHost(host)` 取得借用 controller。
selector 为每个 Scene 选 ordinary nullopt 或 Live/Record/Replay recipe，configure 注册
第六阶段状态，input 只编码本 tick 命令。标准 FixedPrePhysics 是唯一 owner；要求
Host 和 recipe 约分后的 rational step/bits 相同，拒绝 legacy native step。
pause/stepOnce/resume、Live restore 和 Replay seek 都走标准 Host。失败返回
GameLoop Blocked，暂停、不提交 tick/事件、清除固定欠账，Presentation/Egress 保留。
Scene 变化释放旧会话；World/Host 关闭先封存健康录制；module shutdown 断开绑定。
Record 要求新目的文件，不支持恢复改写。stop 后明确恢复/restart 或 disconnect。
callbacks 禁止重入生命周期/control；同 Scene clear/load 前 stop 后 restart。
输入边沿映射/量化由应用负责。Core 和 All 不自动启用 adapter，也不自动接管物理/脚本。
用例 `AYEntity_DeterministicHostSessionTests`；独立进程门禁
`AYEntity_DeterministicHostSessionProcesses` 验证真实 Host 10000 ticks。
完整契约、装配示例与验收见[第七阶段](../../AYDocs/DETERMINISTIC-HOST-STAGE7.md)。

## 类型化确定性状态（第八阶段）

`AYEntity::Determinism` 新增 `DetTypedStateSchema` 和 `registerTypedSchema`。
稳定字段 ID 的 default 决定类型；支持 DetFloat32、DetVec2/3、DetQuaternion、
固定整数、bool 和 DetEntityRef。callback 使用 `read<T>/write` 或
`readGlobal<T>/writeGlobal`，初始覆盖使用 `detStateDefaults/writeDetState`。
typed 字段不暴露 raw writable span；非法访问即使被 catch，仍 fault 当前 tick。
Float lanes 要求有限，保留 signed zero/subnormal；raw Quaternion 不暗中归一化。
manifest 2 保存字段类型和默认值；纯 word 会话仍使用 manifest 1，旧字节不变。
检查点 envelope 1/.rpl v2 不变；恢复/解码完整预检，分歧增加 zero-based lane。
类型化 Host 的 10000 tick record/live/replay/seek 已通过 Windows Debug/Release；
`unittest/portable` 为原生 GCC/Clang/ARM64 提供同一 codec 的精确 oracle 入口。
示例、格式、容量及实际验收见[第八阶段](../../AYDocs/DETERMINISTIC-STATE-STAGE8.md)。

## 确定性 2D 碰撞（第九阶段）

链接 `AYEntity::Determinism`，添加 actor 前 `installDetCollision2D` 注册 typed
body/history schemas 和无持久缓存的系统，`detCollisionBodyState2D` 构造初始
block。Earlier input 系统写 DetBodyVelocity，碰撞统一计算 world XY AABB 扫掠
阻挡与滑动，时间/轴/稳定 ID 决定命中次序。只解 Kinematic solid 对 Static solid；
初始穿透拒绝，不解动态刚体/堆叠。Trigger 不阻挡，tick 末正面积重叠按规范 pair
生成 Enter/Stay/Exit，下一 tick 交付；穿过后终点已离开不生成事件。
Body/velocity/policy/history 与 pending events 全部进入现有 checkpoint/replay。
默认 64 body/32 trigger pairs，配置范围 1–64/1–61；应选择场景需要的容量。
安装失败销毁部分配置；语义/容量失败需显式 restore，前序系统和 pose history
不自动回滚。无 `.ayscene` component 或 Box2D/Jolt 迁移。几何 oracle、边界测试、
真实 Host 10k 与 SDK 契约见[第九阶段](../../AYDocs/DETERMINISTIC-COLLISION-STAGE9.md)。

## 状态语义校验与布局缓存（第十阶段）

`registerValidator(id,version,callback)` 注册只读纯函数，校验 complete checkpoint
的跨字段、跨实体约束。seal、checkpoint、restore 和 tick 的预期结构提交边界
都检查；restore 错误在 World 写入前返回，tick 错误 fault 后显式 restore。
最多 64 validators，按 ID 执行；ID/version 进入 manifest 3，没有 validator 的
会话继续输出原 manifest 1/2。禁止隐藏可变捕获、外部效果和 session 重入。
`DetStateLayout` 拥有验证后的 metadata/defaults，适合重复初始化/读取/写入；
Session 自动缓存布局，保持同一 canonical lane 和 sticky fault 规则。
验证和测量见[第十阶段](../../AYDocs/DETERMINISTIC-VALIDATION-STAGE10.md)。

## 通用网络 Lockstep（第十一阶段）

`DeterministicLockstep` 显式持有 sealed Session 的推进权，固定 1–8 成员各提交
一个 owned DetTickInput（包括 no-op），按 source/sequence 合帧。manifest/初始
checkpoint/roster 握手一致，当前输入与 hash 都齐全才 advance；缺包 stall，
相同 duplicate 幂等，冲突/分歧停止。纯协议 DetLockstepBarrier 无网络/World 依赖。
应用负责 admitted/authenticated connection→stable member、channel 和重发/超时。
链接 AYNetwork 时可使用 `sendDetLockstepPackets`，勿重复安装 Host 的 Sim owner。
可传入 DetReplayWriter.advance sink；失败后用 agreed checkpoint+new epoch 重建。
接口、wire format、限制和测试见[第十一阶段](../../AYDocs/DETERMINISTIC-LOCKSTEP-STAGE11.md)。

## 扩展确定性 2D 碰撞（第十二阶段）

安装前设置 `DetCollision2DConfig.extended=true` 启用 profile 2，原 profile 1
字节/行为保持兼容。所有移动在共同时间线上 relative sweep，按 time/axis/
stable pair 处理：两个 Kinematic 等权共享法线速度，MovingObstacle 以规定速度
运动并施加法线速度，切线无摩擦/携带。连续触发使用实际 piecewise solved path，
初末均不重叠的 crossing 发 Enter+Exit。固定 history/capacity 仍在 checkpoint。
安装器注册 ID=systemId/version2 纯语义 validator，seal/restore/tick 末预检
body/policy/history/已发行身份/event payload；invalid restore 不修改 World。
扩展 cap 为 60 current+transient pairs，contact passes 1–256 默认128；初始
穿透、挤压/迭代超限 fault 后显式 restore。无动态刚体/摩擦/旋转 shapes。
完整规则和真实 Host 10k 动态场景见[第十二阶段](../../AYDocs/DETERMINISTIC-COLLISION-STAGE12.md)。

Linux x64/ARM64 × GCC 13.3.0/Clang 18.1.3 四组原生 portable 实际通过：
typed codec/layout、三成员10000 tick Lockstep、完整共享 Session 与两种碰撞 solver
10000 tick/5000 checkpoint replay，golden 与 Windows Host一致；完整 Host表现与
实际传输仍使用 Windows集成证据。见[原生验收记录](../../AYDocs/DETERMINISTIC-NATIVE-ACCEPTANCE.md)。

## 共享 Session kernel 与标准 Host Lockstep

`AYEntity::DeterminismKernel` 可构造拥有注册状态的 `DeterministicSession(config)`，
不需要 World/Host；与 `AYEntity::Determinism` 的 World adapter 运行同一核心与碰撞 solver。
`IComponent` 最小生命周期位于独立公共头，既有 IEntity 继续包含它。
`registerLogicProfile(id,version,hash)` 在 seal 前声明权威代码身份，进入 manifest4；
无逻辑身份的会话仍保持 manifest1/2/3 原字节。受限 Logia 通过 AYScript::Determinism 安装。

标准 Scene recipe.lockstep 显式启用固定 roster 联机；input 每个 tick 采样一次 local input，
controller.receiveNetwork/networkPackets 在 owner-thread 外部帧边界接入认证 transport。
缺输入/hash冻结 Host/Session 双时钟并保留表现，controller.pause/stepOnce 保持明确暂停。
Live 恢复使用 resetNetwork(agreedCheckpoint,newerEpoch)，再明确握手/resume；
Replay 禁止 live lockstep、Record 不改写历史，无自动 checkpoint 传输或 rollback。
实现、范围、10000 tick 实际 Host/network 与 Session/collision 原生检查见
[完成阶段](../../AYDocs/DETERMINISTIC-COMPLETION-STAGE13.md)。

## Stage14: bounded rollback and predictive networking

Include AYEntity/DeterministicRollback.h and link DeterminismKernel (owned) or
Determinism (World). DeterministicRollback is the sole Session owner: bounded
actual/merged input history and checkpoints, per-type Omit/Hold/Zero prediction,
late correction/resimulation, confirmed effect drain and atomic registered-state
restoration on executing failure. Held input frames explicitly include releases;
prediction never reads future actual frames or previous guesses.

DeterministicRollbackNetwork adds fixed admitted members, inputDelay, actual-only
hashes, all-peer verified effects and trusted recoveryMember checkpoint/event-journal
chunk transfer. Standard Host opts into Live/Record recipe.rollback exclusively; input
request.tick includes delay, no sampling or Host clock advancement on resimulation.
Controller exposes confirmed/verified/epoch diagnostics, takeConfirmedEvents and
beginNetworkRecovery. Explicit pause persists. Record appends peer-verified real input; Replay uses schema2 with no live rollback owner.

Portable AYEntity_RollbackChecks exercises 10000-tick typed/RNG/topology and collision
rollback, 0/2-tick delayed network loss/reordering/duplicates, budgets, identity,
multi-chunk recovery and pending-effect preservation. Standard Host unit/process
gates cover actual integration. Full bounds/contracts and recorded platform status:
[Stage14](../../AYDocs/DETERMINISTIC-ROLLBACK-STAGE14.md).


## Stage15: confirmed rollback recording and reproduction

Link Determinism and include DeterministicRollbackReplay.h. The recording writer
observes verified retained history, never advances/restores Sim and never writes
predictions. Host Record+rollback syncs on ingress/forward/recovery, seals only the
verified prefix, and records speculative head metadata. Host Replay auto-detects
schema2; seekRollback(epoch,nextTick) and confirmed effects use the same controller.

Existing .rpl container header.version=1 (current v2 player) gains adapter schema2 event0x20006: bounded LE/FNV
profile1 initial state, real input+witness, epoch recovery snapshot+repair events,
and completion seal. Schema1 is unchanged. Recovery may skip unavailable real
input; segments expose skippedTicks, gap interior seek rejects, and snapshot gaps
are trusted recovery rather than proof of simulation. Matching checkpoint/manifest
and monotonic epoch/frontiers are validated before playback; missing seals,
corruption, unknown records and incompatible witnesses fail. Bound: 256 MiB stored
and decoded, 100000 container records, each adapter record <=8 MiB. No file rotation.

Recorder sync must run after every network mutation/before recovery; failures stop
recording. Reader seek clears pending effects and suppresses seek replay; backward
playback needs presentation reset. Tick/confirmation/verification frontiers,
rollback/replayed/last/max depth, speculative forward ticks and canonical history
bytes are observable without writing Sim. Counters reset per epoch; byte count is
not allocator heap size and network transfer buffers are separate.

AYTest_DeterministicRollbackReplay covers prefix sealing, unsealed/overwrite and
corruption rejection, exact effects, epoch seek, changed-logic first-field diagnosis
and asymmetric snapshot-gap recovery. The actual Host/AYNetwork worker runs a
10000-tick typed/RNG/topology/collision example, delayed/dropped/reordered inputs,
epoch2 recovery, confirmed recording/replay/seek and diagnostic CSV output.
See [Stage15](../../AYDocs/DETERMINISTIC-ROLLBACK-REPLAY-STAGE15.md).

The offline AYEntity_DeterministicRollbackReplayWorker accepts recording.rpl, expected.state and optional epoch/nextTick; verifies full bytes and event identity trace, prints segment/gap/first-field diagnostics, without Host/network.

## Stage16: bounded external transport scheduling

`AYEntity::DeterminismKernel` + `DeterministicTransport.h` provides
`DetTransportScheduler`: fixed remote member IDs, atomic bounded retained-set
replacement, per-peer byte-rate/burst and per-frame byte/packet attempt budgets,
unsent-first incremental egress, periodic resends, and weighted realtime/recovery
round robin. Failed sends stay pending; one pressured peer cannot block others.

After ingress/forward/recovery, call `sync(detRollbackTransportBatch(networkPackets))`
and `pumpDetTransport` with authenticated connections and monotonic milliseconds.
The AYNetwork adapter is in `DeterministicNetwork.h`; link AYNetwork explicitly.
Lockstep uses `detLockstepTransportBatch`. Keep clocks/policy outside Sim. Inspect
retained/pending bytes, retries, resends and disconnected state; explicit admitted
rebind uses `resumePeer`. Do not pump legacy unbudgeted helpers alongside it.
See [Stage16](../../AYDocs/DETERMINISTIC-TRANSPORT-STAGE16.md).

## Stage17: automatic replay file segmentation and indexed seek

Opt-in `DetRollbackReplayArchiveOptions` on writer.begin or Host recipe.rollbackArchive
(Record+rollback only) rotates complete records by byte/count budgets. Healthy finish
publishes .rpi; each physical .rpl remains independently sealed schema2. Reader files()
exposes physical metadata; segments() remains logical recovery epochs. Indexed seek
loads a bounded target file, silently replays, rejects gaps; continuous transitions
preserve exact state and emit boundary effects once. One decoded file retained, two
temporarily during replacement. Missing/corrupt files fail before use; partial output
is not automatically salvaged. Fresh single-writer output and immutable playback.
Defaults64 MiB/10000 records; hard per-file256 MiB, index16 MiB/65536 files/epochs.
See [Stage17](../../AYDocs/DETERMINISTIC-ROLLBACK-ARCHIVE-STAGE17.md).
Windows x64 MSVC Host/replay regression: Release8.70s, Debug144.49s; actual dual
process network/archive + offline playback/seek589.45s PASS. Each archive11 files,
10000 ticks/30910 exact events; Release independently reads both Debug archives.

## Stage18: offline interruption recovery and archive inspection

Link `AYEntity::Determinism`, include `DeterministicRollbackReplay.h` and call
`DetRollbackReplayArchive::inspect`, `scan` or `rebuildIndex`. Build
`AYEntity_ReplayArchiveTool` for `ayreplay inspect|scan|recover|rebuild` JSON CLI.
Scanning derives ordinal filenames from .rpi/.rpi.partial, ignoring source index
bytes. Stop at the first missing/invalid/discontinuous/budget segment; retain only
independently sealed files. Rebuild copies into a NEW directory, verifies hashes
and serialized metadata, then publishes profile2 .rpi. Original files stay intact.
Recovered prefixes remain explicitly marked by Reader.recovery() and Host
replayRecovery(), even when atEnd()/playbackCompleted(). No unsealed-tail salvage,
gap skipping, Sim execution proof, authentication or power-loss durability promise.
Inputs/output must be quiescent. Budgets and naming rules remain Stage17's.
Profile1 writer and schema2 .rpl stay unchanged; older readers reject profile2.

ArchiveChecks covers a process exiting without destructors, malformed recovery
metadata, lost/corrupt index, middle gaps, truncated tail, discontinuous splice,
exact prefix state and source preservation. Fixed checked-in Windows 128-tick,
five-file/two-epoch archive and state/event byte oracles are read by native CI
with actual AYReplay/AYIO/LZ4 adapters. Full Host/AYNetwork remains Windows-only.
See [Stage18](../../AYDocs/DETERMINISTIC-REPLAY-RECOVERY-STAGE18.md).

## Stage19: executable regressions, witness comparison and short repros

Include `DeterministicReplayRegression.h`, link `AYEntity::Determinism`. Supply an
application factory returning a fresh configured/sealed Session to `verify` or
`runnerMain`. `compare` sequentially compares logical input/state/event records,
ignoring physical segmentation and restart layout; equal recorded witnesses are
not an executable proof. Reports distinguish version/initial/input/state/event/
recovery/range/file/execution/budget failures, with exact stable field/input details.
Trusted recovery gaps and event repair remain explicit, separately counted; recovered
prefix flags persist after successful verification. No Sim is invented from a file.

`writeArtifacts` publishes report.json in a NEW directory. Locatable failures also
export independently sealed schema2 left/right_000.rpl cases (previous full
checkpoint plus one input/recovery), original manifest and available actual state.
These are constructed diagnostic cases, not full original sessions. Re-reading
checks reported record identity. Registered-state closure is required for a
one-step repro; hidden callback/external state cannot be reconstructed.

`ayreplay verify SOURCE RUNNER NEW_DIR [TIMEOUT_MS]`, `compare LEFT RIGHT NEW_DIR`
and `reproduce PACKAGE_DIR RUNNER NEW_DIR [TIMEOUT_MS]` complement Stage18 commands.
The tool uses AYPlatform owned shell-free processes, default300s/bounded output;
exit0 without a bounded protocol report is rejected. Application runners use
`--ayreplay-verify SOURCE NEW_DIR`. Global logical record and decoded artifact
budgets apply, in addition to existing file/record bounds. Inputs stay immutable.

ReplayRegressionChecks executes the fixed Windows archive, checks layout-independent
equality and input/state/event/version/gap/range distinctions, source identity,
budgets and short repros. ReplayRegressionProcesses injects an epoch2/tick96 fault
after physical transitions; an independent one-tick run must reproduce all difference
fields. Spaced/special paths, deadlines, missing runner/report and bad arguments are
tested. Native CI uploads reports, repros, actual state and exact module/root revisions
even on failure. See [Stage19](../../AYDocs/DETERMINISTIC-REGRESSION-STAGE19.md).

## Stage20: simulation fault boundaries, scene queries and sustained campaigns

`DetTickContext` access and request errors now fault the tick even when a callback
catches the exception and returns success. This includes pose, legacy/typed state,
RNG and structural/event requests. Malformed or over-budget requests are rejected
before append; existing/retired spawn IDs and unknown nonzero despawn IDs remain
ordered, prospective-boundary topology checks. A failed tick does not advance and
cannot be checkpointed or resumed until a valid explicit restore. Earlier successful
writes are not automatically undone. Event limits are shared across all producers:
1024 commands and 64 KiB of aggregate payload plus 16 bytes per command, checked
before allocation/append. Exactly 65520 payload bytes in one command fit the budget.

Include `DeterministicCollisionQuery2D.h` and capture `DetCollisionQuery2D` from a
system's context, or a matching Session checkpoint at a quiescent boundary, using
the installed `DetCollision2DConfig`. Both paths validate typed body/history data,
policy and represented XY bounds. Checkpoint capture additionally checks manifest
numeric profiles, body/history schema IDs, field types/versions and pose snapshot
validity; it does not establish application/content/code compatibility or run
arbitrary game validators. At most `config.maxBodies` active bodies, with a hard maximum of 64,
are copied. Disabled bodies are omitted. The value owns no World/context pointers:
callback (priority,id) order determines which writes it sees; recapture after
movement or restore while existing snapshots keep their original values.

Queries use world XY AABBs and offsets; Z, rotation and scale do not affect shape.
`overlap` returns all matching stable IDs in ascending order, including touching
by default (`interior=true` excludes mere face/corner touching). `raycast` takes
a displacement, not a normalized direction or an unbounded ray: its closed
segment includes both endpoints; inside/boundary starts hit at zero with zero
normal. `sweep` requires positive area, reports initial penetration and inward
face entry, and excludes outward/tangent motion and grazing corners. Targets stay
frozen, including moving obstacles. Ray/sweep hits sort by software fraction, axis
and stable ID; X wins exact entry-time ties. Reciprocal layer/mask filtering supports
an ignored stable ID and optional trigger exclusion. Invalid capture/query data or
arithmetic throws without returning a partial result. A caught pure query exception
does not itself set a Session fault; illegal context access during capture remains
sticky, and an uncaught exception follows the normal system fault path.

`AYEntity_SimulationCampaign` starts fresh owned Sessions for each seed and compares
three lanes: real-input forward execution, corrected prediction/rollback, and
random checkpoint restore/replay. It varies registration order, rational dt,
payloads, late/shuffled/duplicate ingress and restore points; structural state, RNG,
typed state and optional collision histories remain registered authority. Full
canonical checkpoints are checked at each confirmed boundary, with exact committed
event identities/payloads and repeated-drain checks for exactly-once delivery.

```
AYEntity_SimulationCampaign --seed 20261010 --cases 8 --seconds 18000 --output NEW_OUTPUT_DIRECTORY
AYEntity_SimulationCampaign --seed 189145 --cases 1 --ticks 16 --output NEW_OUTPUT_DIRECTORY
```

`--output` is required; a fresh run subdirectory is created. `--seed` accepts the
full UInt64 range (default 1), `--cases` is 1..1000000 (default 8), `--seconds` is
0..86400 (default 0), and optional `--ticks` is 16..8192 (otherwise 256..512 per
seed). Sustained mode performs new cases until both the steady-clock deadline and
minimum case count are satisfied; it finishes the current case without idle sleeps.
`--context-only` runs the directed fault/restore checks. Seed 189145/ticks16 is a
fixed harness regression: an explicit bootstrap duplicate guarantees duplicate
coverage even when the random schedule contains none; this corrects a false
coverage failure, not a product defect.

Per-case references are pruned to the confirmation/rollback horizon (16..48 history
ticks, 16 MiB canonical history budget); each completed case releases its Sessions
and reference state. `progress.json` periodically records case/tick/checkpoint/event,
restore, rollback, replay and duplicate counters. Failures include seed, lane, first
available field difference, available canonical checkpoint/input witnesses and the
complete single-seed command recipe. These witnesses localize the failure; they
are not universally standalone one-step replays of the arrival/rollback schedule.

The bounded external runner report now checks the complete schema/source/result/
identity/recovery and exit-status protocol rather than accepting matching text.
This is protocol validation for a trusted application runner, not authentication or
proof of its implementation. Root and portable builds register
`AYEntity_ContextFaultChecks`, `AYEntity_SimulationCampaign`,
`AYEntity_SimulationCampaignShortCase`, `AYEntity_CollisionQueryChecks` and
`AYEntity_RunnerReportChecks`. See [Stage20](../../AYDocs/DETERMINISTIC-SIMULATION-STAGE20.md)
for the protocol contract and actual build, sustained campaign and cross-platform
acceptance evidence; running/planned checks remain distinct from completed results.
The portable rollback gate keeps its full 100000+ advance workload in Debug; its
checked-iterator/unoptimized budget is 1500s, while Release/native CI stays 240s.

## Stage21: registered character movement and platform support

Include `<AYEntity/DeterministicCharacterController2D.h>` and link
`AYEntity::DeterminismKernel` (owned Session), `AYEntity::Determinism` (real replay
files), or `AYEntity::DeterminismHost` (standard World/Host). Install
`installDetCharacterController2D(session, config)` before adding actors or sealing.
This facade includes extended profile2 collision; use `config.collision` for all
body defaults and queries. A separate collision installation is not adopted.

A character is an enabled solid Kinematic box. Build its body with
`detCollisionBodyState2D(config.collision, body)` and its controller block with
`detCharacterState2D(config, state)` after setting `state.enabled=true`. Other
actors receive the canonical disabled controller defaults. An input callback
before `config.preSystemId` calls `detCharacterInput2D(context, id, config, input)`;
`targetSpeed` holds and `jumpPressed` is consumed once. The state getter returns
own velocity, grounded/support identity and the last carrier velocity. Mutable
authority is registered typed state, so checkpoint restore and rollback include it.

The first profile covers software `DetFloat32` acceleration/deceleration, gravity,
supported jumping, contact tolerance, downward ground adhesion and full XY moving
platform carry. Discard/Inherit selects whether release/jump keeps carrier velocity,
with explicit finite own-speed caps. It retains collision's single XY position
writer and continuous-trigger path. Axis-aligned world XY boxes remain the shape
contract; stairs, slopes, one-way platforms, crouch, buffered/coyote jump, rotated
shapes, dynamic rigid bodies and 3D are outside this profile.

`AYEntity_CharacterControllerChecks` exercises directed motion/support, invalid
configuration/restore, independent dyadic expected values, late-input rollback,
confirmed events and real replay/archive files. `AYEntity_CharacterControllerProcesses`
uses actual standard Host record/live/restore/replay/seek processes, different
presentation rates and native floating-point environment settings. The portable
executable accepts `--output EXISTING_PARENT_DIRECTORY` and creates a fresh run
subdirectory containing `summary.json`, progress and replay/hash witnesses.
See [Stage21](../../AYDocs/DETERMINISTIC-CHARACTER-CONTROLLER-STAGE21.md) for current
acceptance status and the complete public contract. Local Release passed all16
portable gates; the controller passed Debug and AddressSanitizer, and the standard
Host passed Release/Debug. Its fixed2048-tick evidence contains2049 checkpoints,
193 confirmed events,32 corrections,109 replayed ticks and8 archive files. Native
CI acceptance is recorded separately in Stage21. Test budgets preserve the same
workload: ordinary Release240s, Debug600s, AddressSanitizer900s.

## Stage22: opt-in practical character motion

Set both `config.advanced=true` and `config.collision.oneWayPlatforms=true`
before installation, actor defaults and scene queries. The explicit controller
profile2 and collision profile3 use new typed state/policy/body/history identities.
Default controller profile1 and collision profiles1/2 retain their earlier schemas,
policy defaults and runtime behavior; incompatible manifests are rejected rather
than migrated.

Advanced policy adds bounded tick-based `coyoteTicks`, `jumpBufferTicks` and
`dropThroughTicks`, plus opt-in `crouchEnabled` and software `crouchHeightScale`.
Advanced jump/drop samples are held button values with registered rising-edge
latches; submit false to release them. Legacy jump remains a consumed request.
A one-way solid Static/MovingObstacle body sets `body.oneWay=true`; it blocks
relative downward top-plane crossings with positive horizontal impact overlap.
Its side/bottom and intentional interior passages are traversable. Directional
response does not change the geometric meaning of scene queries.

Crouch changes the registered half height and offset only when the represented
shape has positive area and exactly preserves the current foot minY bits. An
unrepresentable candidate keeps the complete old stance/shape; pose is not adjusted
and no ULP search repairs it. Standing expansion checks reciprocal-mask solid boxes,
including ignored one-way geometry, and remains crouched if blocked. Collision remains the only XY pose writer. The standing shape, stance,
held samples/latches, jump windows, ignored support and release deadline all enter
the checkpoint. Drop releases only after its timer and actual geometric separation,
with a fresh stamped retired-support transition for deferred despawn.

Build `AYEntity_DetCharacterExample` in the engine superproject with
`AY_ENTITY_BUILD_DET_CHARACTER_EXAMPLE=ON` (also available under `BUILD_TESTING`).
See [the runnable example](examples/detcharacter/README.md) and
[Stage22](../../AYDocs/DETERMINISTIC-CHARACTER-CONTROLLER-STAGE22.md).
The example uses standard Host/module assembly, canonical tick input and read-only
presentation, with actual record/replay/seek output. Module gates are
`AYEntity_CharacterAdvancedChecks` and `AYEntity_CharacterAdvancedHostExample`.
Windows MSVC Release passed all17 portable gates; the advanced/legacy focused
Debug and AddressSanitizer checks and standard Host Release/Debug gates passed.
Advanced complete checkpoint/event/manifest bytes match across five builds.
The256-tick advanced scenario produced257 checkpoint rows,30 confirmed events,
10 corrections,43 replayed ticks and8 archive files; final hash14226949613902538608.
The legacy2048-tick hash remains9155686912840316892. Actual logs, preserved first
failures and reproduction scope are recorded separately in Stage22.
Native GCC/Clang x64/ARM64 acceptance is deferred to the separately provisioned
final virtual-machine environments by user instruction; no GitHub CI is requested
for this stage. Stage21 records remain historical evidence of their own revision.

## Stage23: bounded seeded advanced-character campaigns

`AYEntity_CharacterCampaignChecks` adds fresh owned Sessions per seed for held
edges, one-way/drop/crouch, coyote/buffer windows and captured-shape spawn/despawn.
Its independent integer oracle checks defined motion/controller/state/statistic
and pending-event channels; it copies the other checkpoint fields. Complete
canonical checkpoints separately compare registration/fenv, restore continuation,
actual late-input rollback and real replay/archive execution paths. It is not an
independently generated golden for every checkpoint byte.

At the hand-derived first-terminal-contact rows only, bodyY must be exactly the
incident velocity or +0: A's65th flight step in each96-tick cycle, B row64,
D rowlanding/landing+65, and E row127+coyoteTicks. This is not a generic grounded
exception; all other bodyY/bodyX and geometry/controller/action channels keep
unique expected bits. Baseline checks count `terminalBodyChecks`; representative
recording applies the same constraint. Actual terminal bodyY still participates
in exact full-checkpoint comparisons across every lane, fenv, restore and replay.
The original seeds/timing/inputs remain, and only the test oracle was corrected.

The executable accepts `--seed UINT64`, `--cases 1..1000000`, `--seconds 0..86400`,
`--ticks 192..1024`, optional `--long-boundaries`, and required `--output
EXISTING_PARENT`. It creates a fresh child directory and stops after both the
minimum case count and steady duration are satisfied. Further iterations use new
seeds; unsigned64 wrap is explicitly covered by `AYEntity_CharacterCampaignSeedEndpoints`.
Progress updates about every15s. Seed/hash/uint64 counts are JSON decimal strings,
and execution/check counts are not unique coverage counts.
`AYEntity_CharacterCampaignMaxTicks` runs seed23,2cases,1024ticks with a separate
1200s Release /2400s Debug timeout, or2400s when CMake detects address sanitizer
flags. Ordinary/SeedEndpoints budgets remain Release300s,Debug900s,ASan1200s.
The initial instrumented MaxTicks timeout at1200.06s is retained; the new2400s
submission-witness-final retry passed in1219.02s without shortening the workload.
Release/Debug/ASan each passed all three ordinary gates (CTest real totals
60.80s/1146.29s/1949.92s). The final short-evidence report verified equal normalized
summaries and six groups of corresponding representative whole-file bytes; these
compare actual execution paths, not independently derived full-byte goldens.

Only the first two cases retain real replay representatives: original inputs in
`.rpl`, and a separately executed single-member source/sequence-remapped `.rpi`.
Both submitted canonical frame streams and their distinct input digests are
retained; this is not multi-peer transport/authentication acceptance. Each archive
has at most32 segments; ongoing seeds do not accumulate complete checkpoint arrays
or recordings. Failure output preserves a complete seed recipe and available
checkpoint/input witnesses; missing legal fault-state checkpoints stay null.
Submit failures bind `inputMember`, the contribution's original tick and exact
member canonical input to the corrected Session checkpoint captured before submit.
An independently known expected checkpoint may be absent for a predicted head;
it remains null rather than borrowing a baseline or stale lane witness.

`AY_ENTITY_CHARACTER_LONG_BOUNDARIES=ON` registers the optional
`AYEntity_CharacterCampaignLongBoundaries` gate. Its seven streamed65535-window
templates specify196648 actual advances, rather than synthetic high-tick states.
Its `checkpoints` count covers only the boundary `cp` helper checks (specified196647),
not every checkpoint API read or independent full-byte golden.
Release/Debug/ASan actually passed the upper-boundary gate (CTest real
18.05s/340.81s/620.48s), each including7 templates,196648 advances and196647
boundary `cp` helper checks. Final Long evidence compared two configuration pairs
of the one seed1 sample:8 corresponding files per pair,16 complete file pairs.
Standard root Release fresh configure/build/CTest/source captures exited0;
its three campaign gates passed in61.90s total. Standard versus portable R/D/ASan
matched9 gate pairs/18 sample pairs/228 complete file pairs. The outer wrapper's
post-test path error and first evidence-reader text error remain preserved; only
evidence recovery was performed, without rerunning product tests.
The sustained seed5 lane requested32400s but is now **UserRequestedStop** after
actual OS elapsed9023.6241365s (2h30m23.624s). Its completed reported prefix is
4153cases/1585805ticks through seed4157; active4158 was interrupted. The retained
OS observer records exit-1,601 observations/no gaps and stderr0bytes. The original
wrapper remains failed/-1; the terminating PowerShell object's null ExitTime
metadata error is retained. The stop receipt is validated, not a9h/run pass;
there is no final passed campaign summary or claimed capture-pump completion.
Its startup
CMake SHA `dfb5f5345e2bdebbc40945619aabfcf2a49175d4eedc434be98a10b6d78c57a6`
remains historical; the final ASan-budget registration is
`7172956d6d24c6d8bca15d869ff648e585ec5f6a3f8e3abcf3bf5b2c4c1ef506`.
Stage23 applicable short/boundary/standard acceptance is **LocalPassed**; sustained
acceptance is **UserRequestedStop**, with **NativeDeferred to Final VM**.
Final SDK doctor actually exited0 (94 capabilities/53 modules/51 search cases/300 entry symbols, baseline28, 0 errors/0 warnings; completion observed2026-10-11T09:53:56Z). It checks catalog structure/references/symbols, not simulation or native execution. Stage23 verified commits are local AYEntity `6c02dc0ef222b143967aa4d27d7a50e2e6d6770d` and root `1f6ed71320e710ad7fb228084b17db39538b6b0c`; historical evidence source pins remain unchanged.
See
[Stage23](../../AYDocs/DETERMINISTIC-CHARACTER-CAMPAIGN-STAGE23.md) for exact CLI,
budgets, witness scope, completed evidence and remaining acceptance items.


## Stage26: explicit static step-up (Release locally passed)

The character facade can explicitly select controller3/collision4 with
`advanced=true`, `collision.oneWayPlatforms=true`, `collision.stepUp=true` and a
finite software `0 < collision.maxStepHeight <= 1000`. Profile4 allows at most59
current+transient trigger pairs; profiles1..3 retain their previous fields/bytes
and capacities. The public collision-only installer refuses this facade-owned
protocol before registration. Actor initialization leaves `state.stepped=false`.

Grounded ordinary-Static walking may try one eligible static step per tick,
selected from stable actor-ID order by `tick % candidateCount`. The sole solver
first verifies the full ordinary world path, then tries whole-world up/across/down
in dt/4,dt/2,dt/4 scratch phases. Other bodies keep advancing. Exact represented
foot/top contact, positive target X overlap and better horizontal progress are
required; low clearance, no improvement, dynamic/one-way response or trial
geometry/contact/trigger budget failure retains the entire ordinary path.
Allocation or unknown faults still propagate. No partial pose jump or second
motion owner is installed. Trigger detection uses the complete selected path.

Read the committed `DetCharacterState2D::stepped` output (typed Bool280); it is
not an authoring switch. Body Requested100/Stepped110 are internal and both zero
at committed boundaries. The profile4 history adds a ninth height policy field;
its pair bound59 keeps the128-field limit. Policy/code/schema identity is explicit;
no numeric, geometry, event, input/checkpoint envelope or replay wire is upgraded.

The standard Host executable adds `--scenario steps`, including three1/4 static
stairs, over-height and low-roof obstacles, registered actual step counters and
committed-state presentation. Default/`--scenario advanced` retains the Stage22
scene and output. Steps uses its own application/content identity. Necessary
`AYEntity_CharacterStepsChecks` and `AYEntity_CharacterStepsHostExample` gates
are **LocalPassed** in Release (Steps0.15571s, Host128ticks0.393514s; actor10 actual3 steps, other actors0, final centerY3/2). Old controller/advanced gates also passed with legacy hash9155686912840316892 unchanged. Final native VM remains deferred. No new long campaign or
cross-configuration matrix is requested. See
[Stage26](../../AYDocs/DETERMINISTIC-CHARACTER-STEP-UP-STAGE26.md) and
[example instructions](examples/detcharacter/README.md).

Actual Stage26 source/outputs and the preserved localized-LastTest evidence-reader failure are recorded in [acceptance.json](../../out/detstep26-20261011/acceptance.json) and Stage26. The reader correction did not rerun products; before/after12 source/example SHA checks and SDK doctor exit0 passed. Full CP compares actual production paths, not an independent full-byte golden. No new default Host, Debug/ASan or native acceptance is inferred.

## Stage27–29: prescribed paths, safe motion policy and static slopes

`DeterministicPathMotion2D.h` supplies a registered velocity driver for solid
MovingObstacle bodies. It supports Once, Loop (including the closing leg),
PingPong, integer-tick dwell, pause and disable/resume. Points are relative to
each bound actor's origin. Path pre precedes character pre, and path post follows
character post. Only collision writes pose; a path opportunity progresses only
when the requested represented target was reached. All cursor/control/result
state and immutable points are checkpoint authority. Restart resets phase only
at the origin and never teleports. Unrepresentable binary32 targets fault instead
of accumulating endpoint drift; author dyadic paths for straightforward exact motion.

Opt-in `collision.motionPolicy=StopPrescribedOnConflict` selects collision5 and
controller4. It stops prescribed movers that encounter a solid static/prescribed
obstacle or cause a directly proved opposed-face pinch, then recomputes the
entire world once from the original tick start. Other independent movers continue.
Characters' continuous carry from stopped supports is removed during the retry;
jump/leave inheritance is retained. Typed Clear/Blocked/CrushPrevented results,
stable partner, axis and fresh committed-tick stamp are recorded. Ordinary
iteration exhaustion, invalid geometry and a failed retry remain execution faults.
This initial policy proves direct AABB face constraints, not arbitrary chains.

Opt-in `collision.staticSlopes=true` selects collision6/controller5. A body shape
`SlopeRisesRight` or `SlopeRisesLeft` fills its bounds with a solid triangular
wedge, allowed only for ordinary solid Static bodies. Real triangle narrowphase
handles overlap, continuous sweep and ray query, including the base/high side.
Characters walk uphill/downhill within `maxSlopeRisePerRun`, jump off normally,
and expose a committed nonunit `groundNormal`. Support height uses the highest
foot corner. Crouch clearance uses wedge interior; step trials exclude slope shapes.
The solver splits the shared world path at the slope's high end: uphill motion
becomes horizontal on its top segment, and reverse motion resumes the descending
tangent after crossing it. This prevents a grounded character launching across
an adjacent flat surface while preserving jumps and the single pose owner.
The wedge's own highest-foot top segment also retains support without an adjacent
floor. Intent, support and restore use its local horizontal tangent before the
foot crosses onto the diagonal; both legal terminal velocities are accepted at
the exact join. The registered normal remains the wedge's raw shape normal.
No capsule, rotation, dynamic mass/friction or 3D motion is provided.

Old default profiles/state bytes remain unchanged. New manifest identities are
explicit; numeric/AABB geometry/event/checkpoint/input/replay envelopes retain
their existing profiles. Collision5 allows at most59 trigger pairs; collision6
at most58. Necessary Release gates are LocalPassed: Path0.101359s,
MotionPolicy0.0528883s and CharacterSlopes0.0397255s. Current old controller,
advanced and step gates also passed, retaining legacy hash9155686912840316892.
The final real standard Host `motion` integration passed in0.279483s with128ticks;
actual slope-walk, carry, blocked-path and crush-prevented counters were each128.
The path16-tick and motion8-tick fixtures include actual late rollback, confirmed
effects, sealed replay and3 short seeks each; slope checks include both high-end
flat transitions and mid-slope complete checkpoint restore. Native acceptance
remains deferred to the user's VM environment. No new campaign/long-stability
matrix is added. Actual input pins, logs and retained failures are recorded in
[acceptance.json](../../out/detmotion27-29-20261011/acceptance.json). See
[Stage27–29](../../AYDocs/DETERMINISTIC-MOTION-AND-SLOPES-STAGE27-29.md).

### Stages30–32: moving terrain and loadable levels

Explicit `collision.movingSlopes=true` requires staticSlopes/extended and selects
collision7/controller6; `continuousTerrain=true` additionally selects8/7. Fixed
ordinary MovingObstacle wedges use platform-relative sweep, tangent, carry and
surface transitions. True triangle/triangle prescribed motion uses continuous SAT.
Only proved direct opposed flat features receive the single Stop retry; arbitrary
diagonal crushing/chains still fault. Pair capacities are57/56. Rotation, mass and
one-way/trigger/Kinematic wedges are outside this surface.

Grounded walkable terrain traversal preserves bounded actual own X when changing
support, including after a wall clips carrier motion. Crest, valley and exact low
endpoint handoff share the sole whole-world solver and real continuous trace;
terminal zero-overlap low endpoints keep the valid old floor tangent. Jump/gap
motion does not implicitly attach. Old default manifests/wire remain unchanged.
On an actual walkable inclined landing, terrain8 also settles bounded own X and
carry together when own intent descends and no jump/drop is active.

Include `AYEntity/DeterministicLevel2D.h` and load a bounded Experimental `.dlt`
resource before configuration. Use `detLevelSessionConfig(level)` and
`configureDetLevel2D` in a fresh Session or standard Host recipe, then register
application input separately. Exact software bits, stable IDs, collision layers,
characters and named paths are validated through the actual installers. Full
canonical content enters typed defaults and a pure validator, not only a hash.
No runtime file owner, mutable cache, general Scene import or automatic packaging
is introduced. Registration failure still requires discarding the partial Session.
Examples and actual necessary acceptance are tracked in
[Stage30–32](../../AYDocs/DETERMINISTIC-TERRAIN-STAGE30-32.md).
Necessary Windows Release acceptance passed:8 directed/affected regression targets
and one standard Host Live128 target. SDK doctor passed with no errors/warnings.
Native platform acceptance remains deferred to the user's final VM environment.

## Registered level actions and trigger rules (Stage33–35)

Include `DeterministicActions2D.h`; install its fixed target table before actors.
Execute collision participation and platform controls before path/character pre.
Mask zero preserves body mode and path bindings. Occupied solid enabling and
restart away from origin return registered denial results without target writes.
Callers explicitly retry; no hidden action queue or automatic retry is present.
The immutable table/full path policy and mutable global results bind the Session.

After Actions installation, `installDetInteraction2D` registers up to16 Enter/Exit
rules with once, cooldown and delay. Each has at most one pending action.
Complete authored rules enter manifest defaults; counts, timestamps, pending
jobs and results enter typed globals and pure restore validation. Due jobs run
by rule ID, followed by canonical events and matching rule ID. Once is consumed
when scheduled, including later denials. Timing uses logical ticks only.

`AYEntity_DetInteractionExample` uses actual console input outside Sim, standard
Host Scene lifecycle, a `.dlt` door/platform/collectible scene, Live save/load and
existing Record/Replay/Seek. Build with testing or
`AY_ENTITY_BUILD_DET_INTERACTION_EXAMPLE=ON`. Command usage and the short Host
gate are in [the example](examples/detinteraction/README.md); necessary local
acceptance and final native deferral are recorded in
[Stage33–35](../../AYDocs/DETERMINISTIC-INTERACTION-STAGE33-35.md).

## Authored interactions, progress and persistent content (Stage36–40)

Experimental `.dlt` format2 adds bounded action targets and Enter/Exit rules to
the level file. `configureDetLevel2D` installs these internally; acquire their
resolved policies through `detLevelActionsConfig2D` and
`detLevelInteractionConfig2D`, rather than installing a second copy. Format1's
canonical bytes and identity remain unchanged. All authored fields and resolved
path policies bind the manifest. The optional `beforeActors` registration hook
runs only on the destination, after scratch preflight and internal installers,
before level identity and actors; false/throw requires discarding that Session.

`DeterministicProgress2D.h` provides16 inclusive UInt64 counters,8 derived AND
objectives (up to8 thresholds each), and16 collectible permissions. Install it
after matching Actions and before actors. Explicit operations precede path and
character pre. A claim requires an actual canonical Enter and an Applied trigger
disable; normal refusal preserves both value and claim state. Objectives can
reverse after subtraction. Collected live triggers must retain mask0; full policy,
values, claims and result stamps are checkpoint authority. No automatic consumer
or retry queue is installed by this facade.

`DeterministicSave.h` reads/writes bounded `.dsv` files outside Sim. A16-byte
AYDSAVE1/version1/length envelope holds the unchanged complete checkpoint payload
(at most8MiB). Writes exclusively create fresh files. Decode is output-atomic;
application compatibility still requires Session restore. Host callers use
`readDetSave` then controller restore/resume at a permitted Live frame boundary.
An I/O failure may leave a partial new file; replacement, migration and crash
durability are not provided.

The `AYEntity_DetLevelTool` target produces `aydetlevel validate/inspect/canonicalize`;
see [the authoring tool](tools/detlevel/README.md). The new
`AYEntity_DetContentExample` uses an actual moving platform, pause/resume zones,
two collectibles, a door, a conditional finish and persistent save/load. Build
with testing or `AY_ENTITY_BUILD_DET_CONTENT_EXAMPLE=ON`; commands and integration
scope are in [the content example](examples/detcontent/README.md).

Necessary Windows Release checks are **LocalPassed**: LevelInteraction2D,
Progress2D, Save and LevelTool; the unchanged-format Level2D regression and
affected Interaction Host also pass. The content Host gate compares complete
final checkpoints across six short Live/load/Record/Replay/Seek/stdin processes,
with actual feature observations and saved64-tick continuation. Native acceptance
remains deferred to the user's final VM environment. Actual evidence and the
ongoing ten-hour task are tracked in
[the execution plan](../../AYDocs/DETERMINISTIC-CONTENT-TEN-HOUR-20261011.md).

## Automatic collection and objective actions (Stage41)

`DeterministicProgressFlow2D.h` installs a bounded consumer after matching Actions
and Progress, before actors. It processes only the actual collision producer/type
Enter stream, in canonical event order then claim ID; optional current objectives
gate each claim. Earlier successful claims can unlock later claims in the same
tick. A closed gate, normal denial or Stay does not retry without a new Enter.
All other application Progress writes must precede Flow at collision.priority.

Up to8 objective rules execute fixed Actions on false-to-true edges, after claims
and in rule ID order. Initial true fires at tick0; once consumes an actual attempt,
including denial. Non-once requires a later false→true edge. Registered counts,
results, edge memory and complete policy restore with the Session. The policy
limit12288 bytes can require13 chunks plus two mutable schemas, within the total
Session budget. A later write changing a routed objective's truth faults at the
boundary. Level2 can install Progress/Flow through its beforeActors hook; formats3/4
also author and internally install complete Progress/Flow tables.

`AYEntity_ProgressFlow2DChecks` is LocalPassed in Windows Release (0.264903s),
including actual Enter/gates/denials, objective reversal,8-tick complete checkpoint
continuation, late rollback/confirmed drain and real sealed replay/two seeks.
See [the ongoing execution record](../../AYDocs/DETERMINISTIC-CONTENT-TEN-HOUR-20261011.md).
## Deterministic authored progress, relocation and cooked memory (Stages42/44/45)

`AYDLEVEL 3` adds sorted counters, AND objectives, actor-specific collectible claims,
explicit gated claim bindings and objective rising-edge actions. The Level facade
installs matching Actions/Progress/Flow internally before actors; use
`detLevelProgressConfig2D` and `detLevelProgressFlowConfig2D` to read that policy,
and avoid duplicate installers. Pure counters permit zero authorized Actions targets.
Version1/2 canonical bytes and hashes remain unchanged. Individual maxima still must
fit the combined8KiB semantic identity and actual64-schema/session budgets. Start from
[the content level](examples/detcontent/content.dlt) and use
[aydetlevel](tools/detlevel/README.md) to validate/inspect/canonicalize it. `aydetlevel float`
provides the independent AYMath integer decimal-to-bits authoring boundary.

Manual assembly can opt into `installDetCharacterRespawn2D` before actors/seal,
with advanced/extended/one-way/characterRespawn explicitly enabled,1..8 fixed
actor-specific spawns and matching body/controller factories. This is the sole
controller8/collision9 pipeline. An earlier canonical input system requests a spawn;
collision first validates normal whole-world motion, then resolves standing destination
clearance in actor-ID order at tick end. An occupied destination keeps normal movement.
Accepted relocation resets movement/support/windows, preserves consumed held samples,
and discards interpolation history. It retains real normal-motion/endpoint triggers,
without a teleport chord. Results are copied typed state and restore with checkpoints.
Level1/2/3 and Actions profile1 do not authorize this opt-in; direct facade assembly
is required. See `DeterministicCharacterRespawn2D.h` for explicit timing and limits.

`encodeDetLevelCooked2D` / `decodeDetLevelCooked2D` provide a pure-memory bounded
`AYDLC001` envelope for validated canonical Level text. Decoding checks exact version,
length/EOF, payload checksum, actual Level preflight, byte-identical canonical text
and semantic identity; failure leaves output unchanged. FNV values do not authenticate
content. This codec has no file/Resource owner or simulation tick ownership; the optional
resource/import/Pak adapter is provided by the separate opt-in target below.

`AY_ENTITY_LEVEL_RESOURCES=ON` adds `AYEntity::DeterministicLevelResources` with
explicit startup loader registration and immutable validated `.dlc` snapshots.
Authoring builds convert `.dlt` while preserving logical hierarchy; ProjectBuild
checks complete source/cache identity, and CookShip validates final levels before
publishing. Resource Core retains its metadata-only dependency boundary. Acquire
content before configuring a new Session/Host; reload never changes running authority.
See [the runnable resource example](examples/detlevel-resource/README.md).
The necessary Windows Release gate is LocalPassed: three resource lanes,8ticks each,
27 complete checkpoint comparisons,14 rejection checks and302-character cache
staging/replacement/reuse. Deep-path copy has no separate coverage claim. Final
native VM acceptance remains deferred.

## Stage46: authored checkpoint/death/manual content

`AYDLEVEL 4` retains all existing content and adds explicit Respawn ownership and
actor-specific spawn/default/region tables. Configure installs the sole fresh
controller8/collision9/Respawn1 and RespawnFlow1. Read `detLevelRespawnConfig2D` /
`detLevelRespawnFlowConfig2D`; do not install a second motion facade. Input precedes
routing, which precedes ProgressFlow, Interaction and all path/character pre.
Original1/2/3 bytes/hash remain unchanged. Actions2 binds complete new ownership;
false retains Actions1. Progress/Flow/Path algorithm and profile remain1. The Path
facade exposes its unchanged canonical state descriptor for pure typed observers.

Routes select all real checkpoint Enter events before death/manual arbitration.
Death wins manual; each actor attempts once per tick, and ordinary denial/Stay has
no hidden retry. Accepted endpoint Enter is real and can select a checkpoint again.
Copied registered state distinguishes requests from actual collision outcomes.
Start from [the standard Host example](examples/detcheckpoint/README.md). Necessary
core/Level4 and six128-tick Host paths passed, including keys/exit, collision door,
real platform carrying, death and manual relocation with save/load/replay/seek/stdin.
This stage adds no implicit damage/health system or final native acceptance.

## Stage47: dedicated deterministic vitality

Install matching shared Actions, a separate HP-only Progress and `installDetVitality2D`
before actors/seal. Fixed Hurt/Heal direct permissions and real Enter regions own
bounded UInt64 HP. Nonlethal Hurt grants an exact tick protection window; healing
does not revive HP0 or refresh protection. Direct canonical input precedes Vitality6,
then every path/character pre. Read copied actor/results, never write HP Progress
outside Vitality. Normal denials preserve HP; pure checkpoint closure binds exact
owned writes, mirrors, deadlines, counts and permissions. No body/respawn writer is
added. The necessary Release core gate passed0.344201s with8ticks and actual short
restore/rollback/replay/seek. See `DeterministicVitality2D.h`; native VM is deferred.


## Stage48: explicit source, accepted HP recovery and authored life

Prefer Level5 `configureDetLevel2D` and the matching vitality getters. It installs
shared Actions, HP-only Progress/Vitality1 or explicit Vitality2 with full Flow2.
Manual recovery is separately authored; canonical input4 precedes Vitality6/Flow7,
then motion, recovery post62. External death authority checks actual callback ID,
not command.source. Only fresh matching Queued+Accepted restores fixed HP; occupied,
stale/manualfalse/persistent HP0 cannot restore/retry. Equal HP creates no Progress
write. Read copied HP/normal-result/recovery facts. Collision remains sole pose owner.
Old profiles1 and Level1..4 retain literal identity. Joint schema/policy budgets apply.
See `examples/detvitality`: actual Hurt/Heal, checkpoint, lethal floor, manual recovery,
collection, door and carrying platform. Short core/Level/tool and six real Host
process paths passed; only necessary local gates, native VM deferred.


## Stage49: real periodic regions and Level6

Prefer Level6 authored acquisition. The vitality header adds a periodic boolean
after recovery and before counts; each region line appends periodTicks. Explicit
periodicRegions selects Vitality3, false retains exact1/2 and requires period0.
Period0 remains Enter-only;1..65535 starts an episode/attempt on real Enter and
allows only future real Stay at processingTick>=nextEligible. All callbacks consume
previous collision events in canonical order, then region ID. At most one attempt
per binding/tick, but later matching Exit/reentry transitions still run. Exit clears
the clock; reentry starts afresh; transient Enter+Exit has one attempt and ends inactive.
Normal protection/dead/budget/inactive/Unchanged denials consume the interval.
No independent poll, overlap sampling, fake Enter, wall time or implicit death retry.

Separate region224 is<=114fields for16regions, complete policy<=8192/nine chunks.
Copied `detVitalityRegionState2D` exposes attempts/lastAttempt/nextEligible/episode/
transition/phase. Pure closure checks exact registered counts/sums/stamps/deadlines
and actor-attempt witness. Delivered episode frontier precedes current motion/respawn,
so it is not falsely tied to final collision history. Retired references clear in the
next routing callback; same-boundary retirement remains valid. Actor copies defaults.
Level1..5 retain complete bytes/hash, including actual fixed Level5 fixture identity.

Necessary local Release periodic core0.331411s, Level6 0.324838s passed; selected
hand interval/protection/budget/Exit/reentry/transient/recovery behavior is distinct
from full CP actual restore/late rollback/rpl/seek equality. Native final VM deferred.
Actual standard Host example includes Hurt2 period3, Heal2 period2, accepted death
placement outside hazard and equalHP Manual recovery; six actual file/input paths
agree. Full source/failure/commit receipt is acceptance-49.json.

## Stage50: explicit moving trigger paths and Level7

`DetPathMotion2DConfig::movingTrigger=false` keeps Path1 identity; true selects2
for bound non-one-way AABB MovingObstacle triggers. Path still writes velocity only,
collision is the sole pose owner and emits actual swept Enter/Stay/Exit. Triggers
have no solid response or support/carry. Mask0 suppresses participation independently
of enabled/paused phase controls. Exact sampled XY reachability and origin-only
restart remain required. Use public `detPathMotionPolicySchema2D` and mutable factory
instead of guessing Path1 fields/version. Mutable path state is actor-only; only
immutable policy has a global alongside actor copies.

Actions3 is selected only by an authorized target's explicit Path2. It binds actual
version/role and full nested collision/path bytes, including mixed Path1 targets.
Ordinary trigger collision gates do not select3; old Actions1/2 literals stay exact.
Progress1, ProgressFlow1 and Vitality1/2/3 keep their algorithms/profiles while matching
the actual nested Path/Actions descriptors, priorities, validator versions and logic.
Internal selector headers are compatibility metadata, not additional gameplay APIs.

AYDLEVEL7 adds `path <id> <playback> <moving-trigger> <waypoint-count>`;
old1..6 reject nondefault roles and preserve complete writer/canonical/hash identity.
Use normal Level acquisition/getters; author via `aydetlevel` and inspect explicit
path flags. Full combined schema/chunk/semantic budgets still apply.
The [moving-region example](examples/detmovingregion/README.md) uses real standard
Host input→Actions/HP/Flow→Path/Character→Collision→Present, not a second tick owner.

Initial fresh Release six affected core gates passed1.95s. Path2's actual12ticks,
13fullCP/9events/11rollbacks/21replayed/one sealed rpl and seeks0/3/6 completed;
hash5258465926788394780 is diagnostic, full bytes compare real execution paths.
The subsequent combined Host/tool gate failed in Flow/Vitality because the new
actor-only mutable Path descriptor was incorrectly required to have a global.
The narrow fix retains all layout/version checks and every original global check.
Failure materials remain at stage50-host2. Corrected actual Host/CLI gates passed in stage50-host3 (two gates, CTest3.19s), with unchanged source pins. The six16tick Host paths agree in full state/trace/load/seek/stdin saves; actual four actions independently pause/resume and disable/enable the moving hazard. Final necessary local gates passed: Level7 authoring0.753711s (3 cases/12 rejects/57 complete CP comparisons), old Level6/Flow/Vitality/Regions3, and Resource1.08742s/Cooked0.10974s. Exact Stage50 receipts preserve source fixes and failures; native final VM deferred.
Native Deferred to Final VM; no new long-run, compiler matrix or remote CI claim.


## Stage51: finite deterministic melee attacks

Use `AYEntity/DeterministicAttack2D.h` with `AYEntity::DeterminismKernel` (or the
standard Host target). Separately install matching collision/Actions, dedicated HP
Progress and Vitality, then `installDetAttack2D` before actors/seal. The Attack config
copies that complete Vitality policy; dependencies are not installed twice. Default
canonical input4 calls `detSetAttackInput2D` before Attack5/Vitals6 and motion pre.
Held samples need explicit false release; an accepted start locks facing and gives
finite warmup/active/recovery windows. Copied state/result and public descriptor
factories expose registered phase, consumed target bits and historical HP attempts.

Only strict frozen-geometry overlap against authored Hurt targets causes an attempt.
Initial-alive snapshots permit same-tick trades; normal denials consume the target
bit. A new accepted recovery generation or dead/retired owner cancels an open attack
on its next callback. There is no repeat/buffer, weapon sweep, wall occlusion or
second pose writer. Existing ordinary Combat/Ability APIs have separate contracts.

The short `AYEntity_Attack2DChecks` target is registered in standard and portable
builds. Necessary MSVC Release passed1/1 (JUnit3.51278s):16tick/17CP,56 selected
hand rows,10 restore/11 config negatives,16 confirmed effects,1 rollback/1 replayed
tick and one sealed rpl/two seeks. Standard production Kernel compiled26.219s; no
Host/Debug/ASan/native run. Native final VM deferred. See
[design](design.md) and the [SDK contract](../../AYDocs/sdk/contracts/2d.md#2dsimulationdeterministic-attack)
for ordering, capacity, rollback/restore closure and the precise evidence boundary.
No new level-file grammar or interactive attack Host is included in this facade.
The unchanged-source receipt is stage51core-diagnostic2/result.json. First existing
Replay dependency build exit1 without diagnosis remains in stage51core-first; the
same-source retry passed without changing production behavior.


## Stage52: authored Level8 attacks and playable standard Host

`AYDLEVEL 8` appends attacks/attack/attacktarget after vitality, preserving all earlier
1..7 writer/semantic/hash branches and rejecting new nondefault attack authority in
old versions. Acquire through load/decode, detLevelSessionConfig and configure;
`detLevelAttackConfig2D` resolves the exact already-installed Attack/Vitality config.
The Level installer owns these facades before application hook/actors; do not install
another Attack. Source bits/windows/literal Hurt permissions/amounts bind full content
identity. Combined existing text/semantic/schema budgets still apply.

Enable `AY_ENTITY_BUILD_DET_ATTACK_EXAMPLE=ON` (default OFF) for standard target
`AYEntity_DetAttackExample`. The [attack content example](examples/detattack/README.md)
uses real EngineModuleRuntime/Entity runtime/Host with app52/input5,24tick script and
canonical8-byte input. It demonstrates locked facing, short/long preparation, sampled
jump miss and equal-HP accepted recovery cancellation, with Live-only persistent
save/load and sealed Record/Replay/seek. Every effect remains registered; presentation
reads committed values. aydetlevel inspect exposes complete Attack authoring tables.

Necessary local Release Level8 passed0.463677s (3cases/29rejects/16handRows/29fullCP
comparisons), old7/Cooked compatibility passed0.931608/0.180456s. Actual six24tick
standard Host passed6.14051s and full CLI1.06216s, with complete state/trace/save4/
load/seek suffix equality. Existing Resource compatibility passed0.975352s (27CP/
14rejects); this does not claim separate Pak execution of the new attack content.
First unchanged Actions TU build exit1 without diagnosis remains in stage52-host1;
same-source retry passed without a runtime fix. Receipts stage52-levels1/host2. No new
long campaign/matrix/CI/native result; final native VM deferred. See design for closure.

## Stage53: fixed-target deterministic character decisions

Prefer `AYEntity/DeterministicBrain2D.h` with `AYEntity::DeterminismKernel`.
Install matching Character and Attack/HP dependencies separately, then Brain before
actors/seal; do not reinstall them. Respawn-enabled acquisition uses the matching
existing recovery/Character pipeline. Default Brain3 -> player input4 -> Attack5 ->
Vitals6 -> optional Flow7 -> motion pre uses the same collision priority. Player
input must not replace Brain-owned inputs. Owners remain enabled while present;
retirement is allowed. A primary target is fixed and Attack-whitelisted; sensing
does not narrow the Attack facade’s full authored damage whitelist.

Inclusive center sensing gives signed chase input or held attack requests; absent/
dead targets patrol. Absolute patrol bounds change direction, not physical position.
Busy follows the previous Attack phase, sends false held and zero desired speed;
dead owners also request zero and retired owners release without missing Character
reads. Zero intent is not instant velocity freeze: deceleration, gravity, carrying
and sole collision motion still apply. No navigation, wall/ledge avoidance, line of
sight, auto target selection, timer/RNG, body/HP/pose writer or native AI adoption.

`detBrainState2D` copies registered decisions and pre-input/HP/motion observations.
At CP.nextTickN these were observed on tickN-1; previous Attack metadata does not
equal the later current Attack state. Request counts are not attacks/hits. Full
Character/Attack/table literal identity, mutable globals/default actor copies and
pure input/observation closure support restore/rollback/replay. Only actual fresh
Accepted respawn permits target speed clearing. Max8 owners,121 mutable fields,
18KiB/19 policy chunks still share joint64-schema and aggregate Session budgets.

Read `detCharacterStateSchema2D` and `detCharacterPolicySchema2D` for actual selected
descriptors; these factories install nothing and preserve old defaults and profiles.
Necessary local MSVC Release four gates passed: Brain3.6326s, Controller6.04419s,
Advanced4.27686s and Respawn0.21503s; standard production Kernel compiled29.344s.
Brain actual16tick/17CP includes27selected hand rows,107advance calls,81CP comparisons,
11restore/10config negatives,16confirmed events,1rollback/1replayed tick,1physical
sealed rpl/2seeks. Hand behavior and complete actual execution equality have different
scopes; no independent whole-CP golden or history authentication is claimed. Initial
new fixture C2665 was corrected only to an existing public descriptor typed read,
keeping the7/4 oracle; later mixed Windows-path reader rejection after test exit0
was repaired only in OUT evidence collection. Both failures remain preserved; no
production behavior fix or repeated successful gates. Receipts stage53-actual-freeze
and stage53core-evidence-completion bind531source pins/9owned files/4executables.
The matching Brain respawn-recovery combination was not separately directed;
the ordinary Respawn compatibility pass does not establish that new combination.
No new Level Brain grammar/Host content/Debug/ASan/native/backend matrix/long-run
result; final native VM deferred.
See [design](design.md) and [SDK contract](../../AYDocs/sdk/contracts/2d.md#2dsimulationdeterministic-brain).

## Stage54: authored Level9 enemies in the standard Host

`AYDLEVEL 9` appends complete sorted `brains`/`brain` rows after attacks. Use normal
Level load/decode, session config and configure; resolve `detLevelBrainConfig2D`
for copied decisions from the exact prepared Character/Attack/HP authority. Level
installs Brain before the application hook/actors; never install it twice. Empty
rows install no Brain and the getter throws. Formats1..8 reject nondefault rows
and keep their old writer/default/semantic/hash branches; capacities remain joint.

The [Brain content example](examples/detbrain/README.md) uses the real standard
EngineModuleRuntime/Entity runtime/Host Scene. Enable default-OFF
`AY_ENTITY_BUILD_DET_BRAIN_EXAMPLE` for target `AYEntity_DetBrainExample`.
App54/inputVersion6, logic80 and input system4 map six Hero-only canonical bytes;
Enemy belongs to Brain3. There is no Manual or Enemy-held command mapping.
Busy/Dead zero intent retains actual controller braking/gravity/carrier motion.
The fixed target controls sensing, not the full Attack damage whitelist. No
navigation, line of sight or second pose/HP/tick owner is introduced.

The default64tick scenario and warmup save38 compare Live/fresh load/Record/Replay/
seek/stdin complete checkpoints and trace suffixes when actually executed. Live
file I/O stays outside Sim; Record cannot rewind and Replay captures no input.
Necessary local Release Level9 passed1.24213s (16ticks/17CP/16selected hand rows/
13fullCP restore comparisons/28rejects), including real text/cooked roundtrip.
Old8 .476128s/old7 .740938s/Cooked .18782s, standard six-path Host11.3003s and
CLI1.19754s passed. Actual stdin68rows includes4revisited ticks and suffix42 of26;
load/seek26rows, Replay0input calls. HeroHP6/hurts1/attacks3/attempts3 and
EnemyHP0/dead/hurts3/attacks3/attempt1/cancel1,64decisions/3pulses/Dead actually
passed selected hand checks. Existing Resource .986975s is old-sample compatibility,
not a separate new9 Enemy Pak run. SourcesUnchanged receipts stage54-levels1/host1;
no production behavior fix. Stage53 core evidence stays separate, and this scene
does not cover its unmatched recovery combination. Final native VM deferred;
no new matrix or long campaign, independent full-CP golden or authentication.

## Stage55: registered character input control and finite dash windows

Use `DeterministicCharacterControl2D.h` over a separately installed exact advanced
Character pipeline (`advanced=true`), or full explicit Respawn when its flag is
enabled. Install Control before actors/seal. It does not acquire a second collision
writer or install dependencies twice. One to eight sorted enabled ordinary solid
Kinematic owners start with zero Character input and remain enabled while present;
retirement is supported. Caller keeps Brain/Control owners disjoint and all other
callbacks from overwriting these Character inputs. Failed assembly is discarded.

An actual earlier registered input callback calls `detCharacterControlInput2D`;
default Control15 precedes Character pre20 at collision priority. Persistent full
held samples need explicit false release: missing input holds dash/cancel/jump/drop.
A consumed denied edge is never buffered/repeated. Start T locks facing target
speed on [T,T+duration), ready=T+duration+cooldown; cancel retains the deadline.
groundOnly samples previous committed ground. Dash still uses Character's real
acceleration/deceleration, gravity, carrier, jump and wall response; no exact
distance, instant speed, invulnerability or direct pose/body-velocity/HP writes.
HP death requires explicit application cancel.

Previous-tick actual Accepted relocation cancels before the next Character input
and denies a fresh edge; Queued/denied does not. A current Accepted post may reset
committed speed/crouch after the captured decision; pure closure permits only that
actual result/stamp and next Control observes it. Copied context/CP state describes
the last Control decision, not later support. At nextTick N phase/output refer to
N-1, including the last Active emitted tick at N==endTick. Retirement cancels once
and holds the original ready deadline; counters do not prove physical distance.

Profile1 defaults input304/state305/policy306+/system15/validator&logic79. Actual
factories expose120 input/121 timeline fields at8owners and full nested literal
policy<=8192bytes/9chunks. All real64-schema/Session limits still apply. Pure
restore checks identity/defaults/counters/stamps/actual emitted input atomically;
it does not authenticate history or imply ordinary Combat/Ability state closure.

Necessary local MSVC Release single `AYEntity_CharacterControl2DChecks` passed
JUnit0.423906s; standard production Kernel compiled14.062s (not a Host runtime).
Actual16ticks/17fullCP,41selected independent hand rows/121explicit advances/
85CP comparisons,10negative restores/10negative configs,16confirmed events,
1rollback/1replayed tick,1physical sealed .rpl/2seeks. Selected finite edges/ground,
actual acceleration/wall/jump/retirement/Accepted-vs-Occupied checks are separate
from reverse-order/restore/late-Hold correction/replay-seek whole-CP execution
equality; the latter is not an independent whole-CP golden or authentication.
Receipts `stage55core-integration-fix/result.json` / `stage55-actual-freeze.json`
bind540 unchanged candidate sources and unchanged executable. First portable
build omitted Control.cpp and failed eight unresolved LNK2019 symbols before any
gate ran. One CMake source line and nearby State Doxygen brief fixed integration;
production behavior/assertions unchanged, first failure preserved. No old gate
repeated or new Host/Level content, Debug/ASan/backend matrix/long/native result;
final native VM deferred. See the [SDK contract](../../AYDocs/sdk/contracts/2d.md#2dsimulationdeterministic-character-control).

## Stage56: authored Level10 finite Character Control

Format10 adds `controls/control` after brains. Use normal Level load/configure and
`detLevelCharacterControlConfig2D` for the exact prepared advanced Character and
optional complete Respawn. Level installs Control after Brain, before the
destination hook/identity/actors; never duplicate it. Empty rows install none and
getter throws. Old1..9 retain literal/writer/default/hash branches and reject
nonempty Control rows. Authoring rejects Brain/Control owner overlap; actual
factory-selected namespaces share the real Session budgets, not extra capacity.

The optional default-OFF `AY_ENTITY_BUILD_DET_CONTROL_EXAMPLE` builds
`AYEntity_DetControlExample`, a real EngineModuleRuntime/standard Host/Scene using
[Level10 content](examples/detcontrol/content.dlt). Hero input is Control-owned;
Enemy remains Brain-owned. App56/inputVersion7/eight-byte Hero samples/input4/
logic81 map speed+2,jump,crouch,drop,attackHeld,facing,dashHeld,cancel. No Manual
or Enemy-held input. Control15 precedes Character pre; actual gravity/braking/
carrier/wall/jump remain. Death cancellation is explicit application input.

Necessary local MSVC Release7 unique cases passed: LevelCharacterControl10Checks
0.223617s, old9 1.17518s/old7 .654778s/Cooked .116577s, standard ControlHost
13.2032s, CLI1.37562s and existing Resource1.00207s. New Level10 actual16ticks/
17CP,16selected hand rows/46advances/33CP comparisons/25authoring rejects,
real file/text/cooked roundtrip and active restore. Optional Respawn checks initial
seal/getter/restore binding only, not a new Accepted-motion exercise.
Actual64tick active-save4 six-path full state/trace/suffix/save equality passed;
load/seek60rows, stdin68rows with suffix8 of60, Replay0input calls. Control has
4starts/11requests/7denials/1cancel, ends Active/ready73; HeroHP96/hurts2/attacks2/
attempts2 and EnemyHP94/alive/hurts2/attacks2/attempts2/cancel0/64decisions2pulses.
FinalHash5877846431418660777/content15814538279326814389 are diagnostics, not
independent state goldens or authentication. Existing Resource27CP/14negatives
uses old samples, not new10 combat Pak execution. Selected hand fields and whole
actual CP execution equality are separate scopes; final native VM deferred.
No new matrix/Debug/ASan/long/native run or numeric/Character/Collision/checkpoint/
save/replay wire change is claimed; explicit Level profile10 is separate.

Receipts stage56-levels2/levels3/host2 and stage56-actual-freeze.json bind
per-run unchanged sources. levels2 remains overall Failed, with three genuine old
case passes retained without rerun; only new10 was retried. First new fixture
C2677/C2661 came from quoted-helper ADL (renamed jsonQuote); later CRLF source1007
was wrongly equated with canonical LF980, corrected using independent previously
accepted Stage54 canonical bytes/FNV3155852323544543593 while retaining original
source/content fingerprints. Production behavior/hand oracle unchanged. First
standard build exit1 had no compiler diagnostic; cause remains unconfirmed.
All three failed receipts remain. The new test changed across batches, so whole
source snapshots are not claimed identical between them. Stage55 standalone
core evidence remains separate historical scope.

## Stage57: offline finite bits and editable character content starters

The offline `aydetlevel float-bits <eight-hex-digits>` accepts finite software
Float32 patterns (no `0x`/whitespace), returns canonical lowercase bits and the
unchanged exact decimal formatter, including signed zero. Existing decimal
`float` behavior is unchanged.

`aydetlevel create character fresh.dlt` and `create dash fresh.dlt` prepare and
validate public Level content before exclusive canonical output. Both have
Kinematic hero10 standing on Static floor100; character uses format1, dash uses
format10/advanced Character and one groundOnly Control owner. They use app/input1
and step1/64 with no Brain/Attack/HP/rules. Supply a matching application input/
Host recipe; the existing app56/input7 detcontrol scene is separate. Static seal
does not establish future motion or first-pre ground evidence. Fresh-only output
protects existing files; an I/O failure may retain a partial new file.

See [tool usage](tools/detlevel/README.md) and [tool design](tools/detlevel/design.md).
Actual Stage57 local Release LevelToolChecks passed1.8485s; standard Tool build
passed4.953s. Both starter create/validate/inspect and complete canonical bytes,
finite1.25/-0, malformed/nonfinite and protected/unknown-kind cases passed with
unchanged source snapshots. No new Host/matrix/long gate,
numeric/simulation wire profile or native success is implied. Final VM Deferred.

## Stage58: opt-in captured line of sight for idle decisions

Direct facade acquisition may set `DetBrain2DConfig::lineOfSight=true`; false
keeps exact Brain1 descriptors/literal/logic/behavior. True selects Brain2 with
the same field capacity and adds `SightBlocked=7` (`Retired=6` unchanged).
Use the exact already-installed Character/Attack configs and their complete
policy, then install Brain before actors/seal. The actual deterministic query
dependency captures one immutable matching collision scene before all owner
input writes; there is no second collision installer or mutable query cache.

Alive idle sensed owners raycast a closed pose-XY center segment to their fixed
target. Actual owner reciprocal layer/mask applies; owner is ignored, triggers
excluded and target hits skipped. Any other solid full AABB/wedge hit blocks,
including endpoints/inside starts and Kinematics. One-way boxes occlude by
geometry, while translating proxies remain frozen at capture. Blocked sight
requests patrol movement/false held, with real controller deceleration/gravity.
Busy/Dead/Retired precedence stays. It does not cancel a launched Attack or
filter its actual Hurt whitelist/damage, and does not provide navigation.

The recorded mode is the earlier pre-motion decision, not a later checkpoint
visibility claim. Pure restore checks registered consistency/full identity and
does not rerun a historical ray on moved geometry. Existing Level1..10 Brain
tables/standard Host still acquire Brain1; this direct opt-in adds no authoring
or Host input grammar. Existing profile1 runtime records remain historical.
See [SDK contract](../../AYDocs/sdk/contracts/2d.md#2dsimulationdeterministic-brain).
Actual necessary local MSVC Release gates passed: new
`AYEntity_BrainLineOfSight2DChecks`0.780831s, affected default Brain2DChecks3.65266s
and existing BrainHostExample11.4542s. New2 scope is4tick/5CP restore continuation,
12 selected hand rows/6geometry/16advances/6full actual CP comparisons/4bad
restore/1bad config (max3scene bodies), not independent whole-CP goldens.
Nineteen legacy complete files match prior Brain1 initial/final/input/hashtrace/
physicalRPL plus old Host six states/twoSave38/six traces. They establish these
actual default samples, not new LOS replay/late rollback/Host or history proof.
First core1 cl.exe CreateProcess Access is denied remains; orchestration j4->2
same-source retry passed with no production behavior/oracle edit.
Actual source/exe/receipts match stage58-actual-freeze.json and before/after
snapshots are equal. No matrix/long/native/VM gate; final native VM Deferred.

## Stage59: authored line of sight and a standard door Host

Level11 authors the existing opt-in Brain2 with `brains <count> <lineOfSight>`.
Use ordinary Level load/configure and the existing `detLevelBrainConfig2D` getter;
it resolves complete prepared Character/Attack/query authority. Old1..10 false
retain their byte/default branches, empty owners require false, and older formats
reject true. The tool exposes the authored bool/full Brain rows and canonicalizes
the explicit version through the real fresh Session recipe.

Optional default-OFF `AY_ENTITY_BUILD_DET_SIGHT_EXAMPLE` builds
`AYEntity_DetSightExample`: real EngineModuleRuntime, standard Entity Host/Scene,
[immutable Level11 content](examples/detsight/content.dlt). External input controls
only the registered door action; Enemy movement/Attack remains Brain-owned.
See [example recipe](examples/detsight/README.md) for save/replay and input boundaries.
Brain3 captures before door input4, so restoring a checkpoint with an earlier
blocked observation/current open mask does not require historical reraycasting.
Necessary local MSVC Release seven unique cases passed: new Level11 1.10249s,
old CharacterControl10 .204423s/MovingTrigger7 .69038s/Cooked .115796s,
standard SightHost10.4039s, CLI1.85419s and existing Resource1.06179s. New authored
scope is8ticks/9CP,16selected hand rows/23advances/9full actual CP comparisons/
16rejects with real file/text/cooked and restore continuation. Six64tick Host
paths agree in complete final CP/twoSave9/full hash trace or55-row continuation;
stdin68rows has suffix13 of55, Replay0input calls. Selected hand fields and full
actual execution equality are separate scopes, not independent whole-CP goldens
or historical ray authentication. Existing Resource uses old samples, not a
separately packaged Level11 sight scene. Final native VM Deferred; no new matrix/long proof.

Receipts stage59-levels2/host1/host2 and stage59-actual-freeze.json bind sources,
executable/artifacts and genuine individual JUnit passes. host1 remains overall
Failed: Resource passed and was not repeated; only failed Host/CLI retried after
changing the source fixture's `action 101` literal to existing `target 101`
grammar. Production/hand checks were unchanged and original OUT/failed raw files
remain. First levels1 harness named unknown AYEntity_LevelControl10Checks before
product tests; corrected existing CharacterControl10 target then passed.
Snapshots match before/after each batch; fixture source changed across batches,
so cross-batch full snapshot equality is not claimed.
No numeric/collision/Attack/save/replay wire, navigation or launched-attack
occlusion is introduced; prior accepted samples remain their historical scope.

## Stage60: offline initial-content preview (local checks passed; native Deferred)

The existing [level tool](tools/detlevel/README.md) provides the
`aydetlevel preview level.dlt fresh.svg` command for checking authored placement.
It shows real software AABB endpoints/filled triangles and initial body roles,
plus per-actor relative waypoint routes. The output is a bounded static SVG,
not a running World, checkpoint or future motion prediction.

Use a fresh output path. The real full Level preflight and complete finite SVG
construction precede file creation; browser pixels are not authority. See the
[tool design](tools/detlevel/design.md) for limits, geometry order and file-failure
scope.

Actual local MSVC Release `AYEntity_LevelToolChecks` passed2.26624s; the
standard production Tool build passed5.438s. The single finite gate performed
seven preview invocations: four successful outputs and three explicit refusals.
Selected hand vertices/world and viewBox bits, path origin independent of body
offset, mask0, repeated terrain whole-SVG byte equality and XML parsing of all
four outputs passed. The terrain output has5518 bytes,11 actors,1 path instance
and2 waypoints, with contentHash `1216874329789544449`.
No simulation tick/current CP, browser-rendering acceptance, future reachability,
independent full-CP golden or native/transport authentication is established.
Physical flush/close fault injection was not run; final native VM Deferred.
The accepted source is stage60-tool2; earlier old Stage59 CLI execution is excluded.

## Stage61: finite initial placement editing (local checks passed; native Deferred)

Use `aydetlevel place source.dlt actor x y fresh.dlt` or `resize-half`
through the existing [tool](tools/detlevel/README.md). The input is checked
software decimal; only the selected actor's initial poseXY or positive body
half is changed. inspect exposes position/halfExtent/offset exact bits.

The full public Level pipeline checks the edited content before fresh output.
Explicit versions and other authority are retained; match the resulting content
identity to the application recipe. Paths follow their actor's initial origin,
while separately authored absolute spawn points are not moved. This is offline
content editing without a runtime checkpoint writer.

Actual local MSVC Release `AYEntity_LevelToolChecks` passed2.41834s, with
production Tool build5.891s. Selected independent dyadic place(-1.25,0.5) and
resize-half(0.5,0.5) inspect bits, changed content identity, retained unselected
tables, full edited/canonical/no-change byte equality and four unknown-actor/
nonfinite/nonpositive-half/existing-output refusals passed. The genuine evidence
is `stage61-actual-freeze.json` / `stage61-tool1`; source/executable pins and
before/after snapshots are bound there, without production behavior/oracle fix.
This finite offline gate does not separately execute an edited bound path or
absolute-spawn gameplay, Sim/current CP, physical I/O faults or browser rendering.
No hot reload, save migration, runtime pose writer, new numeric/wire profile or
historical authentication is added; final native VM Deferred.

## Stage63: finite Static obstacle authoring (local verified; native VM Deferred)

`aydetlevel create terrain fresh.dlt` supplies the existing character/
floor starter with explicit staticSlopes only: format1/app1/input1/step1/64,
collision6/controller5 and nonadvanced/non-one-way. It does not infer runtime
input, moving/continuous terrain, respawn or gameplay tables.

Use `add-solid source.dlt new-id shape x y half-x half-y layer mask fresh.dlt`
for one ID-sorted ordinary nontrigger Static AABB or actual filled left/right wedge.
ID is full nonzero UInt64 decimal; layer/mask are UInt32 decimal and coordinates/
positive half extents use software FloatText. Wedges require an existing explicit
staticSlopes policy; source format and all unselected authority are preserved.
Layer0 fails real validation, while mask0 remains valid geometric authoring.

The complete modified Level validates/seals/encodes before fresh exclusive output.
Follow [tool usage](tools/detlevel/README.md) through place/resize-half/preview;
no runtime motion writer or new simulation profile is added. Count64/configured
lower limits/text64KiB/canonical8KiB/joint budgets remain.

Actual local MSVC Release AYEntity_LevelToolChecks passed in 3.41s; the standard tool build completed in 6.188s. This finite gate exercised create/add/place/resize/preview, selected dyadic actor bits and actual filled-polygon points, sorted identities, full unselected canonical policy/original actors and canonical stability, legal mask0, a valid 64-body source and 16 protected normal refusals. The recorded source snapshots matched; the actual executable and artifacts are hash-bound. Evidence is out/detcontent36-40-20261011/stage63-actual-freeze.json and stage63-tool1/result.json. No Sim advance, future walkability, whole-checkpoint golden, authentication, new content Pak execution, broad matrix or native VM acceptance is claimed; native final VM remains Deferred. No production/oracle repair or prior gate reuse was needed for this actual invocation.

## Stage64: named-path authoring (local verified; native VM Deferred)

The offline tool's add-path/bind-path/edit-waypoint commands prepare
initial full Level content through the existing Path facade; no runtime API or
profile is added. add-path creates a two-point Path1 and binds one unbound ordinary
solid Static in the same edit, because Level requires every path to be used.
bind-path shares an existing ordinary path. The body becomes MovingObstacle with
zero initial velocity; geometry/pose/layers/masks and other tables remain. Wedges
need explicit source movingSlopes, and ordinary binding grants no moving-trigger
or Actions authority.

edit-waypoint retains count/playback/role/bindings and changes the immutable table
for every user. Origin is each actor.position, independently of body.offset;
absolute spawns remain. Exact +0 is required at point0. Software finite decimals,
travel1..65535/dwell0..65535 and existing8paths/24points/64bodies plus full joint
budgets remain. Current valid formats1..11 are preserved; sorted path insertion
creates a newly resolved identity, not old-checkpoint compatibility.

See [tool usage](tools/detlevel/README.md). Complete validation/seal/encoding
precedes fresh exclusive output; no tick is executed or future motion proved.
Actual local MSVC Release AYEntity_LevelToolChecks passed in 4.32962s; the standard tool build completed in 5.938s. The finite authoring gate checked Once/Loop/PingPong spellings, shared per-actor origins versus body offsets, one-point edits and previews, an explicitly enabled moving wedge, existing Path2 role preservation, complete modern Level11 tables, canonical stability, a valid eight-path source and 20 protected normal refusals. Source snapshots matched; the actual executable and raw artifacts are hash-bound in out/detcontent36-40-20261011/stage64-actual-freeze.json and stage64-tool1/result.json. No production/oracle repair or old gate reuse was needed for this actual invocation. No Sim tick, executed trajectory, future sampled reachability, collision safety, whole-checkpoint golden, Host/Pak/matrix, history authentication or native VM acceptance is implied; native final VM Deferred.

## Stage65: offline Static geometry reuse (local verified; native VM Deferred)

The existing content CLI flattens an explicitly mapped donor motif
into a complete fresh target, preserving destination policy/authority.

stamp-solids donor.dlt target.dlt shiftX shiftY old:new[,old:new...] fresh.dlt
has argc8 including argv0. Both ordinary files independently load as complete
validated Level. The map token is1..1024 bytes/1..16 full nonzero decimal UInt64
pairs: exactly one colon per entry, no empty entry, overflow, duplicate donor/new
ID, missing donor or existing target ID. Map order is irrelevant; merged actor
IDs are sorted. Shifts use real finite software FloatText and software addition.
Actual rounding and signed-zero rules apply, so zero shift does not universally
preserve source position bits. A nonfinite translated pose is rejected.

Only unbound ordinary nontrigger/non-one-way Static AABB or filled left/right
wedges qualify, never Character or a path user. All authored actor references
are rejected: Action targets; Interaction trigger/actor/action target; Progress
claim trigger/actor and objective action target; Respawn spawn/binding/regions;
Vitality actor/permission/region; Attack owner/targets; Brain owner/target; Control
owner. Claim/objective/path/counter/rule IDs in separate namespaces are not
misread as actor references. This closes known authored authority, without
certifying unknown application code or remapping gameplay graphs.

The eligible complete initial body is copied, changing only explicit actor ID
and software-translated position. Shape, half extents, offset, layer/mask and
zero velocity/default roles persist. Every destination format/app/input/step,
policy/table and existing actor remains intact. Wedges require destination
staticSlopes already enabled; donor policy is never imported or auto-upgraded.
Actual full merged Level validation/fresh seal and canonical encoding precede
exclusive fresh output. Max64/configured lower bodies, text64KiB/canonical8KiB,
represented geometry/initial penetration and joint namespaces remain binding.
Static-static overlap retains the existing composition semantics.

Output is flat expanded initial content in the existing full identity, not a
prefab/reference/hot reload handle; later donor edits do not propagate. Existing
source/target/output are protected, normal refusal writes no file and physical
I/O failure may leave a partial fresh file. Existing .dlt/.dlc loading recipes
consume it unchanged. No .act/detprefab format, Resource loader, public facade,
permission remap, running-state pose/HP writer, Sim advance or future motion/
walkability/restore migration guarantee is added. inspect/preview stay offline;
actual filled polygons are shown without simulation or history authentication.

Actual local MSVC Release AYEntity_LevelToolChecks passed in 5.13788s; the standard tool build completed in 6.0s. The finite offline gate checked independent dyadic translated body fields, actual three-shape polygon/SVG vertices, permuted mapping and canonical reencoding whole-byte equality, complete unselected destination policy/actors and 22 protected normal refusals. Source snapshots matched; the actual executable and raw artifacts are hash-bound in out/detcontent36-40-20261011/stage65-actual-freeze.json and stage65-tool1/result.json. The product gate passed on its first actual invocation. JUnit stdout was truncated to 1024 characters; only the private freeze reader was adjusted to bind the genuine LastTest.log from the same CLI run directory. No product/oracle repair or gate rerun occurred. No Sim tick, Host, executed future movement, prefab lifecycle, whole-checkpoint golden, unknown application/historical authentication or native VM acceptance is implied; native final VM Deferred.

### Stage66 opt-in authored terrain integration (local verified; native VM Deferred)

The existing default-OFF Control Host example gains a separate explicit workshop
scenario. Current offline tools produce a byte-matched complete Level11 resource
and initial SVG; existing Path/Character/Control/Actions/Brain-sight/Attack/Vitality
facades then execute it through the standard Scene Host. This adds content and a
sample input protocol, with no new gameplay facade or old profile change.
The focused gate is Live128 plus real CP64 continuation64 and Record128. Its
selected hand checks and actual complete-state comparisons remain distinct from
independent full-checkpoint goldens, record playback or authentication.

Actual local MSVC Release AYEntity_WorkshopHostExample passed in 11.1761s; the affected original AYEntity_ControlHostExample passed in 13.4204s. Seven actual offline authoring commands succeeded and the entire generated canonical content equaled independently authored workshop.dlt before Host acquisition. Live128 plus captured CP64 continuation64 and Record128 performed320 actual advances; final complete CP bytes, every original tick trace and all64 restored suffix CP comparisons agreed. The committed workshop summary records heroHP=18, heroHurts=1, attackStarts=2, attackAttempts=1, brainPulses=2 and finalHash=10463999747462011456 (diagnostic UInt64 string). Original Control complete-byte compatibility was separately checked. Source snapshots were stable within their separate real execution episodes. Final Workshop executable bytes were unchanged; the earlier Control executable SHA remains an honest historical pre-test observation whose path was subsequently relinked, not a current-file byte verification. Exact raw authoring/Host/legacy evidence is in out/detcontent36-40-20261011/stage66-actual-freeze.json. The native-encoded LastTest.log is retained as raw bytes, not JSON/UTF-8 authority. Selected hand observations plus full CP execution equality are not independent whole-CP goldens; closed recording existence is not playback/seek/history authentication. No new interactive, broad matrix, Pak or native VM acceptance is claimed; native final VM Deferred.

The subsequent corrected-content Workshop attempt also remains failed: its selected absolute-X hand check rejected at CP121 (JUnit3.71437s). The earlier expected static cap was a false premise because Enemy is the existing actual Kinematic body, not Static. The only final Host change corrected this narrow hand oracle: absolute XY is checked through CP120; CP121..128 instead check both Y=1.5, EnemyX minus HeroX=0.5 and monotone Hero movement against actual previousPosition. No kernel, Enemy role, permission, profile or runtime behavior was changed. Its already successful seven-command recipe and configure were reused unchanged; only the necessary final Host build and sole Workshop gate continued. The directed earlier CP41 diagnostic retains 40 complete tick-trace rows (1..40), then reports the CP41 mismatch; it is a failed diagnostic prefix, not an extra passed41-tick gate. Both original failure episodes and diagnostic bytes remain bound separately from the final passing execution; no soak or native acceptance is inferred.

The first genuine composite gate is retained: the original Control case passed while Workshop failed its selected hand check at CP41. Diagnostics showed legitimate ordinary-floor X/Y corner contact at fraction0 resolving with the existing fixed X priority; Session had no execution fault. The content revision preserves the original explicitly selected continuousTerrain/terrain8 policy and every kernel/oracle rule. It changes only the new floor layout and stable authored ID so initial strict-X overlap and the intended carrier priority are explicit. The corrected complete template, independently authored expected text and seven-command recipe are bound to the final actual descriptors, not the original failed content hashes. A necessary sole Workshop continuation is new content validation; the already passed original Control case is not repeated, and its composite remains overall failed.

The earlier private pre-test CTest path-discovery refusal is retained in the resumedPreTestFailure receipt. Its completed authoring/configure/build steps and unchanged executable were reused after correcting only the OUT evidence discovery; no product gate or successful tool recipe was repeated.

## Stage67: offline filters and one-way platform authoring (local verified; native VM Deferred)

```text
aydetlevel create dash base.dlt
aydetlevel add-oneway base.dlt 90 4 1.5 1 0.125 2 1 platform.dlt
aydetlevel edit-filter platform.dlt 90 2 0 filtered.dlt
aydetlevel inspect filtered.dlt
aydetlevel preview filtered.dlt filtered.svg
```

edit-filter source.dlt actor-id layer mask fresh.dlt has argc7;
add-oneway source.dlt new-id x y halfX halfY layer mask fresh.dlt has argc11,
including argv0 and command. IDs are fully consumed nonzero decimal UInt64;
layer/mask are fully consumed decimal UInt32. Floats use the actual finite
software binary32 decimal parser, including normal rounding/signed-zero rules.
Half extents must be represented strictly positive; nonfinite/overflow or an
underflow-to-zero half extent is refused.

edit-filter changes only one existing actor's initial body.layer/mask, preserving
its mode/shape/geometry/offset/velocity/trigger/oneWay/character/path, all selected
policies/tables and source format/app/input/step. Active layer0 is invalid; mask0
remains active geometric content, not Disabled.
Retained proxy geometry does not promise an unconditional filtered query hit:
reciprocal collision/raycast filters still exclude a mask0 body.
Full actual Level validation checks authored permissions: an Actions-controlled
initial mask can only0 or its
existing literal collisionMask. Another mask is refused; authority is never
rewritten to make an edit succeed.

add-oneway requires the source's explicit advanced+extended+oneWayPlatforms
policy. Existing create dash supplies it; ordinary character and terrain
starters do not. There is no automatic policy/version upgrade and no artificial
format>=10 restriction on otherwise valid explicit-policy content. The sorted
new unique body is ordinary nontrigger Static AABB, oneWay=true, with zero
offset/velocity, no Character/path/ignored identity. No wedge, trigger, moving
platform or gameplay permission is inferred. Runtime one-way response remains
the existing relative downward top-face crossing with strict X overlap; side,
bottom and already-inside motion is allowed, queries inspect complete AABB
geometry. Existing one-way inside and Static/Static overlap semantics remain;
the authoring tool does not promise general overlap rejection or repair.

Both commands load/validate complete ordinary source content, edit local
authoring only, then actual full initial Session validation and canonical
encoding precede exclusive fresh output. Bodies64/configured lower capacity,
source64KiB/canonical8KiB, represented geometry and joint namespace/authority
bounds remain binding. Existing source/protected output are never replaced;
normal refusals create no destination, physical I/O failure may leave a partial
fresh file under the existing writer contract. All unselected fields/tables and
versions persist. Full existing content identity includes the changed body.

inspect appends actor.oneWay Bool; old fields remain. Preview already renders
true box/top line and one-way role without simulation advance. A mask0 platform
still appears in geometric preview. Adding one-way does not supply a drop-through
timer/input or grant Actions collision-control permission for that role.
No new capability/format/facade, physics profile, Scene/runtime writer, editor,
hot reload, replay migration or future movement/walkability guarantee is added.
Existing .dlt/.dlc acquisition and canonical bits remain the current recipe.

Actual local MSVC Release AYEntity_LevelToolChecks passed in 6.08132s; the standard tool build completed in 6.485s. The finite offline gate verified selected dyadic box/top fields and role flags, full unselected policy/actors, canonical stability, mask0 active geometric content with reciprocal filters, actual authorized-door refusal, bounded-body capacity and protected argument/role refusals. Matching source snapshots, the actual executable, raw CLI artifacts and genuine JUnit/LastTest log are bound in out/detcontent36-40-20261011/stage67-actual-freeze.json. The document reader does not run product tests. No Sim tick, Host/movement/replay execution, whole-checkpoint golden, unknown application/historical authentication or native VM acceptance is implied; native final VM Deferred.

## §1 制作入门

[确定性关卡制作入门](tools/detlevel/AUTHORING.md)给出实际执行过的矩形、坡面、编辑、共享路径、实体组
复用与单向平台步骤。27条finite制作命令均通过，完整源码与工具身份相同。
它制作初始内容；运行与恢复证据由组合Host示例提供，native最终VM仍延期。

## §2 Stage69 registered collectible progression

Stage69 LocalPassed / NativeDeferred: one actual standard Host gate AYEntity_ChallengeHostExample in 14.3985s. Live128 plus actual CP97 restore31 (159 advances), and Record128. Real collision Enter is queued at CP97; processing tick97 applies collectible-disable Action sequence1, counter1/objective1 and gate-disable sequence2. CP98 retains the pre-Flow Brain sight observation. Final claimed stamp97, counter1, objective true and both masks0 were read from registered authority. Complete actual CP97/final checkpoint/trace compare between Live and Record; 31 complete restored suffix checkpoints compare inside Host. Independent motion bits stop at CP120; thereafter actual two Kinematic actors maintain Y1.5, X separation.5 and nondecreasing previousPosition. HP/Attack use actual observations, not copied future counts. Record existence is not playback or authentication; no new numeric/backend matrix, runtime profile, native VM or long campaign. Evidence: D:\Aliyat\AliyatEngine\out\detcontent36-40-20261011\stage69-challenge1\result.json.

## Stage70: finite content property edits (local verified; native VM Deferred)

The argument counts including argv0 are7 and6. Source must be a complete
valid current Level1..11. IDs are full-token nonzero unsigned decimal: actor
UInt64 and path UInt32. Offsets use the existing finite ASCII software FloatText
parser, including preserved signed zero; no native float intermediate is used.

edit-offset changes only the selected body's geometry offset. Its pose, shape,
half/layers/mask/role, path binding, all waypoint offsets and absolute respawn
spawns remain. A bound path still starts at actor.position, never body.offset.
The represented initial geometry can become invalid or overlap other solids;
actual full validation decides before any output is created. Preview shows exact
software endpoints of that new initial geometry, not a current checkpoint.

edit-playback changes only one existing immutable path playback field, for every
sharing actor. It preserves path ID/count/points/travel/dwell/movingTrigger role,
all bindings and full permission tables, including an already authored Path2.
Once/Loop/PingPong are the actual existing Path modes; Loop includes its real
last-to-first leg rather than a promised teleport. This does not execute a path,
restart/import its cursor, authorize Actions or prove future sampled reachability.

Format/application/input/step and all unrelated content remain explicit. A real
fresh Session/manifest validates the edited complete Level and canonical encoding
finishes before exclusive fresh output. Changed content resolves a new full
contentHash/manifest, not old-checkpoint compatibility or live hot reload.
Existing64-body/8-path/24-point,64KiB text/8KiB semantic and joint Session limits
still apply; no table size or capacity is upgraded. Normal validation/argument
rejection creates no output. Physical I/O failure can leave a partial new file.
No core/wire/API module or capability entry is added. Actual local MSVC Release AYEntity_LevelToolChecks passed in 6.84019s; the standard tool build completed in 6.141s. Eight successful property CLI calls and eleven normal refusals checked selected independent software geometry/offset fields, path sharing and playback edits, complete unselected source policy/actors, canonical stability and protected normal refusals were checked by that finite offline gate. Source snapshots, the executable, raw CLI outputs and genuine JUnit/LastTest log are bound in out/detcontent36-40-20261011/stage70-actual-freeze.json. The document reader executes no product. No Sim tick, current checkpoint, Host/path movement, restart/reachability, unknown application or historical authentication is implied. No new capability, entry, module, wire/profile or test target is added; native final VM Deferred.
