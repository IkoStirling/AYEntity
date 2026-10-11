# 确定性内容、进度与持久存档示例

**Version:** 0.1.0

**Date:** 2026-10-12

**Status:** LocalPassed — native VM deferred

**Owner:** AYEntity

标准 `EngineModuleRuntime`、`EntityRuntimeModule`、`DeterministicHostIntegrationModule`
驱动一个实际参与碰撞的移动平台关卡。加载 [content.dlt](content.dlt) 后，
公开 Level facade 安装角色、碰撞、路径、Actions 和 Interaction。注册期
`beforeActors` hook 安装 Progress，随后创建实体，最后由 Host seal；不重复安装 Actions。
示例及下面的六进程短门禁已通过本机 Windows Release 验收；原生跨平台验收等待最终 VM 环境。

## §1 内容与输入

角色10从水平平台130上起步。平台使用真实 layer2/mask1，与角色 layer1/mask6
互配，移动2单位/64ticks再返回。角色速度2，每个输入tick显式采样 jump/drop/crouch，
默认全部false；角色预输入、路径、碰撞和支撑继续使用唯一既有 motion owner。
初始位置、尺度、路径采样和1/64步长均使用易精确表示的二进制有理数。

触发区200暂停平台，210恢复平台，规则来自 `AYDLEVEL 2`。两个钥匙220/230
调用注册的 collectible claims，真实 Enter 使 counter1增加，随后禁用该触发体。
两把钥匙满足 objective1，应用明确请求一次开门110，不隐藏重试。终点240只有
objective1已满足时才允许claim，counter2使objective2成立。普通拒绝单独记录。
应用不在每帧调用claim，也不为没有真实 Enter 的状态制造请求。

所有游戏 authority，包括 claims、规则窗口、路径游标、角色支撑、RNG/事件闭包
与应用统计都进入完整检查点。外部终端输入不是检查点内容；恢复后下一tick的
外部样本仍由调用者提供。表现只读稳定 Sim ID 每次重取状态，不保留跨恢复实体句柄。

应用 schema112、logic/validator70、input6/progress8/observer63，与 Level 的
路径8/9、内容32..47、Actions64起、Interaction80起、Progress96起互不重叠。
注册系统都在 collision.priority 内按稳定ID排序，Rules9先于path11、角色20、
collision30、support40、pathpost41；observer63最后读取本tick实际结果。

## §2 运行与存档

构建目标 `AYEntity_DetContentExample`，链接 `AYEntity::DeterminismHost`。
从仓库根运行已构建程序，例如：

```text
AYEntity_DetContentExample --level AYRuntime/AYEntity/examples/detcontent/content.dlt --ticks 160 --require-features --save-at 64 --save fresh64.dsv --output fresh-live
AYEntity_DetContentExample --level AYRuntime/AYEntity/examples/detcontent/content.dlt --ticks 160 --require-features --load fresh64.dsv --output fresh-loaded
AYEntity_DetContentExample --level AYRuntime/AYEntity/examples/detcontent/content.dlt --interactive --ticks 160
```

交互命令：`right N`、`left N`、`idle N`、`jump`、`drop`、`crouch N`、`stand N`、
`status`、`save FRESH.dsv`、`load FILE.dsv`、`help`、`quit`。运动速度与蹲下采样
保持到下次改变；jump/drop是单tick采样，后续运动tick显式释放。当前关卡为普通
实心平台，drop不承诺穿过它；crouch配置未开启，这两个命令展示输入边界而不伪称触发。
文件路径限定可打印ASCII且不能包含空格的交互token；命令行路径可用正常引号传入。

存档在外部帧边界调用 Stage38 utility。`.dsv` 包含原完整 canonical checkpoint，
不同进程先从同一内容/应用recipe构造并seal Session，再 `readDetSave` →
`controller.restore` → `resume`。成功decode不证明兼容；安装的 manifest 与 pure
validators 必须接受才能恢复。记录/回放模式不得加载或倒写历史。save必须fresh，
不覆盖、不自动清理 partial 文件，不承诺崩溃durability、认证或旧规则迁移。

`--record fresh.rpl` 返回实际封口filename（通常 `fresh_000.rpl`），随后可
`--replay actual_000.rpl` 或追加 `--seek 64`。回放不调用live input callback。
`.state` 输出原完整检查点，`.trace` 每行 `nextTick checkpointHash`，summary中的
64位hash以字符串表示；hash只用于定位，完整bytes才用于最终状态相等检查。

## §3 有限验收范围

`AYEntity_ContentHostExample` 是六个短标准Host进程：Live160保存64、新进程
Load64→160、Record160、Replay160、Seek64→160，以及真实stdin在64保存、前进16、
读取同一存档后继续96。门禁比较所有最终 `.state` 完整bytes、Live/Record/Replay
逐tick hash trace、Load/Seek和交互恢复后的完整后缀，以及两个实际64存档bytes。
特征计数来自注册后的真实结果：keys2/finish1、开门、实际平台pause/resume、实际
角色支撑携带、claims3/finish1/denied0，不根据输入次数推断功能已执行。

该示例不提供渲染窗口、网络传输、动态安装、resource cook/pak loader、存档迁移或
最终VM原生验收。本轮没有新长稳任务，也不把此短关卡外推成所有内容的几何正确性认证。
