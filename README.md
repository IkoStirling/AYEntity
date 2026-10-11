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
