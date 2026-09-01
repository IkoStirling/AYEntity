# AYEntity

AYEntity 是 AY Engine 的实体组件系统，负责 Entity/Component 存储、System 调度、场景序列化以及动画、渲染和网络绑定。

## 公开接口

```cpp
#include <AYEntity.h>
#include <AYEntity/IEntity.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/components/TransformComponent.h>
```

入口头文件位于模块根目录；抽象接口位于 `interface/AYEntity/`，其余公开头位于 `include/AYEntity/`。

## 依赖

- AYCore、AYGameLoop、AYReflect、AYSerializer
- AYRenderer、AYMath、AYAnimation、AYResource、AYEventSystem
- AYTest（仅测试）

ECS 结构、Simulation/Presentation 分轨和引导流程见 [design.md](design.md)。

## 显式组件注册

组件必须在创建 World 存储前注册。模块在 `registerTypes()` 阶段调用
`registerComponent<T>()`；需要进入 `.ayscene` 和编辑器动态增删链路的类型
调用 `registerSceneComponent<T>()`。所有模块完成后由 Host 封存注册表：

```cpp
#include <AYApplication/EngineModuleRuntime.h>

ayt::app::EngineModuleRuntime runtime(host);
auto& registry = ayt::entity::ComponentRegistry::instance();

if (auto result = runtime.modules().emplace<
        ayt::entity::EntityComponentModule>(registry); !result) {
    // report result.message()
}

if (auto result = runtime.prepare(); result) {
    registry.seal();
    result = runtime.install();
}
```

`Entity::addComponent<T>()` 不再隐式注册类型；它只在每个 World 第一次为
该类型创建存储时验证注册状态，因此普通 add/get/query 不增加注册分支。
场景保存和读取也不会触发隐式注册，Host 必须先完成上述启动阶段。
当前 `EntityComponentModule` 只迁移组件元数据注册，GameLoop SubSystem 和
表现系统仍使用既有 `bootstrapEntityCore()` / `bootstrapModule()`。
