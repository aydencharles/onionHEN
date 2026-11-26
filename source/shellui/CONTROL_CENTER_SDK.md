# Control Center SDK（11.6 / NPXS40003）

当前 profile 把 11.6 里的 `accessibility`（协助工具）项转换成一个通用扩展宿主。该项仍进入原生 `controls/customControls` 数据流，因此"控制中心 → 自定义"的显示开关、拖动排序和 `controlCenterFunctionControlSettings` 用户持久化均继续由系统实现；但那一屏的行是由模块 193 的描述符另起一套渲染的，瓦片改写不会自动传播过去，见下方「控制中心『自定义/排序』页」。

宿主瓦片的弹窗被改写成**进程内 WebView**：`getMenu` 钩子不再返回系统菜单，而是返回一个指向本机回环 HTTP 服务的原生 `WebViewPS`。服务由 ShellUI 自己拉起，地址固定，页面由 OnionHEN 路由，因此不经过系统 URI 处理器，也不跳出控制中心。

## 注册接口

头文件：`source/shellui/include/control_center_sdk.hpp`

ShellUI 动态符号表只导出 `onion_cc_register` 和 `onion_cc_unregister`。注册 ID 只允许字母、数字、点、下划线和连字符；11.6 首版宿主要求标签不超过 11 字节、图标源不超过 33 字节、`web_url` 不超过 38 字节。`icon_source` 可以是系统图标 ID，也可以是 `/system_ex/vsh_asset/onionhen.png` 这类本地资源绝对路径；自定义 OnionHEN PNG 会同时写入普通态和焦点态图标源。动作可采用原生 URI 重定向、回调或两者并用。

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
  // 11.6 的 HBC 补丁不再读这个字段，见下方「enabled_by_default」小节。
  registration.enabled_by_default = true;

  uint8_t registry_id = 0;
  return onion_cc_register(&registration, &registry_id) ==
         static_cast<int>(onion::control_center::Result::Ok);
}
```

### `web_url` 与 `action_uri` 的分工

| 字段 | 走哪条路 | 效果 |
|---|---|---|
| `action_uri` | `onPress` → `LinkingPS.openURL` | 交给系统 URI 处理器，离开控制中心 |
| `web_url` | `getMenu` → 原生 `WebViewPS` | 在控制中心内的悬浮弹窗里加载网页 |

11.6 宿主渲染的组件用的是 **`getMenu`，不是 `onPress`**：`props.getMenu` 存在时弹窗容器才渲染 `PopupContainer`。所以想做应用内面板就填 `web_url`；两者可以同时填，互不干扰。`web_url` 为空时补丁回落到 `OnionCC?invoke=<registryId>`，该 URI 在 WebView 里渲染不出内容，只保留派发链路可用。

### 弹窗尺寸

弹窗的宽高**不来自 `getMenu` 的返回值**，而是来自瓦片自己的 props：

```
瓦片组件 props.menuWidth / props.menuHeight
  └─ FunctionControl（模块 73，fn #1285）把它转发给
       └─ PopupContainer（模块 1746，fn #19001）
            └─ usePopupStyle（模块 2742，fn #28065）
                 width  = menuWidth 未给 → DEFAULT_MENU_WIDTH(652)
                 height = menuHeight 给了才写进 style
                 └─ 与模块 796 的 popup 样式合并：
                    {minWidth: 652, maxWidth: 1536, minHeight: 216,
                     maxHeight: 810, position: 'absolute', bottom: 190}
```

模块 3478 原始的渲染体**两个都不传**，于是宽度走默认 652、高度直接塌到 `minHeight: 216`——里面的 WebView 又写着 `height: '100%'`，百分比没有确定高度的父节点可解析。这就是"弹窗有点矮"的成因。

补丁因此往瓦片 props 里补写 `menuHeight`（常量 `kMenuHeight116`，当前 **640**），与既有先例同路：模块 2765（music）传 `menuWidth: 1040`，broadcast 瓦片传 `menuWidth: 652, menuHeight: 292`。

> **宽度暂不可调。** 重建后的 fn #35901 是定长 80 字节，补 `menuHeight`（`LoadConstInt` 6 B + `PutNewOwnById` 5 B）后已用掉 78 字节，塞不下第二个属性。`menuWidth` / `menuHeight` 的 identifier 编号都超过 255，只能走 5 字节的 `PutNewOwnById`，没有短编码可省。要放宽只能让函数体越过自己的槽位，本补丁刻意不做。

## 内置 Web UI 服务

`onion_web_server.hpp` / `onion_web_server.cpp` 在 ShellUI 进程内起一个阻塞式 HTTP/1.1 服务：

- 端口固定 **9090**（9020/9021 是 ELF loader、9048 是 daemon 控制通道、9088 是 bootstrapper logger，因此 Web UI 排在它们之上），监听 `127.0.0.1`。
- 文档根目录 `/user/data/OnionHEN/web`，`index.html` 由内置页面写入：文件不存在时写，已存在但仍带本 build 的标记 `<!-- onionhen-web-ui -->` 时也重写（这样改版内置页面能覆盖到已经落盘过的旧副本），**手工替换过的页面不动**。
- 内置页面当前是极简的 `Hello From OnionHEN`：弹窗开了 `backgroundTransparencyEnabled`，页面画布保持 `transparent`，让弹窗自己的 `rgba(8, 10, 15, 1.0)` 底板透出来，而不是再刷一层背景。
- 两层查找：先精确匹配路由表，再落到静态文件；`/` 映射到 `/index.html`。拒绝含 `..` 的路径，静态层支持 `Range` 请求。
- 内置端点：`/api/status`、`/api/ping`。其他端点用 `onion::web_ui::add_json_route(path, handler)` 注册；在 `start()` 之前注册的会先缓存，启动时补注册。
- `onion::web_ui::url()` 返回 `http://127.0.0.1:9090/`，`prx.cpp` 在 `initialize_builtin_control()` **之前**调用 `onion::web_ui::start()`，保证瓦片被改写时服务已经在监听。

`accept` 循环跑在**裸 `pthread_create`** 上，不是 `std::thread`。libc++ 的 `std::thread` 启动时要经
`__thread_specific_ptr<__thread_struct>` → `pthread_setspecific`，这条路径在 ShellUI payload 内不可用：
第一次用它会直接把 SceShellUI 打成 SIGSEGV（详见 `References/ControlCenter/notes/RUNTIME-INJECTION.md` §15）。
服务对象因此是**故意泄漏**的进程级单例（线程 detached，无人 `join`），并且线程体里不许出现
异常和 `thread_local` —— 这两者同样会走到 `pthread_setspecific`。注册的 `RouteHandler` 也**不许抛异常**。

## 槽位与生命周期

- 原生设置记录固定 64 字节：2 字节版本、1 字节数量、1 字节保留区和最多 60 个项目字节；系统占用 `0..15`，SDK 保留 `16..59`。
- `/user/data/OnionHEN/control_center_registry.bin` 保存 ID 到 `registryId` 的映射，采用临时文件、`fsync`、原子 `rename` 和 FNV-1a 校验。
- 注销只移除活动 UI/动作，编号保留为墓碑；相同 ID 再注册会取得原编号，其他 ID 不会复用。
- 注册、更新和注销只排队重载；`NPXS40003` 的 `ReloadApp` 在下一个 ShellUI `OnRender` UI tick 执行。
- 点击图标产生 `OnionCC?invoke=<registryId>`，Boot hook 在 UI 线程查找注册快照；回调在锁外运行，URI 动作转交原始 Boot 实现。
- 补丁挂在 `ioctl(0xC0105203 /* DECRYPT_RNPS_BUNDLE */)` 上，内核每次都会重新解密出一份**全新的** bundle，因此同一缓冲区不会被二次改写，探针无需考虑幂等。

## Profile 闸门

补丁只接受 HBC v89、`fileLength=0x5e9350`、source hash `ff55cd8f3aea18a0b13363a640e56846e27148ab`，同时检查 `NPXS40003`、设置键和每个待改指令/字符串/函数头的原始字节（共 12 处探针）。补丁保持文件长度不变，完成后重算 HBC footer SHA-1。可通过 CMake 参数 `-DONIONHEN_SHELLUI_CONTROL_CENTER_PATCH=OFF` 关闭。

改动补丁后请跑一遍 `References/ControlCenter/tools/07_verify_control_center_patch.py`：它从 `control_center_patch.cpp` 源码里正则抽出常量和字节数组，在真实 `bundle/rnps-control-center.hbc` 上重放六段校验（探针命中、fn #35901 用 v89 汇编器逐字节复现、fn #36469 行 label memo 的长度/尾部/分支位移、**已不再改写的区域逐字节保持 stock**、fn #36469 重定位后的函数头、文件长度与 footer SHA-1），失败即非零退出。

### `enabled_by_default`

11.6 的宿主不读这个字段：`Registration::enabled_by_default` 只被复制进 `SnapshotEntry` 并写进持久化记录，**不影响 HBC**，也不改变瓦片可见性。字段保留是为了注册 ABI 与持久化记录的稳定，供将来的宿主 profile 使用。

> **历史。** 早先的修订版征用的是 Voice Agent 瓦片（registryId 5），那时 `enabled_by_default` 决定 `customControls[5]` 的描述符写成 `isDefault: true` 还是不写该键。换到 accessibility 宿主后，那个槽位**无条件在列**（模块 193 的工厂根本不为 `r29[7]`/`r23[7]` 生成 `isRevoked`），既不需要 feature-gate 改写也不需要 `isRevoked`/`isDefault` 改写，于是这半边连同 Voice Agent 的组件与 `getMenu` 改写一起被移除了。

## 为什么只有一个并发宿主

11.6 的瓦片列表由编译期常量 `new Array(16)` 构建，且字节码区没有空洞，**列表长度本身无法在不做字节码手术的前提下增长**。但这 16 个槽位里有 **5 个带 feature gate**：

| registryId | 瓦片 | 开关 |
|---|---|---|
| 2 | notifications | `enableNotificationsFC` |
| 3 | gamingLounge | `enableGameBaseFC` |
| 4 | music | `enableMusicFC` |
| 5 | voiceAgent | `enableVoiceAgentFC` |
| 6 | broadcast | `enableBroadcastFC` |

补丁征用的是 **registryId 7（`accessibility`）**，它**不在**这张表里：模块 193 的工厂函数为 `r29[7]` / `r23[7]` 生成描述符时**根本不写 `isRevoked` 字段**，所以这个槽位是无条件存在的——既不需要改 feature gate，也不需要改 `isRevoked`，持久化可见性 blob 也永远能解析到它。`kVisualHostCapacity116 = 1`。

> **为什么不用 voiceAgent（registryId 5）。** 零售固件把 `Voice Command Feature Flag` 报为 `disabled`（实机日志 `<118>[VnaUI I] Voice Command Feature Flag is [disabled]`），整个 VnaUI 子系统被编译掉了，该瓦片在本机上根本不出现——表现为"控制中心瓦片数量少一个"。feature gate 的字节可以改，但子系统缺失补不回来。补丁现在**完全不碰**这个槽位（模块 3482 的组件、它的 `getMenu`、模块 193 的两个 `Not` 与 `registryId` 常量都保持 stock）。

这是**本实现的覆盖面**，不是固件上限：另外 4 个 gate 同样可以在各自的描述符、图标块和 `getMenu` 闭包上套用同一套改法，把并发宿主提到 5。44 个持久化编号已经按最终格式保留，扩展时不需要改注册 ABI，也不会重排既有编号。

> **两个 `registryId` 不要混。** SDK 分配的扩展编号（16..59）只用于 `OnionCC?invoke=<id>` 派发和
> `control_center_registry.bin` 持久化；HBC 里宿主槽位的 `registryId` 字段必须保持**槽位自身的值**
> （7）。JS 侧 `1250.js` 会把可见性字节数组按 `(isDefault ? 0x80 : 0) | registryId` 解码，再以
> `customControls.find(c => c.registryId === registryId)` 反查；把这一字节改成扩展编号会让已存的
> blob 匹配失败，整条瓦片被从 `controls` 里丢掉（表现为控制中心少一个瓦片）。

> 真要突破 16 这个长度上限，需要把 `NewArray 16` 就地改成 `NewArray 17`（UInt16 操作数）并在函数尾部腾出约 11 字节追加一条构造指令。模块 193 的工厂函数（fn #2806）在构建完数组后立刻做 `r23.filter(Boolean)`，所以"扩到 17 但第 17 项没填上"不会崩——可以先只改长度验证，再补条目。

## 宿主改写点（accessibility）

| 位置 | 原始 | 改写后 |
|---|---|---|
| fn #35901（80 B） | `I18nPS.t('msgid_accessibility')` → `label`，`'accessibility'` → `icon` | 字符串 `#310` → `label`，`menuHeight = kMenuHeight116(640)`，`{uri: #13094}` → `icon` |
| fn #35903（689 B） | 模块 3479 的工厂（AccessibilityMenu） | 构造原生 `WebViewPS`，`source.uri = #13209` |
| fn #35902（54 B） | 旧 `getMenu` 闭包 | 征用为**自定义/排序页的行 label memo**（fn #36469 的新家），见下节 |

`#35901` 的 `CreateEnvironment` + `CreateClosure #35903` 序列逐字节保留：`#35901` 的 `environmentSize` 为 0，新建的环境只是一条 parent 链接，所以 `#35903` 里的 `GetEnvironment r0, 1` 依然落在模块工厂环境上，和旧的 `#35902` 完全一致（工厂槽 0 = `require(1)` 原生组件注册表，槽 2 = `require(3)`/`jsx`）。

`#35901` 重建后仍是 80 字节：78 字节代码 + 1 条 `LoadConstUndefined`（`74 00`）填充。写入边界正好停在 0x59bb70，也就是死掉的 `#35902` 起点，两者不重叠。

模块 3479 在全 bundle 里只有 `#35902` 一个引用，闭包一改指向它就是死代码，689 字节可以整块征用。

`WebViewPS` 不是模块，而是原生组件注册表（模块 1 = `require(149)('NiSiE5q6')`）里的名字；全 bundle 只有 fn #33252 引用过一次，改写后的 prop 集合就是照那一处抄的。

**保持 stock 的区域。** 补丁不再碰 Voice Agent 半边：模块 193 的 `+0x2fb`/`+0x49e` 两条 `Not`、`+0x4b1` 的属性名、`+0x1d18de` 的 `registryId` 常量、模块 3482 的组件函数（fn #35986/#35992/#35998）及其 `getMenu`（fn #36000）全部逐字节不变。`07_verify_control_center_patch.py` 的第 3 段校验就是逐字节比对这 8 个区域。

### 复用的字符串槽

`#310`（label）、`#13094`（icon 载体）、`#13209`（WebView URI）由同一份改写逻辑写入。写窗口的真实安全边界按相邻条目算出，并已在实机 bundle 上验证不覆盖任何活跃字符串：

| 槽 | 存储 | 安全容量 | 紧邻的活跃条目 |
|---|---|---|---|
| `#310`/`#311` | `0x0d599b` | 24 | `#15035` `onGestureFinish` @ `0x0d59b3`（fn #18491 引用，与本节无关） |
| `#13094` | `0x0e6480` | 33 | 覆盖 `#2902`（唯一引用是 fn #35998，在模块 3482 内，永不渲染）与 `#16885`（已搬迁） |
| `#13209` | `0x137a28` | 38 | `#13210` `turn off:ps vr` @ `0x137a4e`（无字节码引用） |

`#16885`（`sender`）与 `#28944`（`tooltipTitleProps`）是**活跃标识符**——分别被 fn #11770/#11771/#11897/#17901/#17909/#23091 与 #11770/#11771 使用，与控制中心无关——所以 icon/URI 写入前先把它们搬迁进死串 `#9487`（`go to voice command settings`）。这段搬迁**必须保留**，它和 Voice Agent 半边是两回事。

## 控制中心「自定义/排序」页

主条带的瓦片改写**不会**传播到「控制中心 → 自定义」那一屏。那一屏不渲染模块 3478 的瓦片，而是从模块 193 的描述符另起一套行：

```
模块 193   defaultOrderControls = descriptors.map(d => ({id, label, description}))
           ← 只有 label / description，连 iconId 都不带
模块 3522   getControlInfo(id)    ← iconId 在运行时按 id 反查
模块 3525   整屏 + 表头            ← 渲染每行的 label / description
模块 3508   单个格
模块 3515   格子上的按钮           ← 渲染图标
```

所以改写瓦片之后，排序页里那一行仍然显示 stock 的协助工具条目。两半都走这个 bundle 无法用字符串手术改掉的 native：

- **label**：每行的 label 都是 `I18nPS.t(<描述符 label>)`，编译进 fn #36469（模块 3525）：

  ```
  r0 = _closure2_slot0;  return I18nPS.t(r0)
  ```

  `#12801`（`msgid_accessibility`）是协助工具描述符自己的 label 载体，它的存储与一个活跃标识符重叠，**不能**照 `#310` 的做法覆写；而且 bundle 里没有任何空闲槽能装下"`t()` 会翻译成 `registration.label`"的字符串。

- **icon**：行把 `iconId` 交给 `IconPS.getImageSource(name, 56, 56)`（fn #36398）和 PUI Button 的 `icon` 属性（fn #36397）。本 bundle 里 39 个 `getImageSource` 实参**清一色是裸系统图标名，无一是路径**，原生库里也没有任何 `/system_ex/vsh_asset` 查表逻辑，因此没有任何 string id 能让这一行解析到 OnionHEN 的 PNG。要改图标必须带 `id === 'accessibility'` 守卫重写**共用的** 1130 字节按钮组件（模块 3515），当前**有意不做**。

**label 半边已修。** fn #36469 在整个 bundle 里只有一处引用（模块 3525 里的一条 `CreateClosure`），而它自己 31 字节的槽塞不下守卫，所以函数体被搬进 accessibility 宿主本来就已经释放的 **54 字节 `#35902` 槽**，只重指函数头：

```
stock（31 B）                       改写后（52 B，位于 0x59bb70）
  GetEnvironment r0, 1                GetEnvironment r0, 0
  env[1][2].I18nPS.t(env[0][0])       LoadFromEnvironment r0, r0, 0      ; label
  Ret                                 LoadConstString r1, #12801
                                      JStrictNotEqual r0, r1, +10        ; 不是协助工具行就跳尾部
                                      LoadConstString r0, #310           ; registration.label
                                      Ret r0
                                      ── 尾部 = stock 的 31 字节，逐字节相同 ──
```

- `env[0]` 是闭包环境，模块 3525 让 label 与 description 两个 memo **共用**它：槽 0 是 `label`、槽 1 是 `description`，对每一行都一样。
- 比较对象是描述符自己的 label id（`#12801`，模块 193 为 registryId 7 生成的就是它），所以**不需要动描述符**。
- 只有协助工具那一行被短路到 `#310`；其余 15 行和合成的「恢复默认」项继续走各自的 `t()`。
- 因为短路分支直接返回字符串而**不经过 `t()`**，这里不依赖 `t()` 对未登记键的行为。
- `JStrictNotEqual` 编码为 `[op, Addr8, Reg8, Reg8]`，两寄存器**不严格相等**时跳转，目标 = 指令自身地址 + Addr8；分支在 offset 11、尾部在 21，故位移为 10。这两点都在 hasm 清单里逐条核过。
- 函数头只改 offset 与 bytecodeSize，`paramCount`、`frameSize` 和 `w1` 高位的函数标志位原样保留（`0x2aaa52` → `0x59bb70`，size 31 → 52）。

**描述符的 description 未改**：它是另一个 memo（fn #36470，读槽 1），要同样处理需要第二个 ≥52 字节的死槽，当前没做，因此排序页那一行的副标题仍是协助工具的描述。同样，图标仍是协助工具图标。

## 与 HomeUI 顶部导航补丁的关系

两者共用同一个 `SetIconSource` detour：`ReactButton` 在聚焦时读 `invertedIconSource`，而本地资源图标只会填充 `iconSource`，因此需要把 OnionHEN 的图标同时镜像到两个属性。该 hook 现在位于独立的 `onion_icon_source_hook.cpp`，由 `SHELLUI_HOMEUI_TOP_NAV_PATCH` **或** `SHELLUI_CONTROL_CENTER_PATCH` 任一个开启时生效，避免单独关掉其中一个时另一个的焦点图标变空白。
