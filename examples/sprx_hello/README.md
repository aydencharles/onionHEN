# OnionHEN PS5 PRX Example (`onion_example.prx`)

一个最小、完整、可直接编译并在 PS5 原生游戏中测试加载的 PRX 插件示例，无需专有 SDK，仅依赖 `clang`、`ld.lld` 和 `python3`。

## 示例功能

加载进目标游戏进程后，插件会执行以下安全、无侵入的演示操作（不 hook 游戏渲染或内存地址）：

1. **`module_start`**：
   - 调用 `getpid()` 获取目标游戏 PID；
   - 通过 `sceKernelSendNotificationRequest()` 弹出 PS5 系统通知（显示加载成功与当前 PID）；
   - 通过 `sceKernelDebugOutText()` 向 `klog`（3232 端口）输出日志，并在可写时追加到 `/data/OnionHEN/sprx/onion_example.log`；
   - 通过 `scePthreadCreate()` 启动后台线程 `OnionExamplePrx`，每 5 秒打印一次心跳（共 3 次）后自动退出。
2. **`module_stop`**：
   - 通知后台线程提前退出并 `scePthreadJoin()` 回收线程资源。

## 目录结构

```text
examples/sprx_hello/
├── Makefile              # 编译并打包成 PS5 FSELF (.prx)
├── prx.ld                # 16 KiB 对齐的 PS5 PRX 链接脚本
├── catalog.ini.example   # OnionHEN SPRX 目录配置示例
├── src/
│   ├── crtprx.c          # _init/_fini 入口与 PS5 PT_SCE_MODULE_PARAM
│   └── main.c            # 插件主体逻辑（仅依赖 libkernel.prx）
├── stubs/
│   └── libkernel_stub.c  # 链接期 libkernel.prx 桩符号
└── tools/
    └── elf2ps5prx.py     # ELF64 -> PS5 ET_SCE_DYNAMIC OELF -> PS5 FSELF 打包脚本
```

## 编译

```bash
cd examples/sprx_hello
make
```

默认输出：`examples/sprx_hello/build/onion_example.prx`。

### 关于 SDK 版本与 `0x80020063`（`SCE_KERNEL_ERROR_ESDKVERSION`）

PS5 内核在 `dynlib_load_prx` 中会校验模块 `PT_SCE_MODULE_PARAM` 里的 SDK 版本与目标游戏是否兼容。默认参数对齐 PS5 SDK 7.00（`SDK_VER=0x10590001`、`PPR_SDK=0x07000038`、`SDK_UNK=0x00000411`）。

如果目标游戏返回 `0x80020063`，可在编译时覆盖为目标游戏 `eboot.bin` / `libc.prx` 的 SDK 版本：

```bash
make clean && make SDK_VER=0x10590001 PPR_SDK=0x07000038 SDK_UNK=0x00000411
```

## 部署与测试

1. 上传到 PS5 的 `/data/OnionHEN/sprx/onion_example.prx`：

```bash
make deploy PS5_IP=192.168.31.48
```

2. 在 PS5 上进入游戏（建议先让游戏完成初始化、进入标题界面或游戏画面），打开 **OnionHEN Toolbox → SPRX 插件**，将 `onion_example.prx` 绑定到当前游戏（或参考 `catalog.ini.example` 配置 `/data/OnionHEN/sprx/catalog.ini`）并点击手动加载。

3. 验证现象：
   - PS5 屏幕右上角弹出通知：`OnionHEN Example PRX loaded! PID: ...`
   - `klog`（3232 端口）及 `/data/OnionHEN/sprx/onion_example.log` 中出现 `module_start ok` 和 3 条 `worker heartbeat` 日志。
   - 游戏画面持续正常运行，不卡顿不崩溃。
