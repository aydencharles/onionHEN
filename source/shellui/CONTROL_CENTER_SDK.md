# Control Center SDK（11.6 / NPXS40003）

当前 profile 把 11.6 中受 feature flag 控制的 `voice-agent` 项转换成一个通用扩展宿主。该项仍进入原生 `controls/customControls` 数据流，因此"控制中心 → 自定义"的显示开关、拖动排序和 `controlCenterFunctionControlSettings` 用户持久化均继续由系统实现。

## 注册接口

头文件：`source/shellui/include/control_center_sdk.hpp`

ShellUI 动态符号表只导出 `onion_cc_register` 和 `onion_cc_unregister`。注册 ID 只允许字母、数字、点、下划线和连字符；11.6 首版宿主要求标签不超过 11 字节、图标源不超过 33 字节。`icon_source` 可以是系统图标 ID，也可以是 `/system_ex/vsh_asset/onionhen.png` 这类本地资源绝对路径；自定义 OnionHEN PNG 会同时写入普通态和焦点态图标源。动作可采用原生 URI 重定向、回调或两者并用。

```cpp
#include "control_center_sdk.hpp"

static void on_control(uint8_t registry_id, const char *id, void *context) {
  auto *counter = static_cast<unsigned *>(context);
  ++*counter;
  (void)registry_id;
  (void)id;
}

bool register_sample_control() {
  static unsigned invocation_count = 0;

  // 11.6 当前只有一个扩展宿主；移除内置项后其 registryId 仍保留为墓碑。
  (void)onion_cc_unregister("onionhen.toolbox");

  onion_cc_registration registration{};
  registration.struct_size = sizeof(registration);
  registration.id = "sample.counter";
  registration.label = "Counter";
  registration.icon_source = "/system_ex/vsh_asset/onionhen.png";
  registration.callback = &on_control;
  registration.callback_context = &invocation_count;
  registration.enabled_by_default = true;

  uint8_t registry_id = 0;
  return onion_cc_register(&registration, &registry_id) ==
         static_cast<int>(onion::control_center::Result::Ok);
}
```

## 槽位与生命周期

- 原生设置记录固定 64 字节：2 字节版本、1 字节数量、1 字节保留区和最多 60 个项目字节；系统占用 `0..15`，SDK 保留 `16..59`。
- `/user/data/OnionHEN/control_center_registry.bin` 保存 ID 到 `registryId` 的映射，采用临时文件、`fsync`、原子 `rename` 和 FNV-1a 校验。
- 注销只移除活动 UI/动作，编号保留为墓碑；相同 ID 再注册会取得原编号，其他 ID 不会复用。
- 注册、更新和注销只排队重载；`NPXS40003` 的 `ReloadApp` 在下一个 ShellUI `OnRender` UI tick 执行。
- 点击图标产生 `OnionCC?invoke=<registryId>`，Boot hook 在 UI 线程查找注册快照；回调在锁外运行，URI 动作转交原始 Boot 实现。

## Profile 闸门

补丁只接受 HBC v89、`fileLength=0x5e9350`、source hash `ff55cd8f3aea18a0b13363a640e56846e27148ab`，同时检查 `NPXS40003`、设置键和每个待改指令/字符串的原始字节。补丁保持文件长度不变，完成后重算 HBC footer SHA-1。可通过 CMake 参数 `-DONIONHEN_SHELLUI_CONTROL_CENTER_PATCH=OFF` 关闭。

`enabled_by_default` 决定自定义描述符写成 `isDefault: true`（进默认集合，无需用户手动添加）还是不写该键（条目可见但要用户在"自定义"里加入）。两者都不再写 `isRevoked`，因此宿主永不被吊销。

## 为什么只有一个并发宿主

11.6 的瓦片列表由编译期常量 `new Array(16)` 构建，且字节码区没有空洞，**列表长度本身无法在不做字节码手术的前提下增长**。但这 16 个槽位里有 **5 个带 feature gate**：

| registryId | 瓦片 | 开关 |
|---|---|---|
| 2 | notifications | `enableNotificationsFC` |
| 3 | gamingLounge | `enableGameBaseFC` |
| 4 | music | `enableMusicFC` |
| **5** | **voiceAgent** | `enableVoiceAgentFC` |
| 6 | broadcast | `enableBroadcastFC` |

其余 11 个没有 `isRevoked` 字段，是永远可用的系统功能，动不得。

补丁目前只征用 registryId 5（`voiceAgent`，零售机默认关闭，零牺牲），所以 `kVisualHostCapacity116 = 1`。这是**本实现的覆盖面**，不是固件上限：另外 4 个 gate 同样可以在各自的描述符、图标块和动作体上套用同一套改法，把并发宿主提到 5。44 个持久化编号已经按最终格式保留，扩展时不需要改注册 ABI，也不会重排既有编号。

> 真要突破 16 这个长度上限，需要把 `NewArray 16` 就地改成 `NewArray 17`（UInt16 操作数）并在函数尾部腾出约 11 字节追加一条构造指令。模块 193 的工厂函数（fn #2806）在构建完数组后立刻做 `r23.filter(Boolean)`，所以"扩到 17 但第 17 项没填上"不会崩——可以先只改长度验证，再补条目。

## 与 HomeUI 顶部导航补丁的关系

两者共用同一个 `SetIconSource` detour：`ReactButton` 在聚焦时读 `invertedIconSource`，而本地资源图标只会填充 `iconSource`，因此需要把 OnionHEN 的图标同时镜像到两个属性。该 hook 现在位于独立的 `onion_icon_source_hook.cpp`，由 `SHELLUI_HOMEUI_TOP_NAV_PATCH` **或** `SHELLUI_CONTROL_CENTER_PATCH` 任一个开启时生效，避免单独关掉其中一个时另一个的焦点图标变空白。
