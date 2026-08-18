<p align="center">
  <img src="assets/logo.png" alt="OnionHEN" height="128" width="128"/>
</p>

<p align="center">
  <b>OnionHEN</b><br/>
  面向 PlayStation 5 的一体化 HEN 与工具箱
</p>

<p align="center">
  <b>简体中文</b>
  ·
  <a href="README.md">English</a>
</p>

<p align="center">
  <b><a href="#功能">功能</a></b>
  ·
  <b><a href="#运行">运行</a></b>
  ·
  <b><a href="#构建">构建</a></b>
  ·
  <b><a href="#配置">配置</a></b>
  ·
  <b><a href="#赞助">赞助</a></b>
  ·
  <b><a href="#致谢">致谢</a></b>
</p>

---

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="license"/></a>
  <img src="https://img.shields.io/badge/Platform-PlayStation%205-003791?style=flat&logo=playstation" alt="PlayStation 5"/>
  <img src="https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white" alt="C"/>
  <img src="https://img.shields.io/badge/C++-00599C?style=flat&logo=cplusplus&logoColor=white" alt="C++"/>
  <img src="https://img.shields.io/badge/Build-CMake-064F8C?style=flat&logo=cmake" alt="CMake"/>
</p>

<p align="center">
  OnionHEN 是一套模块化的 PS5 payload。一个入口 ELF 完成系统准备、注入<br/>
  ShellUI 工具箱，再拉起监控条、金手指、用户 payload 和后台服务。
</p>

<p align="center">
  <img src="assets/screenshot_01.png" alt="OnionHEN 主界面入口"/>
</p>

<p align="center">
  <img src="assets/screenshot_02.png" alt="OnionHEN 游戏监视条"/>
</p>

<p align="center">
  <img src="assets/screenshot_03.png" alt="OnionHEN Toolbox"/>
</p>

<br>

# 功能

OnionHEN 面向已越狱的 PS5，提供一套能日常使用、也方便维护的自制环境。

- **ShellUI 工具箱** — 注入 PS5 ShellUI 的设置页
- **系统准备** — 提权、重新挂载文件系统、阻断系统更新分区
- **fSELF / fPKG** — 内嵌 kstuff，用来跑自制 SELF / PKG；默认加载，可在工具箱关掉
- **Payload 管理** — 启动和停止普通 `.elf` payload，可选自动启动
- **游戏监控条** — 游戏中显示 FPS、CPU、GPU、内存、温度和网络信息
- **金手指** — 本地 JSON、SHN、MC4、ShnExt 文件，运行中即可开关
- **FTP 服务** — 可选的 TCP **1337** FTP 服务，可从工具箱或 `network.ftp` 控制
- **主机工具** — 休息模式、账号激活、外接硬盘、Title ID、风扇、快捷键和游戏选项
- **应用越狱** — 白名单自制软件可通过守护进程沙盒 FIFO 申请提权
- **可恢复运行时** — 关键守护进程和工具守护进程分开；主进程可以拉起工具进程
- **统一配置** — 工具箱和守护进程共用一份带版本号的 `config.ini`

OnionHEN 不内置内核漏洞。第一次引导仍需要外部 **9021** 上的 `elfldr`。
之后会启动自己的私有 **9020** 加载器，用来加载后续 ELF 和用户 payload。

<br>

# 运行

### 运行要求

1. 一台已越狱的 PS5
2. 一个监听 **9021**、用于第一次引导的外部 [PS5 `elfldr`](https://github.com/ps5-payload-dev/elfldr)

### 加载 OnionHEN

1. 运行内核漏洞利用，并启动外部 `elfldr` 服务。
2. 用漏洞加载端提供的加载器发送 `OnionHEN.elf`。
3. 按顺序等待工具守护进程、`kstuff` 和主守护进程启动。
4. 打开 PS5 设置，进入 OnionHEN 工具箱。

启动是按固定顺序进行的。第一次引导之后，OnionHEN 使用自己的
`onion_elfldr.elf`（端口 **9020**），**9021** 只作为兼容回退。

```text
OnionHEN.elf → bootstrapper → onion_elfldr.elf (:9020) → util.elf → kstuff.elf → daemon.elf → Toolbox
```

### Payload

把独立 payload 放到：

```text
/data/OnionHEN/payloads/
```

只支持普通 `.elf`。可在工具箱里打开自动启动；OnionHEN 会在 ELF 旁边写一个
同名的 `.auto_start` 文件记住这个选择。

### 金手指

把金手指文件放到同一目录。文件名为 `TITLEID_VERSION`，进程名和 8 位十六进制 HASH 都可省略：

```text
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<HASH>].json
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<HASH>].shn
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<HASH>].mc4
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<HASH>].ShnExt
```

省略 `PROCESS` 表示 `eboot.bin`。省略 `HASH` 表示不区分 eboot 构建。带进程名的文件优先于通用 `TITLE_VERSION`；格式优先级为 json、shn、mc4、ShnExt。

金手指从磁盘加载。文件有改动时会重新载入，不必重启整套进程。

`DOWNLOAD_CHEATS` 会通过 HTTPS 下载金手指仓库 ZIP，只解压 `cheats/` 子树，再 flatten 进上述目录；同步完成后会清理临时 ZIP。镜像由 `[cheats] mirror` 控制：`auto` 时简体中文走 cnb.cool，其它地区走 GitHub。

### FTP

可选 FTP 服务监听 TCP **1337** 端口。可以在工具箱的 Network 分组中开关，
也可以在 `config.ini` 中设置：

```ini
[network]
ftp=true
```

<br>

# 构建

### 依赖

| 依赖 | 用途 |
| --- | --- |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) | Prospero 编译器、目标头文件、运行库和 CMake 封装 |
| CMake 3.20+ 与 Ninja | 配置并构建整套 payload |
| Clang / LLVM | 编译 `x86_64-sie-ps5` 目标 |
| `lzma` 或 `xz` | 压缩 bootstrapper |
| Git 与 `curl` 或 `wget` | 初始化 submodule 并获取外部 payload 输入 |

### 完整构建

```shell
git submodule update --init --recursive

export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
./scripts/build.sh --jobs 8
```

脚本会配置项目、拉取外部依赖，并按所需顺序构建整条嵌入链。

### Docker 构建

仓库包含预装 PS5 SDK 和构建依赖的 Dockerfile 与 Compose 服务。
如果存在本地 `.docker/pacbrew` SDK overlay，Compose 会以只读方式挂载它。

```shell
docker compose up --build
```

如果已经有构建好的镜像、不需要重新构建：

```shell
docker compose up
```

### 常用选项

| 选项 | 说明 |
| --- | --- |
| `--build-type Debug\|Release` | 选择 CMake 构建类型 |
| `--build-dir <path>` | 覆盖默认的 `build/` 目录 |
| `--cache-dir <path>` | 覆盖 `.cache/dependencies/` |
| `--stub-missing` | 使用仅供编译的外部 ELF 占位文件，禁止在真机使用 |
| `--skip-unpacker` | 构建完 bootstrapper 后停止 |
| `--init-submodules` | 同步依赖前初始化 submodule |

运行 `./scripts/build.sh --help` 查看完整选项列表。

### 构建产物

| 路径 | 说明 |
| --- | --- |
| `build/bin/OnionHEN.elf` | 发送到主机的最终 payload |
| `build/bin/bootstrapper.elf` | 未压缩的 bootstrapper |
| `build/bin/bootstrapper.elf.lzma` | 嵌入最终 payload 的 bootstrapper |
| `build/bin/daemon.elf` | 关键守护进程 |
| `build/bin/util.elf` | 工具守护进程 |
| `build/bin/shellui.elf` | Toolbox 注入 payload |
| `build/lib/*.a` | 本仓库静态库 |

构建产物都在 `build/`。下载的依赖缓存在 `.cache/dependencies/`。
不会往 `source/` 里写产物。

### 测试

主机侧测试覆盖配置、IPC、payload 辅助逻辑、金手指解析、重定位、工具箱路由和共享平台代码：

```shell
make -C source/util/tests clean
make -C source/util/tests test -j8
```

链接主机侧测试时需要能通过 `KEYSTONE_PREFIX` 找到 Keystone，默认是 `/opt/homebrew`。

<br>

# 配置

OnionHEN 在下面两处读写同一份配置：

```text
/data/OnionHEN/config.ini
/user/data/OnionHEN/config.ini
```

大部分选项都可以在工具箱里改。配置格式是 `schema_version=1`。
如果还没有配置文件，OnionHEN 会按 [`config.ini.example`](config.ini.example)
写出一份带注释的默认文件。

| 配置项 | 默认值 | 可用值 |
| --- | --- | --- |
| `meta.schema_version` | `1` | `1` |
| `toolbox.language` | `system` | `system`, `zh-Hans`, `zh-Hant`, `en`, `ja`, `ko`, `fr`, `de`, `it`, `es`, `pt-BR`, `pl`, `ru`, `ar`, `th` |
| `startup.open_after_load` | `none` | `none`, `home_menu` |
| `home_screen.show_title_ids` | `false` | `true`, `false` |
| `game_menu.show_onionhen_options` | `true` | `true`, `false` |
| `rest_mode.resume_reinject_delay_seconds` | `0` | 秒数 |
| `network.ftp` | `true` | `true`, `false` |
| `rest_mode.stop_utility_daemon_on_entry` | `false` | `true`, `false` |
| `rest_mode.close_running_game_on_entry` | `false` | `true`, `false` |
| `cheats.memory_backend` | `default` | `default`, `libhijacker` |
| `cheats.mirror` | `auto` | `auto`, `github`, `cnb` |
| `app_jailbreak.debug_notifications` | `false` | `true`, `false` |
| `cooling.fan_control` | `automatic` | `automatic`, `temperature_threshold` |
| `cooling.temperature_threshold_celsius` | `77` | `0` 到 `100` |
| `overlay.enabled` | `true` | `true`, `false` |
| `overlay.background` | `true` | `true`, `false` |
| `overlay.edge` | `top` | `top`, `bottom` |
| `overlay.show_cpu` / `overlay.show_gpu` / `overlay.show_memory` | `true` | `true`, `false` |
| `overlay.cpu_usage_mode` | `average` | `average`, `per_core` |
| `overlay.show_ip_address` | `false` | `true`, `false` |
| `shortcuts.cheats_menu` | `off` | `off`, `r3_l3`, `l2_triangle`, `long_options`, `long_share`, `share` |
| `shortcuts.toolbox` | `off` | `off`, `l2_r3`, `long_share`, `share` |

### 运行时数据

| 路径 | 用途 |
| --- | --- |
| `/data/OnionHEN/payloads/` | 用户 payload ELF |
| `/data/OnionHEN/cheats/` | 金手指文件 |
| `/data/OnionHEN/cheats_tmp/` | HTTPS ZIP 与解压临时文件（同步后清理） |
| `/data/OnionHEN/kstuff.elf` | 可选，用来替换内嵌的 `kstuff` |
| `/data/OnionHEN/OnionHEN.log` | 主运行日志 |
| `/data/OnionHEN/OnionHEN_util_daemon.log` | Utility daemon 日志 |

<br>

# 主机侧工具

| 工具 | 用途 |
| --- | --- |
| [`scripts/daemon_log.py`](scripts/daemon_log.py) | 从主机拉取守护进程日志 |
| [`scripts/shutdown_onion.py`](scripts/shutdown_onion.py) | 关闭 OnionHEN 用户态进程，但不结束 `kstuff` |
| [`scripts/ps5_cmake.sh`](scripts/ps5_cmake.sh) | 通过 PS5 Payload SDK 运行 CMake |
| [`scripts/sync_dependencies.sh`](scripts/sync_dependencies.sh) | 获取或构建外部 payload 输入 |

<br>

# 仓库结构

```text
.
├── assets/                    项目图片资源
├── docs/                      架构与技术文档
├── scripts/                   构建、依赖和主机侧辅助脚本
├── source/                    本仓库的 PS5 源码
│   ├── bootstrapper/          启动与内嵌 payload 链
│   ├── daemon/                关键守护进程与工具箱注入
│   ├── util/                  工具守护进程、IPC 与金手指
│   ├── shellui/               工具箱与 ShellUI 挂钩
│   ├── unpacker/              最终 OnionHEN payload 包装
│   ├── libonion_*/            本仓库共享库
│   ├── common/                共享底层实现
│   └── platform/ps5/stubs/    PS5 系统库链接 stub
├── third_party/               外部源码、预编译库和 submodule
├── tools/                     仓库侧生成工具
├── build/                     生成的构建产物（忽略提交）
└── .cache/dependencies/       下载的外部输入（忽略提交）
```

完整模块与 IPC 关系见[架构文档](docs/arch.md)。

<br>

# 故障排查

1. 加载 OnionHEN 前，确认外部 `elfldr` 能从 **9021** 连上。
2. 确认这份 payload 是用完整的 PS5 Payload SDK、按当前固件构建的。
3. 工具箱没出现时，等 `util → kstuff → daemon` 启动完，再看日志。
4. 构建拿不到 `kstuff.elf` 时，再跑一次 `./scripts/sync_dependencies.sh`，或初始化 submodule。
5. 报问题时请附上固件、漏洞加载端、SDK 版本、构建类型、日志和复现步骤。

<br>

# 参与贡献

见 [CONTRIBUTING.md](CONTRIBUTING.md)。

<br>

# 赞助

<a href="https://ko-fi.com/0xp0co"><img src="https://img.shields.io/badge/Ko--fi-Support-FF6433?style=for-the-badge&logo=kofi&logoColor=white" alt="在 Ko-fi 上支持 OnionHEN"/></a>

如果 OnionHEN 对你有用，欢迎支持后续开发。谢谢。

<br>

# 致谢

OnionHEN 离不开 PS5 自制软件与逆向工程社区。

### 贡献者

<p align="center">
  <a href="https://github.com/aydencharles/onionHEN/graphs/contributors">
    <img src="https://contrib.rocks/image?repo=aydencharles/onionHEN" alt="OnionHEN 贡献者"/>
  </a>
</p>

### 来源

- [etaHEN](https://github.com/LightningMods/etaHEN) — LightningMods 与贡献者；本仓库的源码基线
- [GoldHEN](https://github.com/GoldHEN/GoldHEN) — SiSTR0 与贡献者；PS4 一体化 HEN，本项目沿这条路走

### 参考

- [ps5-payload-manager](https://github.com/itsplk/ps5-payload-manager) — itsplk；休息模式唤醒恢复（待机后恢复服务）的设计参考了这个项目

### 实际使用或内嵌

- [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) — Prospero 工具链与头文件
- [elfldr](https://github.com/ps5-payload-dev/elfldr) — 端口 9021 的首次引导加载器；不打进 payload
- [kstuff-lite](https://github.com/EchoStretch/kstuff-lite) — EchoStretch、sleirsgoevy 与贡献者；可选的 `kstuff.elf`
- [libhijacker](https://github.com/astrelsky/libhijacker) — astrelsky；进程劫持与内核读写
- [NineS](https://github.com/buzzer-re/NineS) — buzzer-re；注入 ShellUI
- [cJSON](https://github.com/DaveGamble/cJSON) — JSON 解析
- [7-Zip LZMA SDK](https://www.7-zip.org/sdk.html) — unpacker 解压
- [miniz](https://github.com/richgel999/miniz) — 金手指文件解压

### 测试人员

即食面、雨之声、大饼电玩、安定区、随风、麒麟、尼克库尔曼、云、啊烦、小小蔡、B站谢锡榆、荆枫

也感谢其他参与测试、研究和给出有效反馈的人。

<br>

# 许可证

本项目基于 [GNU General Public License v3.0](LICENSE) 发布。
第三方组件保留各自的许可证与声明。

> OnionHEN 是非官方自制项目，与 Sony Interactive Entertainment 无关。
> 请只在你自己的主机上使用，风险自负。本项目不提供任何担保。
