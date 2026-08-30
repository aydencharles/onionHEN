# 金手指 C++ 架构

## 目标

把 util 金手指收成可扩展的 C++ 模块：编排、写内存策略、格式解析策略用 C++ 表达；ShnExt 重型 crypto/deflate/keystone 仍为 C 翻译单元，经 **Adapter** 接入。

## 设计模式（克制使用）

| 模式 | 用在哪 | 解决什么 |
|------|--------|----------|
| **Facade** | `CheatService` | IPC 只依赖一个入口：export / toggle / flatten / ensureDir |
| **Strategy** | `IMemoryBackend` | mdbg vs kdirect 可替换写内存策略 |
| **Factory Method** | `MemoryBackendFactory::create()` | 按 `util_system_fw_major()` 选后端 |
| **Strategy + Factory** | `ICheatParser` / `CheatParserFactory` | json / shn / mc4 / ShnExt 解析路径统一 |
| **Adapter** | `ShnExtCheatParser` | 包装 `onion_cheat_parse_shnext_buffer` |
| **RAII** | `std::mutex` + `lock_guard` | 锁与服务状态生命周期 |
| **Singleton（进程级服务）** | `CheatService::instance()` | 与原全局 service 同生命周期，线程安全访问 |
| **Strategy** | `ICheatCatalog` / `ICheatMirror` / `IHttpTransport` | 仓库身份、镜像主机、HTTPS 传输可替换 |
| **Factory Method** | `CheatCatalogRegistry` / `CheatMirrorFactory` | 登记 catalog；按 `mirror` + 语言选主机 |
| **Facade** | `CheatSyncService` | IPC 只碰同步入口；安装复用 flatten |

不引入：Observer、Command、Abstract Factory 全家桶（当前无多套产品族需求）。

## 模块结构

```text
onion::cheats
├── i_memory_backend.hpp
│   ├── MdbgMemoryBackend
│   └── KdirectMemoryBackend
├── MemoryBackendFactory
├── i_cheat_parser.hpp
│   ├── JsonCheatParser
│   ├── XmlCheatParser          # .shn Trainer XML
│   ├── Mc4CheatParser          # AES-256-CBC → XML
│   └── ShnExtCheatParser       # Adapter → C parser
├── CheatParserFactory          # createByFormat / loadFile / loadBuffer
├── CheatApplier                # toggle / master-code / verify / code-cave
├── CheatRepository             # 路径解析、热重载签名、调用 Factory
├── CheatFlatten                # 仓库 flatten 安装（C）
├── CheatService                # Facade + 互斥状态
└── sync/                       # 在线仓库（与引擎分目录）
    ├── ICheatCatalog           # 下什么（HEN 集合细节只在 catalog_hen_collection.cpp）
    ├── ICheatMirror            # 从哪下（github / cnb.cool）
    ├── IHttpTransport          # PS5 SDK libcurl (HTTP/HTTPS)
    ├── zip_archive             # miniz 定向提取 cheats/
    └── CheatSyncEngine / Service
        │
        ▼
   C: cheat_engine_utils / ShnExt / third_party crypto
        │
        ▼
   util_platform + pt/mdbg/kernel
```

## 调用顺序

```text
IPC handleIPC
  → CheatService::instance()
       → exportList / toggle
            → Repository::refresh (path + file signature)
            → CheatParserFactory::loadFile
                 → ICheatParser::parse (by extension)
            → Applier::toggle
                 → MemoryBackendFactory
                 → util_find_module
                 → backend read/write (+ optional code cave)
```

## 在线仓库同步

同步只接受 `https://` archive URL。镜像生成 GitHub codeload 或 CNB archive 地址；`CheatSyncEngine` 通过 SDK 静态 libcurl 下载 ZIP，限制响应大小，再用 miniz 只提取 catalog 声明的 `cheats/` 子树，复用现有 flatten installer，最后清理 ZIP 和临时目录。libcurl 使用 OpenSSL，并从 SDK `target/user/homebrew/etc/ca-bundle.crt` 嵌入 Mozilla CA bundle；peer 与 hostname 校验均开启，不依赖目标机文件系统中的证书路径。

下载进度来自 libcurl 的 `XFERINFO` 字节计数和 miniz 的文件提取计数；LegacySettings 页面重新打开时可读取当前阶段和百分比，后台下载在 25%、50%、75% 发送系统通知。archive 失败只在网络/协议错误时切换备用镜像，解压或安装失败不会重复写入。

## 格式解析

| 扩展名 | Parser | 说明 |
|--------|--------|------|
| `.json` | `JsonCheatParser` | GoldHEN/OnionHEN JSON（cJSON） |
| `.shn` | `XmlCheatParser` | Trainer XML |
| `.mc4` | `Mc4CheatParser` | Base64 + AES-256-CBC → XML |
| `.ShnExt` | `ShnExtCheatParser` | deflate + AES + cJSON + 可选 keystone |

加载入口：`CheatParserFactory::loadFile` / `loadBuffer`（`CheatRepository` 调用）。

## 扩展点

- **新写内存后端**：实现 `IMemoryBackend`，在 `MemoryBackendFactory` 注册固件条件。
- **新文件格式**：实现 `ICheatParser`，在 `CheatParserFactory::createByFormat` 注册扩展名；Repository 路径扫描列表同步扩展；补 `source/util/tests` host 用例。
- **新 IPC 操作**：只扩 `CheatService` 公开方法，避免 IPC 直接碰 Applier/Backend。

## Host 单元测试

解析器可在 macOS/Linux 上测（无 PS5 SDK），参考 kylin-core `tests/test_cheat_*`：

```bash
cd source/util/tests && make test
```

覆盖：utils（hex/extract/brace）、Factory 路由、JSON/SHN/MC4 合成样例、真实 fixtures（json/shn/mc4/ShnExt）、文件名 TITLE/VERSION/PROCESS/SOURCE_ID 解析与 resolve。
不覆盖：`CheatApplier` / 写内存（需目标机）。
