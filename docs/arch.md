# OnionHEN 架构分析

OnionHEN 是 PS5 的 All-in-One Homebrew Enabler，基于 **etaHEN**（LightningMods）GPLv3 源码的社区续作，定位类似 PS4 上的 GoldHEN。

| 维度 | 说明 |
|------|------|
| 目标平台 | PS5（Prospero），目标三元组 `x86_64-sie-ps5` |
| 定位 | 内核 exploit 之后加载的 **payload 套件**：提权、服务、Toolbox UI、fPKG/fSELF 支持等 |
| 许可证 | GPLv3 |
| 构建 | CMake + Ninja + `PS5_PAYLOAD_SDK`（clang） |

**运行时前置条件：** 必须先有 **外部 elfldr（端口 9021）** 完成首跳。bootstrapper 启动后会拉起 OnionHEN 自己的私有 **9020** `onion_elfldr.elf`。用户 Payload 只能走 9020，并且必须取得加载器返回的精确 PID；失败时不做进程快照、不自动重试、也不回退 9021。9021 仅作为首次引导和 9020 恢复通道。

---

## 1. 总体架构

### 1.1 加载链路

```
[WebKit / IPV6 等 Kernel Exploit]
        │
        ▼
  OnionHEN.elf  (unpacker)
        │  LZMA 解压内嵌 bootstrapper
        │  经 9021 送出 bootstrapper.elf
        ▼
  bootstrapper.elf
        │  1. 提权 (elfldr_raise_privileges)
        │  2. remount /system、/system_ex
        │  3. unmount /update（阻止系统更新）
        │  4. 经 9021 启动内嵌 onion_elfldr.elf（监听 9020）
        │  5. 将内嵌 ELF 字节经 9020 顺序启动：util → kstuff → daemon
        │  6. 加载 /data/OnionHEN/payloads/ 下 .elf
        ▼
┌───────────────┬────────────────┬──────────────────┐
│  util.elf     │  kstuff.elf    │  daemon.elf      │
│  工具服务守护 │  fself / fpkg  │  核心守护进程    │
│  Cheats      │  内核补丁      │  ShellUI 注入    │
└───────┬───────┴───────┬────────┴────────┬─────────┘
        │               │                 │
        │               │                 ▼
        │               │         inject shellui.elf
        │               │         → SceShellUI (Toolbox)
        │               │                 │
        │               └─────────────────┼── 共用 ptrace / ShellUI
        ▼                                 ▼
  Unix IPC                       ShellUI monitor bar
```

**启动顺序有意串行化：** util → kstuff → daemon。

- util 先提供网络/IPC 等服务
- kstuff 需先完成 ShellUI 补丁
- daemon 再注入 Toolbox

若并行 ptrace 同一进程，容易导致 Toolbox 超时或崩溃。

### 1.2 构建嵌入关系

```
shellui.elf ───────┐
util.elf ──────────┼──► 嵌入 daemon.elf ──┐
onion_elfldr.elf ──┘                       │
                     util.elf ─────────────┼──► 嵌入 bootstrapper.elf
                     kstuff.elf ───────────┤         │
                     onion_elfldr.elf ─────┘         ▼ LZMA
                                               bootstrapper.elf.lzma
                                              │
                                              ▼ 嵌入
                                         OnionHEN.elf  (最终用户 payload)
```

构建阶段（`scripts/build.sh`）：

1. CMake configure
2. 内部库 + `shellui`
3. sync vendor（kstuff）
4. `daemon` + `util`
5. `bootstrapper`（再 lzma 压缩）
6. `unpacker` → `OnionHEN.elf`

> **Payload only:** OnionHEN no longer supports `.plugin` packages. Place
> bare `.elf` under `/data/OnionHEN/payloads/` (or USB `.../payloads/`).

产物落在仓库根目录 `build/bin/`（静态库 `build/lib/`；CMake 树 `build/`）。

### 1.3 仓库布局

```
OnionHEN/
├── assets/           # 图标等
├── docs/             # 技术文档（含本文）
├── scripts/          # 构建与主机侧工具
├── source/           # CMake 主工程
│   ├── bootstrapper/ # 启动器
│   ├── daemon/       # 核心守护
│   ├── util/         # 工具守护（金手指、IPC 等）
│   ├── shellui/      # Toolbox
│   ├── unpacker/     # 最终 OnionHEN.elf
│   ├── libhijacker/ libNineS/ libNidResolver/
│   ├── libonion_*    # 共享：ipc/settings/proc/platform/ready/detour/payload/elfldr
│   ├── common/       # 项目共享的底层实现
│   ├── include/      # 公共头文件
│   └── platform/ps5/stubs/ # PS5 系统库链接 stub
├── third_party/      # 外部源码、预编译依赖与 git submodule
├── tools/            # NID stub 等仓库侧生成工具
└── .cache/           # 下载缓存（如 kstuff.elf，不提交）
```

---

## 2. 模块职责

### 2.1 `unpacker` → `OnionHEN.elf`

- 用户侧最终 payload
- 内嵌 **LZMA 压缩** 的 `bootstrapper.elf`
- 解压后通过 **9021** 发送并执行
- 依赖 7zip-sdk 的 LZMA 解码实现

### 2.2 `bootstrapper` → `bootstrapper.elf`（再压成 `.lzma`）

- 提权、分区 remount、阻止更新
- 以 `.incbin` 嵌入 `onion_elfldr.elf`、`daemon.elf`、`util.elf`、`kstuff.elf` 与图标资源
- 先用外部 9021 启动内置 `onion_elfldr.elf`，再通过 9020 启动 util / daemon / kstuff
- 扫描并自动启动带 `.auto_start` 标记的用户 Payload ELF
- 可选日志端口 **9088**

关键启动策略：

1. 检查 `127.0.0.1:9021` 上的外部 elfldr（首跳必需）
2. 启动内置 `onion_elfldr.elf`，并通过握手确认 `127.0.0.1:9020`
3. 顺序发送内部 ELF 字节：util → kstuff → daemon；9020 是正常路径，9021 仅承担引导/恢复职责
4. 各子 ELF 启动后把主线程名设为稳定进程名（`onion_util.elf` / `kstuff.elf` / `onion_daemon.elf`）
5. 仅在 9020 健康时加载 `/data/OnionHEN/payloads/` 下带 `.auto_start` 的 `.elf`；必须取得精确 PID，否则该 Payload 直接失败

可用 Toolbox「启动时自动加载 Kstuff」（默认开启）关闭，或放 `/mnt/usb0/no_kstuff` 跳过 kstuff。

### 2.3 `daemon` → `daemon.elf`（Critical 守护进程）

- 内嵌 `shellui.elf`、`util.elf` 与 `onion_elfldr.elf`
- 经 **libNineS** 将 Toolbox 注入 `SceShellUI`
- 按依赖顺序监视私有 9020 与 util：9020 异常时先通过外部 9021 恢复加载器，再通过 9020 恢复 util
- 9020 在同步加载 Payload 时发布 busy 标记，避免健康检查误杀；超过有界宽限期仍未恢复则按卡死处理
- Unix socket：`/system_tmp/onionhen/ipc/crit_service`
- IPC 前缀 `0x9000000`（`BREW_*`）

主要能力：

- 重挂载、拷贝/删除目录、stat、chmod
- 启用 Toolbox
- 风扇阈值调整
- 强制杀进程 / 杀守护进程

### 2.4 `util` → `util.elf`（Utility 守护进程）

- 与 critical 分离，承载网络/IO 与较重业务，提高稳定性
- Unix socket：`/system_tmp/onionhen/ipc/util_service`
- IPC 前缀 `0x8000000`（`BREW_UTIL_*`）

| 服务 | 端口 / 入口 | 说明 |
|------|-------------|------|
| Cheats | IPC | flat-file cheat engine（flat `TITLE_VERSION.ext` + mdbg/kdirect）；详见 [util_arch](util_arch/) |
| ShellCore / ShellUI 补丁 | — | 休息模式恢复、toolbox 激活等 |

> **已移除：** FTP（1337）、Klog 网络服务（9081）、Legacy CMD（9028）。  
> 注意：代码里仍使用 `ps5/klog.h` 的 `klog_printf` / `klog_puts`，那是内核日志 API，不是 9081 服务。

### 2.5 `shellui` → `shellui.elf`（Toolbox UI）

- 注入 `SceShellUI` 的 Mono 层
- 替换/扩展 Debug Settings 风格菜单
- 工具箱与子页菜单 XML 均由 `ps5ui::Page` 动态组装（`toolbox_xml.cpp`）

主要菜单能力：

- 内容安装与管理（系统 PkgInstaller UI、附加内容管理）
- Payload 与内核组件（Payload ELF、kstuff 管理）
- 游戏辅助（金手指引擎、OnionHEN 游戏选项）
- 监控与显示（ShellUI 监控条、Title ID）
- 账号激活
- 系统与硬件（风扇、休息模式、外置 HDD、BD 激活）
- 操作偏好（工具箱语言、手柄快捷键）
- 高级调试 / 关于

注入路径详见 [shellui-injection.md](shellui-injection.md)。

HomeUI 顶部导航和 Settings Debug Settings 入口按固件 profile 覆盖 2.30–12.70；11.00 起 Settings 走 `debug_settings_old`。

游戏内 `fps_elf` 注入已移除。监控信息走 ShellUI 监控条（CPU / GPU / RAM / IP）。

### 2.6 内部静态库

| 库 | 作用 |
|----|------|
| **libhijacker** | 进程劫持、kernel R/W、spawner、调试、通知；依赖 NidResolver |
| **libonion_elfldr** | **唯一** ptrace/`pt_*` + inject 侧 `elfldr_load` / `elfldr_payload_args` / 内置 loader 侧 `elfldr_spawn` / `elfldr_read` / `elfldr_raise_privileges`；**authid 不在每条 ptrace 上翻转**（由 inject 入口一次提权） |
| **libNineS** | 进程注入编排（`inject_elf` / stager）；**pt/elfldr 实现来自 libonion_elfldr** |
| **libNidResolver** | PS5 模块 NID 解析（SHA1 等） |
| **libonion_ipc** | **客户端**（injectee 双单例）+ **服务端传输环**（`ipc_server`：listen/accept/loop/reply）；daemon/util/shellui 共用 |
| **libonion_settings** | 统一 `config.ini` schema；各进程以 `onion::Settings g_settings` 为真相源 |
| **libonion_detour** | 共享 Detour + hde64 钩子栈 |
| **libonion_proc** | 共享 proc/ucred（allproc 遍历、dynlib、authid）+ **sysctl 进程查询**（`find_pid` / `onion_find_pid_ex` / `isProcessAlive`）；daemon / util / shellui / bootstrapper 共用 |
| **libonion_platform** | 平台叶子：`if_exists` / `touch_file` / `rmtree`、`OnionHEN_log`（可配置 tag/路径）、`onion_notify`；修一处全树受益 |
| **libonion_ready** | 跨进程 ready/runtime 标记（`/system_tmp/onionhen/ready/<name>` + wait/timeout）；替代固定 sleep 与 ad-hoc 文件旗 |
| **onion/lnc.h** | 共享 LNC 启动 ABI（`LncAppParam` / `Flag` / 错误码）；daemon `launcher.hpp` 仅为 shim |
| **libNineS** | ptrace 注入编排；**proc/ucred → libonion_proc**；**pt/elfldr → libonion_elfldr** |

#### Daemon 模块（加深后）

| 模块 | 职责 |
|------|------|
| **msg.cpp** | 仅 `IPC_loop` + transport 胶水 |
| **ipc_handle.cpp** | crit 命令表分发 |
| **daemon_inject.cpp** | toolbox 注入 |
| **daemon_settings.cpp** | LoadSettings + mtime 缓存 |
| **daemon_fs.cpp** | remount / chmod / test_sb / reply / fan / ForceKill / pid 查找 |

#### ShellUI 模块（加深后）

| 模块 | 职责 |
|------|------|
| **ipc.hpp** | 仅 `onion/ipc_client`（**不**拉 HookedFuncs） |
| **shellui_types.hpp** | 枚举 / 插件 / overlay / settings 类型 |
| **hooked_funcs.hpp** | Mono hooks + UI API（include types） |
| **mono_runtime** | Mono 反射 / 属性读写 / 类查找 |
| **toolbox_xml** | `generate_*_xml` / `generate_toolbox_xml` 菜单 XML（`ps5ui::Page`） |
| **settings_ui** | `settings_commit` / SaveSettings 等 UI 侧设置 |
| **shellui_notify / shellui_proc** | UI 用 `notify(const char*)` 与进程/USB 辅助 |
| **hook_onpress + onpress_*** | 页面所有权策略 + 表驱动 OnPress：原生 Settings 页面纯透传，Toolbox 按 root / cheats / payloads / account / plapps 拆域 |

#### Ready / runtime flags 协议

| 标记名 | 发布方 | 等待方 / 用途 |
|--------|--------|----------------|
| `util` | util 在 IPC 线程启动后 | bootstrapper 启动 util 之后 |
| `kstuff` | bootstrapper 在 mprotect 成功后 | daemon 注入 toolbox 前 |
| `daemon` | daemon 在 IPC 线程启动后 | bootstrapper 启动 daemon 之后 |
| `toolbox` | shellui 注入完成后，内容为自身 PID | daemon 按当前 SceShellUI PID 判断跳过或重注入 |
| `fps_overlay` | shellui（监控条开关） | ShellUI 监控条状态 |
| `util_booted` | util 冷启动完成后 | rest-mode / toolbox 延迟路径（替代 `util_first_boot`） |

#### IPC 分层（加深后）

```
msg.hpp          协议：路径、magic、命令枚举、IPCMessage
ipc_server.*     传输：Unix listen/accept/recv/send + 线程环 + reply
handleIPC (进程) 业务：crit 与 util 各自命令表
ipc_client.*     注入侧客户端
```

#### 配置分层

```
onion::Settings       持久化 schema（双路径 twin：primary + shellui）
onion::SettingsStore  进程内线程安全真相源（mutex + snapshot/store/update）
g_settings            daemon/util：SettingsStore；shellui：Settings（UI 线程）
LoadSettings()        统一 bool 契约：刷新 store；缺文件用默认并成功
mtime 门控            settings_config_newest_mtime — 任一 twin 更新即失效
运行时原子量          util rest-mode / network 标志等
OverlayLayout         仅 shellui：由 overlay_pos 派生的像素坐标
```

### 2.8 主机工具（`scripts/`）

| 脚本 | 用途 |
|------|------|
| `build.sh` | 一键构建流水线 |
| `sync_dependencies.sh` | 同步 kstuff 等外部依赖 |
| `send_elf.py` / `send_payload.ps1` | 网络发送 ELF |
| `launch.py` | IPC 控制应用 |
| `shutdown_onion.py` / `kill_daemon.py` | 从 PC 关栈：util → 重启 ShellUI → daemon 退出；**不杀 kstuff**（TCP **9048**） |
| `daemon_log.py` | 守护进程日志 |
| `ps5_cmake.sh` | Prospero CMake 封装 |
| `pack_bootstrapper.sh` | bootstrapper 尺寸记录 + lzma 打包 |

---

## 3. IPC 与通信模型

```
shellui / homebrew
        │ Unix domain socket
        ├─► /system_tmp/onionhen/ipc/crit_service  (daemon, 0x9xxxxxxx)
        └─► /system_tmp/onionhen/ipc/util_service  (util,   0x8xxxxxxx)

homebrew (app jailbreak)
        └─► sandbox file  …/download0/etahen_jailbreak|onionhen_jailbreak
             (SceSysCore 进程生命周期事件 + 沙盒目录 vnode 事件 + 白名单 TID)
```

### 3.1 消息格式

定义见 `source/include/msg.hpp`：

```cpp
struct IPCMessage {
  int magic = 0xDEADBABE;
  enum DaemonCommands cmd;
  int error = 0;
  char msg[0x1000];
};
```

### 3.2 主要命令族

**Critical（daemon，约 `0x9000000`）：**

- `BREW_REMOUNT_FOLDER` / `BREW_STAT_CMD` / `BREW_COPY_*` / `BREW_DELETE_DIR`
- `BREW_CHMOD_DIR` / `BREW_ENABLE_TOOLBOX`
- `BREW_ADJUST_FAN_SPEED`
- `BREW_KILL_DAEMON` / `BREW_FORCE_KILL_PID`

**Util（约 `0x8000000`）：**

- `BREW_UTIL_LAUNCH_PAYLOAD`
- `BREW_UTIL_GET_GAME_VER` / `BREW_UTIL_GET_GAME_CHEAT` / `BREW_UTIL_TOGGLE_CHEAT`
- `BREW_UTIL_DOWNLOAD_CHEATS` / `BREW_UTIL_CHEAT_SYNC_STATUS`（git catalog 同步；`RELOAD_CHEATS` 已移除，热重载靠文件签名）
- `BREW_UTIL_UNUSED_DOWNLOAD_KSTUFF`
- `BREW_UTIL_SHELLUI_ON_STANDBY`

**已废弃但保留序号（兼容旧客户端）：**

- `BREW_UNUSED_ACTIVATE_DUMPER`（原 `ACTIVATE_DUMPER`，固定保留 `0x9000004`，避免 Itemzflow ABI 后续命令错位）
- `BREW_UTIL_UNUSED_LEGACY_CMD_SERVER`（原 TOGGLE_LEGACY_CMD_SERVER / TCP 9028 已移除）
- `BREW_UNUSED_DECRYPT_DIR`（原 DECRYPT_DIR，SELF 目录解密已移除）
- `BREW_UNUSED_TESTKIT_CHECK`（原 TESTKIT_CHECK；客户端改为本地探测）
- `BREW_UTIL_TOGGLE_FTP`（保留原 TOGGLE_FTP 序号；通过 Toolbox 实时启停 ftpsrv）
- `BREW_UTIL_UNUSED_KLOG`（原 TOGGLE_KLOG）
- `BREW_UTIL_LAUNCH_ELFLDR`（旧手动启动命令；内置 9020 由 bootstrapper 管理）

### 3.3 运行时路径

| 路径 | 用途 |
|------|------|
| `/data/OnionHEN/` | 数据根目录 |
| `/data/OnionHEN/config.ini` | 配置 |
| `/data/OnionHEN/OnionHEN.log` | 日志 |
| `/data/OnionHEN/payloads/` | payload `.elf`（唯一扩展包格式；启动时 stage 到同目录） |
| `/system_tmp/onionhen/ipc/*` | Unix IPC socket |
| `/system_tmp/onionhen/ready/<name>` | ready/runtime 标记；`toolbox` 内容为 SceShellUI PID |
| `/system_tmp/onionhen/pid/<key>.PID` | payload/内部服务 PID 状态 |
| `/system_tmp/onionhen/app_launched` | ShellUI LaunchApp 返回值 |
| `/system_tmp/onionhen/patch_plugin` | LaunchApp patch gate（外部标记；ShellUI 只读取） |

### 3.4 Itemzflow 兼容状态

- `ITEM00001` 默认位于 `app_jailbreak.exact_title_ids`，避免 Loader 因未进入 jailbreak 流程而等待 30 秒；精确 Title ID 与前缀白名单均可通过 `config.ini` 覆盖。
- Critical IPC 保留原版 `BREW_ACTIVATE_DUMPER=0x9000004` 的废弃槽位，确保 Itemzflow 使用的后续命令数值不发生错位。
- **已知问题：**已发布的 ItemzCore 固定连接 `/system_tmp/etaHEN_crit_service`，当前 daemon 仅监听 `/system_tmp/onionhen/ipc/crit_service`，因此 Loader 通过后仍可能出现最长一分钟的 daemon 等待。后续应增加独立受监控的旧路径监听器，并与当前路径共用 `handleIPC`；不得直接替换当前路径，也不应依赖 Unix socket 符号链接。

---

## 4. 功能清单

### 4.1 系统 / HEN 核心

- 提权与分区 remount
- 阻止系统更新（unmount `/update`）
- **kstuff**：fself / fpkg 相关内核能力（通常 ≥ 3.00）
- App jailbreak（按设置启停；SceSysCore 生命周期 + 沙盒 vnode 事件 + 白名单 TID，无常驻轮询）
- 双守护进程架构（util 可被 daemon 拉起）

### 4.2 用户界面（Toolbox）

- Debug Settings 替代菜单
- 内容安装与管理、Payload 与内核组件
- 游戏辅助、监控与显示
- 账号激活
- 系统与硬件、操作偏好
- 高级调试、关于与支持
- 金手指（flat 文件 + mdbg/kdirect）

### 4.3 网络服务

- 首跳依赖外部 **9021 elfldr**；它同时是私有 9020 的恢复根。用户 Payload 严格使用内置 **9020 onion_elfldr**，不回退 9021

### 4.4 扩展

- 自定义插件（兼容 [etaHEN SDK](https://github.com/LightningMods/etaHEN-SDK)）
- `config.ini` 驱动的开关（overlay、快捷键等）

### 4.5 已移除

| 能力 | 说明 |
|------|------|
| 内嵌 9021 elfldr | 改为内置私有 9020 loader；9021 只作为外部首次引导 / 9020 恢复通道 |
| FTP 1337 | 服务与 Toolbox 开关已移除 |
| Legacy CMD 9028 | util TCP hijacker 协议与 Toolbox 开关已移除；app JB 仅 FIFO |
| Klog server 9081 | 服务与 Toolbox 开关已移除 |
| ps5debug / app-dumper | 不再内嵌 |
| Byepervisor / hen.bin | 1.xx–2.xx HV 路径不打包 |
| Discord RPC | 已移除 |
| libSelfDecryptor | 已移除；SELF 目录解密 IPC 亦已移除 |
| 沙盒内 `/data` 可见性补丁 | 仅路径可见，非越狱，已移除 |

---

## 5. 依赖组件

### 5.1 构建与运行时（外部）

| 依赖 | 说明 |
|------|------|
| **ps5-payload-sdk** (`PS5_PAYLOAD_SDK`) | Prospero 工具链、`prospero-cmake`、系统头文件 |
| **clang** | 目标 `x86_64-sie-ps5` |
| **elfldr @ 9021** | 首跳必需，不随 OnionHEN 打包 |
| **onion_elfldr @ 9020** | OnionHEN 内置私有运行时 loader；由 bootstrapper 拉起 |
| Kernel exploit | 如 IPV6 等，用于先获得代码执行 |

### 5.2 第三方依赖（`third_party/`）

| 组件 | 上游 | 角色 |
|------|------|------|
| **kstuff-lite** | [EchoStretch/kstuff-lite](https://github.com/EchoStretch/kstuff-lite) | 提供 `kstuff.elf`，同步进 bootstrapper |

```bash
git submodule update --init --recursive
./scripts/sync_dependencies.sh
```

### 5.3 树内第三方源码

| 库 | 用途 |
|----|------|
| **7zip-sdk (LZMA)** | unpacker 解压 bootstrapper |
| **cJSON** | JSON（通知、IPC 载荷等） |

金手指解析器使用 `third_party/cheat_support/` 内直接编译的 AES、base64、miniz、SHA-256 实现。

### 5.4 预编译静态库

| 库 | 用途 |
|----|------|
| **libkeystone** (`third_party/keystone/`) | ShnExt 汇编 |

C++ runtime 统一由 `PS5_PAYLOAD_SDK/target/lib` 提供。项目不再携带旧 curl/TLS、minizip/zlib/zstd 或外部 9021 服务镜像；金手指仓库同步直接链接 SDK `target/user/homebrew` 提供的 libcurl/OpenSSL，嵌入 SDK CA bundle 开启证书校验，下载 HTTPS ZIP 后用现有 miniz 定向提取 `cheats/`。用户 Payload 通过 `common/elfldr_remote.c` 严格走内置 9020；外部 9021 仅用于首次引导和恢复 9020。

### 5.5 PS5 系统库 stub（`source/platform/ps5/stubs/*.so`）

当前链接目标：

- `libkernel_sys`
- `SceSystemService` / `SceUserService`
- `SceNetCtl` / `SceNet`
- `SceNotification` / `SceRegMgr`
- `SceSysCore` / `SceAppInstUtil`
- `SceGnmDriver`

运行时解析到主机系统模块。

### 5.6 构建期工具

| 工具 | 用途 |
|------|------|
| **Go stubber**（`tools/stubber/`） | NID stub 生成 |
| **Python3** | 构建辅助脚本（如 `encryptver.py`） |
| **lzma / xz** | bootstrapper 打包 |

---

## 6. 架构特点小结

1. **分层嵌入**  
   用户只下发一个 `OnionHEN.elf`，内部层层解压/拉起全套服务。

2. **双守护进程**  
   critical（注入/监视）与 util（网络/IO）分离，util 可崩溃恢复。

3. **elfldr 边界清晰**
   外部 9021 只负责首跳；运行时使用内置 9020，并通过握手避免误用其它占端口服务。

4. **UI = 进程注入**  
   Toolbox 不是独立 App，而是 ptrace 注入 ShellUI 的 Mono 代码。

5. **kstuff 解耦**  
   fPKG/fSELF 能力来自 kstuff-lite；可用 `/data/OnionHEN/kstuff.elf` 覆盖。

6. **IPC 协议稳定**  
   Unix socket；废弃命令保留序号，便于旧客户端不崩。

---

## 7. 相关文档

| 文档 | 说明 |
|------|------|
| [../README.md](../README.md) | 项目总览、功能列表、配置、加载方式 |
| [shellui-injection.md](shellui-injection.md) | ShellUI 注入路径与 libNineS 稳定性修复 |
| [pkg-writeup.md](pkg-writeup.md) | PS5 PKG 技术说明 |
| [../source/README.md](../source/README.md) | 源码树与构建说明 |
| [../third_party/README.md](../third_party/README.md) | 子模块与已移除第三方依赖 |

---

## 8. 构建速查

```bash
git submodule update --init --recursive
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk

./scripts/build.sh
# 或缺 vendor 时仅编译验证：
# ./scripts/build.sh --stub-missing
```

手动 CMake：

```bash
./scripts/ps5_cmake.sh -S source -B build -G Ninja
cmake --build build
```
