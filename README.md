# AYEntity

AYEntity 是 AY Engine 的实体组件系统，负责 Entity/Component 存储、System 调度、场景序列化以及动画、渲染和网络绑定。

## 公开接口

```cpp
#include <AYEntity.h>
#include <AYEntity/IEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/components/TransformComponent.h>
```

入口头文件位于模块根目录；抽象接口位于 `interface/AYEntity/`，其余公开头位于 `include/AYEntity/`。

## 依赖

- AYCore、AYGameLoop、AYReflect、AYSerializer
- AYRenderer、AYMath、AYAnimation、AYResource、AYEventSystem
- AYTest（仅测试）

ECS 结构、Simulation/Presentation 分轨和引导流程见 [design.md](design.md)。
