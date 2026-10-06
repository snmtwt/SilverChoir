# H5 UI Plugin 测试环境

本指南使用插件自带的 `h5ui://plugin/showcase.html` 页面，在一个空关卡中验证当前原生后端的主要能力。

## 资源

- 页面：`Resources/UI/showcase.html`
- 样式：`Resources/UI/showcase.css`
- 脚本：`Resources/UI/showcase.js`
- 图片：`Resources/UI/media/showcase.png`
- 视频：`Resources/UI/media/showcase.mp4`

这些资源位于插件目录中，编辑器和打包后的 Win64 游戏都可以通过 `h5ui://plugin/` 读取。

## 一、创建空关卡

1. 在 Unreal Editor 中选择 **File > New Level > Empty Level**。
2. 保存为 `L_H5UIPluginTest`。
3. 确认 **Edit > Plugins > H5 UI Plugin** 已启用。当前工程已经启用，不需要重复修改 `.uproject`。

## 二、创建全屏 Widget

1. 在内容浏览器中选择 **Add > User Interface > Widget Blueprint**。
2. 命名为 `WBP_H5UIPluginTest`。
3. 打开 Designer，从 Palette 的 **H5 UI Plugin** 分类拖入 **H5 UI View**。
4. 将控件命名为 `HtmlView`，勾选 **Is Variable**。
5. 将 Anchors 设为全屏拉伸，Left、Top、Right、Bottom 都设为 `0`。
6. 设置以下属性：

| 属性 | 值 |
| --- | --- |
| URL | `h5ui://plugin/showcase.html` |
| Auto Load | `true` |
| Receive Input | `true` |
| Consume Input | `true` |
| Pass Through Transparent Mouse Input | `true` |
| Pass Through Keyboard Without Editable Focus | `true` |
| Enable JavaScript | `true` |
| Target Frame Rate | `60` |
| Default Size | `1920 x 1080` |

## 三、初始化数据

在 `WBP_H5UIPluginTest` 的 Event Graph 中，为 `HtmlView` 绑定 **On Ready For Bindings**，依次调用：

```text
Set Data String  Name=player.name       Value=Silver Pilot
Set Data Number  Name=player.level      Value=27
Set Data Boolean Name=system.connected  Value=true
Set Data String  Name=session.status    Value=Ready
Set Data String  Name=player.callsign   Value=CHOIR-7
Set Data String  Name=mission.region    Value=central
Set Data String  Name=mission.notes     Value=Maintain formation and verify the media feed.
Set Data Number  Name=mission.signal    Value=65
Set Data Number  Name=mission.readiness Value=82
Set Data String  Name=last.event        Value=ReadyForBindings
Synchronize Models
```

注意：`Name` 是 `FName`，需要完整填写点号，例如 `player.name`，不能只填 `name`。

## 四、接收页面事件

为 `HtmlView` 绑定 **On UI Event**：

1. 使用 **Break H5UI Event** 得到 `Name`、`Payload` 和 `Element Id`。
2. 将三项拼成字符串并执行 **Print String**。
3. 调用 `Set Data String`，将 `last.event` 设置为同一个字符串。
4. 调用 `Synchronize Models`，右侧 Last event 区域会立即更新。

可以按 `Name` 增加以下分支：

| Name | Blueprint 行为 |
| --- | --- |
| `FocusName` | `Focus Element By Id`，Element Id=`player-name` |
| `RefreshStats` | 调用 `Get Performance Stats`，把结果写入 `perf.*` 后执行 `Synchronize Models` |
| `ReloadView` | 调用 `Reload` |
| `ResetDemo` | 重新写入第三节的默认数据并执行 `Synchronize Models` |

性能数据显示建议：

```text
perf.update   <- Update Milliseconds
perf.submit   <- Submit Milliseconds
perf.javascript <- JavaScript Milliseconds
perf.jsTimers <- JavaScript Timers
perf.batches  <- Draw Batches
perf.vertices <- Vertices
perf.viewport <- ViewSize.X + " x " + ViewSize.Y
```

## 五、验证 JavaScript

Showcase 底部的 **JavaScript** 面板由外部 `showcase.js` 驱动，不需要 Blueprint 参与页面内部状态更新：

- 点击 **Increment**：计数器递增，并通过 `window.ue.emit` 发送 `JavaScriptCounterChanged`。
- 点击 **Run timer**：状态先变为等待，约 60 ms 后由 `setTimeout` 改为 `Timer completed`，并发送 `JavaScriptTimerCompleted`。
- 点击 **Add node**：使用 `document.createElement` 和 `appendChild` 动态添加蓝色节点，并发送 `JavaScriptNodeCreated`。

还可以在 Blueprint 中调用 **Execute JavaScript**：

```javascript
document.getElementById('js-status').textContent = 'Called from Blueprint';
ue.setData('last.event', 'JavaScript wrote UE data');
ue.getData('last.event');
```

节点返回 `Success`、`Result` 和 `Error`。把 **On JavaScript Error** 绑定到 `Print String`，可以看到页面脚本或 Execute JavaScript 的语法/运行错误。

## 六、添加到空关卡

打开 `L_H5UIPluginTest` 的 Level Blueprint，在 **Event BeginPlay** 后执行：

```text
Create Widget (Class=WBP_H5UIPluginTest, Owning Player=Get Player Controller)
-> Promote to variable TestWidget
-> Add to Viewport

Get Player Controller
-> Set Input Mode Game and UI (Widget to Focus 留空，Hide Cursor During Capture=false)
-> Set Show Mouse Cursor=true
```

不要使用 `Set Input Mode UI Only`，该模式会从 UE 层阻止输入继续进入游戏。进入 PIE 后，透明页面区域的鼠标会传给游戏；点击输入框后键盘交给 HTML，点击页面透明区域或其他非编辑控件后键盘重新传给游戏。`Edit pilot` 按钮通过 UE 事件调用 `Focus Element By Id` 后，也可以直接把键盘焦点移到 Display name。

## 七、当前页面覆盖范围

- `.html` 根节点兼容和外部 RCSS 加载。
- Flexbox、盒模型、选择器、字体、圆角、裁剪和滚动。
- 图片纹理、Hover/Active 状态、Transition、Transform 和 Keyframe Animation。
- 透明背景鼠标穿透，以及非编辑焦点状态下的键盘穿透。透明根层使用 `pointer-events: none`，实际 UI 表面使用 `pointer-events: auto`。
- Button、Text、Password、Textarea、Select、Checkbox、Radio、Range 和 Progress。
- `data-bind`、`data-ue-model`、`data-ue-event`、`data-ue-event-type`、`data-ue-payload`。
- 外部/内联经典 JavaScript、DOM 查询与修改、事件、Promise、Timer、Animation Frame 和 `window.ue` 桥接。
- `SetDataString`、`SetDataNumber`、`SetDataBoolean`、`SynchronizeModels` 和 `FocusElementById`。
- 本地 MP4、`autoplay`、`loop`、`muted` 和 UE Media Texture 渲染。
- `GetPerformanceStats`、`Reload`、`OnReadyForBindings`、`OnLoadFailed` 和 `OnUIEvent`。

`Load HTML String` 和 `Close` 属于视图生命周期 API，不适合放在页面自身内部。可以在 Level Blueprint 里绑定两个测试按键，分别调用它们；调用 `Close` 后再调用 `Load URL(h5ui://plugin/showcase.html)` 即可恢复。

## 故障检查

- 页面空白：检查 URL 是否完整写为 `h5ui://plugin/showcase.html`，并绑定 `On Load Failed` 打印错误。
- 无法键盘输入：确认 `Receive Input` 已开启，使用 `Set Input Mode Game and UI`，然后点击输入框取得焦点。
- 游戏收不到鼠标或键盘：不要使用 `Set Input Mode UI Only`；确认两个 Pass Through 属性已开启，并为透明 HTML 根层设置 `pointer-events: none`。
- 视频黑屏：确认 Win64 的 `WmfMedia` 插件可用，并检查 Output Log 中的 Media Framework 错误。
- 中文输入法没有候选窗：当前版本只接收 Slate 已提交的字符，尚未实现完整 IME Composition Adapter。
- 性能数据一直是 0：先点击 `Refresh stats`，并在对应 UE 事件分支中写入 `perf.*`。
- JavaScript 面板停在 Starting：确认 `Enable JavaScript` 已开启，并绑定 `On JavaScript Error` 查看具体错误。
