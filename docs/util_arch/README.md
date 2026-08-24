# util 守护进程架构

`util.elf` 是 OnionHEN 的 **Utility 守护进程**：与 `daemon.elf`（critical）分离，承载网络 IO、FTP、金手指和 Toolbox 相关服务。

| 项 | 值 |
|----|-----|
| 产物 | `build/bin/util.elf` |
| 源码根 | `source/util/` |
| Unix IPC | `/system_tmp/onionhen/ipc/util_service` |
| IPC 命令前缀 | `0x8000000`（`BREW_UTIL_*`） |
| 日志 | `OnionHEN_log` → stdout + klog + `/data/OnionHEN/OnionHEN_util_daemon.log` |
| 崩溃日志 | `/data/OnionHEN/OnionHEN_util_crash.log` |

全局架构见 [arch.md](../arch.md)。  
金手指 **C++ 设计**见 [cheats_cpp.md](cheats_cpp.md)；格式细节见 `source/util/source/cheats/README.md`。

---

## 1. 在整体系统中的位置

```text
┌─────────────┐     spawn/load      ┌──────────────┐
│ bootstrapper│ ──────────────────► │  daemon.elf  │  critical IPC
└─────────────┘                     │  (main)      │  /system_tmp/onionhen/ipc/crit_service
                                    └──────┬───────┘
                                           │ 也可拉起
                                           ▼
                                    ┌──────────────┐
                                    │  util.elf    │  util IPC + 网络服务
                                    └──────┬───────┘
           ┌───────────────────────────────┼───────────────────────────────┐
           ▼                               ▼                               ▼
    ShellUI                                                    游戏进程
    (Unix socket 客户端)                               mdbg|kdirect 写内存
```

**典型客户端**：`shellui.elf` 通过 `IPC_Client(util_daemon=true)` 连 util socket。

---

## 2. 启动顺序（`main` 调用链）

入口：`source/util/source/main.cpp`。

```text
main()
 │
 ├─ 1. 设置稳定线程名 onion_util.elf
 ├─ 2. sceNetCtlInit / sceUserServiceInitialize / 日志与通知初始化
 ├─ 3. setjmp + fault_handler_init(cleanup)
 ├─ 4. payload_get_args() → kernel_base
 ├─ 5. 刷新系统语言并进入 PTRACE_AUTHID
 ├─ 6. 清理 util crash 日志
 ├─ 7. LoadSettings()                      # 配置及 FTP autoload
 ├─ 8. start_ip_thread()                   # 后台刷新本机 IP
 ├─ 9. pthread_create(IPC_loop)            # 常驻 Unix 监听
 ├─10. 发布 util ready/runtime 标记
 ├─11. CheatService::ensureDir()
 │
 └─12. for(;;) sleep(1)
```

要点：

- **IPC 线程先于主循环启动**；listen `accept` 失败会自愈重绑（参考 [ps5-payload-manager](https://github.com/itsplk/ps5-payload-manager)）。
- Toolbox 休息恢复在 **daemon** 的 SceSysCore `NOTE_EXEC`（新 `NPXS40087`）上，等 `libSceNpTrophy.sprx` 与 `libSceNpTrophy2.sprx` 后再注入（参考 [kstuff-lite](https://github.com/EchoStretch/kstuff-lite)）。
- 金手指 **service 状态**在冷启动后 `ensureDir` 一次。

---

## 3. 模块地图与职责

```text
source/util/
├── source/
│   ├── main.cpp                 # 生命周期编排
│   ├── msg.cpp                  # Unix IPC 服务端传输
│   ├── ipc_handle.cpp           # BREW_UTIL_* 命令分发
│   ├── service_facade.cpp       # 进程内 FTP 生命周期与端口切换
│   ├── common_utils.c           # 通知 / ptrace attach / 通用工具
│   ├── faulthandler.c           # 信号与崩溃落盘
│   ├── cpp_service.cpp          # IP 线程
│   ├── util_platform.c          # 共享平台：固件/版本/模块/读文件
│   ├── util_language.c          # 系统与 Toolbox 语言解析
│   ├── util_toolbox.cpp         # Toolbox 重注入请求
│   └── cheats/                  # 金手指领域（C++ 编排 + 解析 + C 适配）
│       ├── CheatService / Repository / Applier
│       ├── ICheatParser + Factory (json/shn/mc4)
│       ├── ShnExt adapter + C crypto/utils/flatten
│       └── sync/ Catalog + Mirror + HTTPS + ZIP 安装
├── include/
│   ├── common_utils.h / ipc.hpp / pt.h / sfo.hpp / ...
│   ├── util_platform.h
│   └── cheats/                  # 金手指公共/内部头
└── CMakeLists.txt                # util.elf 与 ftpsrv 源码模块构建
```

| 模块 | 文件 | 依赖方向（被谁用） |
|------|------|-------------------|
| Lifecycle | `main.cpp` | 无（顶层） |
| IPC transport | `msg.cpp` | main 线程创建 |
| IPC commands | `ipc_handle.cpp` | IPC client 线程调用 |
| Logging / notify | `common_utils.c` | 几乎全部 |
| Platform | `util_platform.c` | cheats、可被其它业务复用 |
| FTP | `service_facade.cpp` + `third_party/ftpsrv` | main / IPC |
| Cheat sync | `cheats/sync/*` | IPC 后台任务 |
| IP poll | `cpp_service.cpp` | main 启动 |
| Toolbox reinject | `util_toolbox.cpp` | util 崩溃重启路径 |
| Cheats | `cheats/*` | msg IPC |

**依赖方向**：

```text
main ──► IPC / FTP / cheats(init) / ip_thread
ipc_handle ──► CheatService / CheatSyncService / FtpServiceFacade
CheatService ──► Repository / ParserFactory / Applier ──► util_platform + pt/mdbg/kernel
```

金手指的进程、固件和文件能力统一由 `util_platform` 提供。

---

## 4. 线程模型

| 线程 | 创建位置 | 生命周期 | 作用 |
|------|----------|----------|------|
| main | `main` | 进程 | 保活 |
| IPC accept | `IPC_loop` | 常驻 | accept Unix 连接 |
| IPC client | `ipc_client`（每连接一个，detach） | 连接级 | 读 `IPCMessage` → `handleIPC` |
| IP poll | `start_ip_thread` | 常驻 | 刷新本机 IP 字符串 |
| FTP listener | `FtpServiceFacade::start` | 配置启用期间 | 运行 `ftp_serve` 并管理监听端口 |
| Cheat sync | `CheatSyncService::start` | 单次任务 | HTTPS 下载、解压与安装 catalog |

故障：`faulthandler` 触发 `cleanup` → cleanup → `exit`。

---

## 5. IPC 协议与命令分发

### 5.1 报文

定义见 `source/include/msg.hpp`：

```text
struct IPCMessage {
  int magic = 0xDEADBABE;
  DaemonCommands cmd;
  int error;
  char msg[0x1000];   // JSON 入参 / 文本出参
};
```

服务端：`networkListen(UTIL_IPC_SOC)` → accept → 校验 magic → `handleIPC(client, json, cmd)` → `reply(error, out_var)`。

### 5.2 util 命令表（`BREW_UTIL_*`）

| 命令 | 作用 | 内部去向 |
|------|------|----------|
| `TEST_CONNECTION` | util 可用性探测 | IPC reply |
| `DAEMON_PID` | 返回 util pid | `getpid` |
| `TOGGLE_FTP` | 启停进程内 FTP | `FtpServiceFacade` |
| `FTP_STATUS` | 返回 FTP 运行状态 | `FtpServiceFacade` |
| `RECOVER_FTP` | 待机恢复后重绑已启用的 FTP 监听 | `FtpServiceFacade` |
| `GET_GAME_VER` | 游戏版本字符串 | param.json / param.sfo（msg 内实现） |
| `GET_GAME_CHEAT` | 导出金手指列表 JSON 文件路径 | `CheatService::exportList` |
| `TOGGLE_CHEAT` | 开关某条金手指 | `CheatService::toggle` |
| `DOWNLOAD_CHEATS` | 后台 HTTPS ZIP 下载 → 定向解压 → flatten | `CheatSyncService` + 现有 flatten |
| `CHEAT_SYNC_STATUS` | 上次/正在进行的同步快照 | `CheatSyncService::status` |
| `CANCEL_CHEAT_SYNC` | 请求取消指定同步任务 | `CheatSyncService::cancel` |
| `LAUNCH_PAYLOAD` | 加载 payload `.elf` | `load_payload` → `onion_payload_load`（有效 PID 已存在时保持现有实例；新实例仅使用私有 9020 并要求精确 PID） |
| `BREW_KILL_DAEMON` / `BREW_RELOAD_SETTINGS` | 结束 util / 重载 ini | main 侧状态 |

以下稳定 ABI 命令返回错误且不产生副作用：

- `UNUSED_KLOG`、`UNUSED_DPI`、`UNUSED_SHELLUI_ON_STANDBY`
- `UNUSED_RELOAD_CHEATS`、`UNUSED_DOWNLOAD_KSTUFF`
- `UNUSED_LEGACY_CMD_SERVER`
- `UNUSED_LEGACY_SERVICE_SCAN`、`UNUSED_LEGACY_SERVICE_TOGGLE`
- `LAUNCH_ELFLDR`

### 5.3 金手指 IPC 时序（ShellUI → 游戏）

```text
ShellUI
  │ GET_GAME_VER(tid)
  ▼
util handleIPC ──► param.json / sfo ──► version 字符串
  │
  │ GET_GAME_CHEAT(tid, version)
  ▼
cheat_service_export_list
  │  resolve all /data/OnionHEN/cheats/<TID>_<VER>[_PROCESS][_SOURCE_ID].{json,shn,mc4,ShnExt}
  │  load + parse → 写 /user/data/OnionHEN/<tid>_cheats
  ▼
ShellUI 读列表 JSON，渲染开关
  │
  │ TOGGLE_CHEAT(tid, version, pid, cheat_id)
  ▼
cheat_service_toggle_index
  │  refresh 文件签名 → onion_toggle_cheat
  ▼
cheat_engine_runtime
  │  util_find_module → base
  │  remote_ops(fw)：mdbg | kdirect
  ▼
目标游戏进程内存
```

---

## 6. 各子系统内部逻辑

### 6.1 `common_utils` / 日志

- **`OnionHEN_log`**：格式化 → 追加 `\n` → `printf` + `klog_printf` + append 日志文件。
- **`notify`**：系统通知气泡。
- **`pt_attach_proc` / `pt_detach_proc`**：提权 authid 后 `ptrace`（code cave mmap 用）。
- 其它：HTTP 初始化封装声明、`download` 相关声明等。

全模块日志统一走 `OnionHEN_log`。

### 6.2 `util_platform`（共享平台）

| API | 行为摘要 |
|-----|----------|
| `util_system_fw_major` | `sceKernelGetProsperoSystemSwVersion` → `version >> 16` |
| `util_file_read_alloc` | 整文件读入 heap（带 max size） |
| `util_resolve_game_version` | 扫 appmeta 的 param.json / param.sfo |
| `util_get_running_bigapp` | BigApp appid + 进程表匹配 + 版本解析 |
| `util_find_module` | dynlib 列表匹配（filename/libname/basename）→ 失败则 eboot 映像基址 fallback |
| `util_find_module_in_app` | 按 appid 扫进程再 `util_find_module` |

**与 hijacker 的固件号差异**：hijacker `getSystemSwVersion()` 用 `kern.sdk_version` 做偏移表；金手指门控用 Prospero major。二者用途不同，不可混用。

### 6.5 IP / Toolbox reinject

- **`start_ip_thread`**（`cpp_service.cpp`）：维护全局 IP 字符串，供 notify 文案使用。
- **`toolbox_reinject` / `enable_toolbox`**（`util_toolbox.cpp`）：util 崩溃/重拉后经 crit daemon 请求注入；休息恢复不在 util。

### 6.6 金手指（C++：`onion::cheats`）

编排与格式解析均为 C++（Facade / Strategy / Factory）；ShnExt crypto 经 Adapter 调 C。详见 [cheats_cpp.md](cheats_cpp.md)。

```text
┌──────────────────────────────────────────────┐
│ CheatService   Facade + mutex + 热重载状态    │
└───────────────────┬──────────────────────────┘
        ┌───────────┼──────────────┐
        ▼           ▼              ▼
 CheatRepository  CheatApplier   Flatten
 路径/签名/load    toggle 补丁    仓库安装
        │           │
        ▼           ▼
 CheatParserFactory  IMemoryBackend
  json/shn/mc4/C++   Mdbg | Kdirect
  ShnExt → C adapter (Factory by FW)
        │           │
        └─────┬─────┘
              ▼
       util_platform + pt/mdbg/kernel
```

#### 路径与格式

```text
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].{json,shn,mc4,ShnExt}
```

`PROCESS` 与 8 位十六进制 `SOURCE_ID` 均可省略。`SOURCE_ID` 只标识物理来源，不参与运行时 hash 匹配。同步 flatten 时，未带显式 ID 且位于扫描根目录下的来源，会把相对路径的 `\\` 归一化为 `/`、转小写后计算 SHA-256，取前 8 位小写十六进制字符作为 ID。Repository 返回所有兼容来源，进程限定来源只匹配对应进程。
显示排序使用 `json` → `shn` → `mc4` → `ShnExt`，不会在独立来源之间择一。

#### 热重载

`cheat_service` 为每个来源缓存 path + size + inode + mtime + ctime。来源集合或签名变化时，先禁用已启用项，再整体重载并重建全局 ID 映射。

#### Toggle 写内存顺序（`cheat_engine_runtime`）

```text
1. util_system_fw_major → 选 backend（<0x840 mdbg，否则 kdirect）
2. util_find_module(module_name) → 失败则 util_find_module_in_app
3. 检测 PS2 模拟模块；base = sections[0].vaddr
4. Master Code / MC 依赖偏移修正（可选）
5. 对每个 patch 预检最终地址并以 `max(on_len, off_len)` 登记运行时占用；若与其它启用 cheat 重叠则整条拒绝
6. 对每个 patch：
     addr = absolute|ps2 ? offset : base+offset
     snapshot → write on/off → readback verify；后续失败时逆序回滚已写入 patch
     失败则 code_cave：pt_attach → pt_mmap 页 → mprotect → 再写
7. 更新 entry.enabled / last_applied_pid
```

#### 下载 flatten

在线 `DOWNLOAD_CHEATS` 走 `onion::cheats::sync`：Catalog（仓库身份）+ Mirror（github / cnb.cool）生成 archive URL，使用 HTTPS 下载 ZIP 到临时目录，miniz 只提取 catalog 声明的 `flattenRoots`（HEN 集合为 `cheats/`），再调用现有 `onion_cheat_flatten_install_tree`，完成后清理临时文件。`[cheats] mirror=auto|github|cnb`；`auto` 时简体中文走 cnb.cool，其它地区走 GitHub。进度经 `CHEAT_SYNC_STATUS.progress` 回传 Toolbox。

---

## 7. 与外部库 / 二进制依赖

| 依赖 | 用途 |
|------|------|
| libhijacker | 内核原语、偏移（shellcore / 注入路径） |
| libonion_elfldr | ptrace / mmap 注入原语；内置 9020 loader 复用其 spawn/read 实现 |
| keystone | ShnExt 汇编（`third_party/keystone/`）；C++ runtime 由 PS5 SDK 提供 |
| cJSON | IPC 与配置载荷 JSON 解析 |
| AES/base64 third_party | MC4 / ShnExt 解密 |
| miniz / sha256 | ShnExt 解压与密钥派生；flatten 也用 SHA-256 生成 HENCC source ID |
| ftpsrv | 编译进 util 的 FTP 服务源码模块 |
| libcurl / OpenSSL | 金手指 catalog HTTPS 下载与证书校验 |

---

## 8. 配置与运行时数据

| 路径 | 用途 |
|------|------|
| `/data/OnionHEN/config.ini` | toolbox 与守护进程配置 |
| `/data/OnionHEN/cheats/` | 金手指 flat 文件 |
| `/data/OnionHEN/cheats_staging/` | 下载解压临时区 |
| `/user/data/OnionHEN/<tid>_cheats` | ShellUI 消费的列表 JSON |
| `/data/OnionHEN/OnionHEN_util_daemon.log` | 运行日志 |
| `/data/OnionHEN/kstuff.elf` | 下载的 kstuff |
| `ONION_FLAG_UTIL_BOOTED` | util 是否已完成过冷启动（typed ready flag） |

`LoadSettings` 读取统一的 `config.ini` schema（`onion::Settings`），缺失键使用默认值。

---

## 9. 源码导航速查

| 想改… | 先看 |
|--------|------|
| 守护进程启停 / 主循环 | `source/util/source/main.cpp` |
| 新 IPC 命令 | `source/include/msg.hpp` + `source/util/source/msg.cpp` |
| 金手指列表/开关 | `cheats/cheat_service.cpp` → `cheat_applier.cpp` |
| 新金手指格式 | `ICheatParser` + `CheatParserFactory::createByFormat` |
| 写内存策略 | `i_memory_backend.hpp` / `memory_backends.cpp` |
| 版本/模块/固件 | `util_platform.c` |
| IP 线程 | `cpp_service.cpp` |
| 下载/zip | `cheats/sync/http_transport_ps5.cpp` / `zip_archive.cpp` |
| 日志 | `common_utils.c` → `OnionHEN_log` |

---

## 10. 设计约束（实现时应遵守）

1. **业务进 util，不进 critical daemon**（稳定性边界）。
2. **平台能力放 `util_platform`**，避免功能目录再复制一份进程/固件逻辑。
3. **日志统一 `OnionHEN_log`**（klog 仅作底层 sink，不是第二套业务 logger）。
4. **金手指路径 flat、无 txt 索引、无扩展名子目录**。
5. **高固件写内存优先 kdirect**，避免无必要 `PT_ATTACH` 停进程；code cave 仍可短时 ptrace mmap。
6. **C++ 单元勿 include NineS `freebsd-helper`**（与 SDK 头冲突）；模块信息用 `util_module_info_t` ABI 对齐。
