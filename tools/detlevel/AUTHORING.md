# 确定性关卡制作入门

**Version:** 0.1.0
**Date:** 2026-10-12
**Status:** Active — necessary local authoring recipe passed; native VM deferred
**Owner:** AYEntity / Determinism

> **引擎架构入口**：[ARCHITECTURE.md](../../../../AYDocs/ARCHITECTURE.md) §6、§10。

## §1 准备工具和输出目录

构建 `AYEntity_DetLevelTool`，取得 `aydetlevel` 可执行文件的完整路径。下面的 PowerShell
示例用 `$tool` 表示这个路径，在一个空输出目录执行。每一步写入新的 `.dlt` 文件，
原文件保留，便于比较和撤回修改。

```powershell
$tool = '你的构建目录/AYRuntime/AYEntity/aydetlevel.exe'
& $tool create terrain 01-terrain.dlt
& $tool inspect 01-terrain.dlt
& $tool preview 01-terrain.dlt 01-terrain.svg
```

`terrain` 提供最小角色、地面及明确的静态坡面策略。`character` 是基础角色模板，
`dash` 提供高级角色和冲刺配置。模板的应用/输入身份需要与实际 Host 匹配；模板
不会代替应用的输入处理或生成战斗、任务权限。SVG 展示初始布局和路径，模拟中
运动、伤害和可达性需要由实际内容运行验证。

## §2 放置矩形与坡面

```powershell
& $tool add-solid 01-terrain.dlt 50 slope-left -4 0.5 1 0.5 2 1 02-left.dlt
& $tool add-solid 02-left.dlt 90 slope-right 4 0.5 1 0.5 2 1 03-slopes.dlt
& $tool add-solid 03-slopes.dlt 200 aabb 7 1 0.5 1 2 1 04-solids.dlt
& $tool preview 04-solids.dlt 04-solids.svg
```

参数依次是源关卡、新实体 ID、形状、中心 X/Y、半尺寸 X/Y、layer、mask、新输出。
半尺寸必须大于零。`slope-left` 的高点在左侧，`slope-right` 的高点在右侧；坡面是
填充三角形。layer 和 mask 是整数位集合，实际碰撞需要双方过滤匹配。mask 为零
可保留几何而禁用匹配碰撞。工具会拒绝重复 ID、非法数值和超出实际关卡预算的内容。

数字输入按软件 Float32 的固定规则舍入。需要检查精确值时：

```powershell
& $tool float 1.25
& $tool float-bits 3fa00000
```

`inspect` 的 `positionBits`、`halfExtentBits` 和 `offsetBits` 提供精确位表示。

## §3 调整位置和尺寸

```powershell
& $tool place 04-solids.dlt 200 6 1 05-position.dlt
& $tool resize-half 05-position.dlt 200 0.5 0.75 06-size.dlt
& $tool canonicalize 06-size.dlt 06-canonical.dlt
```

位置修改保留 body.offset 和其他实体。绑定路径的原点会随实体位置移动；独立配置
的绝对重生点保留原坐标。相同精确赋值具有相同规范化字节，实际修改会更新内容身份。
已有存档和录像仍使用各自的完整内容身份，编辑后的内容需要匹配的新会话。

## §4 制作移动平台

先创建普通静态矩形，再将新路径与它一起保存：

```powershell
& $tool add-solid 06-size.dlt 201 aabb -2 1 1 0.25 2 1 07-platform.dlt
& $tool add-path 07-platform.dlt 201 30 2 1 64 pingpong 08-path.dlt
& $tool edit-waypoint 08-path.dlt 30 1 2 0 32 8 09-path-edit.dlt
& $tool preview 09-path-edit.dlt 09-path.svg
```

`add-path` 的两点偏移分别为正零和给定 dx/dy；它同时绑定实体，避免产生未使用的
路径。播放模式支持 once、loop、pingpong。travel 范围为 1..65535 逻辑 tick，dwell
为 0..65535。路点索引从零开始，首点必须保持正零；路径原点采用实体 pose，独立
于 body.offset。`bind-path` 可让另一个普通静态实体使用已有路径：

```powershell
& $tool add-solid 09-path-edit.dlt 202 aabb -4 3 1 0.25 2 1 10-platform.dlt
& $tool bind-path 10-platform.dlt 202 30 11-shared.dlt
```

共享路径编辑影响所有绑定实体。新绑定要求普通静态实体；角色、触发区、单向平台
或已绑定实体不能这样转换。移动坡面需要源关卡明确选取 movingSlopes 策略。

## §5 复用静态地形组

```powershell
& $tool stamp-solids 04-solids.dlt 01-terrain.dlt 12 0 '50:150,90:190,200:120' 12-reused.dlt
& $tool preview 12-reused.dlt 12-reused.svg
```

从 donor 选择普通静态实体，以 old:new 映射明确分配目标 ID，并按软件浮点运算平移。
目标关卡保留自身策略、权限和所有既有实体，复制的几何直接展开到完整关卡中。
一次最多 16 对映射。角色、触发区、单向或移动实体，以及任何带关卡权限/规则引用
的 donor 实体会被拒绝；需要独立配置其玩法含义。目标坡面策略必须已经选取。

## §6 单向平台和碰撞过滤

```powershell
& $tool create dash 13-dash.dlt
& $tool add-oneway 13-dash.dlt 210 3 1 1 0.25 2 1 14-oneway.dlt
& $tool edit-filter 14-oneway.dlt 210 2 0 15-filter.dlt
& $tool inspect 15-filter.dlt
& $tool preview 15-filter.dlt 15-filter.svg
```

`add-oneway` 要求源关卡已明确选择高级角色和单向平台策略，生成普通静态矩形。
`edit-filter` 只改变 layer/mask，保留实体角色、尺寸和权限表。带动作权限的实体
仍受真实权限约束，修改不会自动改写该表。mask0 保留活动代理的几何，但不匹配
双向过滤的碰撞或射线。预览中的单向标记表示初始角色，实际下落穿透由角色输入
与物理策略共同处理。

## §7 检查和实际运行

```powershell
& $tool validate 11-shared.dlt
& $tool inspect 11-shared.dlt
```

验证使用完整关卡加载和 fresh Session 准备流程，检查当前策略、预算、引用及初始
几何。动态内容还需使用匹配应用/输入的真实 Host，观察平台支撑、角色运动、遮挡
和战斗。完整制作样例见本模块 `examples/detcontrol` 的 workshop 场景。

工具先完成验证、编码或完整 SVG，再独占创建新输出。逻辑拒绝不创建文件，已有
输出不会覆盖；物理写入或关闭失败可能留下新的部分文件，需由调用方处理。


## §8 本机制作验收

上述27条制作命令已在实际工具中按顺序执行，完整源码及可执行文件保持一致。
两处共享路径绑定、规范化字节以及mask0单向角色均检查通过。此处验证初始内容
制作流程；组合内容运行另见[workshop示例](../../examples/detcontrol/README.md)。
SVG不是运行可达性的证明，native最终虚拟机验收仍延期。
