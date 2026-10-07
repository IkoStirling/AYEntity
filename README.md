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
