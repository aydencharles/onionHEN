# Shell UI 日志并发写入评审

审查范围：`dev` 分支工作区未提交改动中与日志相关的部分。

| 文件 | 改动 |
|------|------|
| `source/libonion_platform/source/log.c` | 新增 `flock` 侧车锁、轮转前重检 |
| `source/libonion_platform/include/onion/log.h` | 文档补充多进程共享约定 |
| `source/libonion_elfldr/include/onion/elfldr_log.h` | `printf`+`klog` 改为走 `LOG_*` |
| `source/shellui/src/prx.cpp` | 新增 `onion_log_configure` / `onion_log_configure_crash` |

---

## 1. 当前并发控制机制

### 1.1 事实清单

| 机制 | 位置 | 覆盖范围 |
|------|------|----------|
| `pthread_mutex_t g_lock` | `log.c:33` | **进程内**所有线程；覆盖 `klog` / `stdout` / 文件三个 sink |
| `O_APPEND` 打开描述符 | `log.c:120` `open(..., O_WRONLY\|O_CREAT\|O_APPEND, 0777)` | **跨进程**，仅保证单次 `write()` 追加的原子性 |
| `flock(LOCK_EX)` 侧车锁 | `log.c:147` `rotate_lock_acquire_locked()` | **跨进程**，**仅覆盖轮转**（rename 链），不覆盖写入 |
| 轮转前重检 | `log.c:227-232` | 取锁后 `refresh_file_locked()` + `fstat` 复查，避免对端已轮转后重复轮转 |
| 描述符跟随 | `log.c:169` `refresh_file_locked()` | 每次写入前比对 `fstat(fd)` 与 `stat(path)` 的 `st_dev`/`st_ino`，失配则重开 |

**结论：写入路径没有跨进程互斥，唯一保障是 `O_APPEND` + 单次 `write()` 的原子追加语义。锁只保护轮转。**

### 1.2 实际写入者（本次改动后）

`/data/OnionHEN/OnionHEN.log` 的写入者从 2 个增加到 3 个：

- `daemon` — `daemon/source/main.cpp:257`，tag `OnionHEN`，常驻且多线程
- `ShellUI` — `shellui/src/prx.cpp:740`，tag `ShellUI`，**本次新增**；`run_keep_alive()` 常驻，与 daemon 全程并发
- `bootstrapper` — `bootstrapper/source/main.cpp:35,56`，tag `Bootstrapper`，启动期短暂存在

叠加 `elfldr_log.h` 的改动，`libonion_elfldr` 的错误输出（`LOG_PUTS`/`LOG_PERROR`/`LOG_PT_PERROR`）现在也进入文件 sink，而这些库被链入 `util`、`bootstrapper`、`libNineS`。稳态下的并发写者是 **daemon + ShellUI 两个常驻进程**。

`util` 进程走独立路径 `/data/OnionHEN/OnionHEN_util_daemon.log`，不参与共享。

### 1.3 平台可行性（已实测）

- `flock` 在 PS5 上真实可用：SDK `target/lib/libkernel.so` 与 `libkernel_sys.so` 均导出 `T flock`。
- `target/include/fcntl.h:322` 声明 `int flock(int, int)`，`LOCK_EX`/`LOCK_UN` 在 `__BSD_VISIBLE` 下可用 —— `log.c` 中"FreeBSD 经 `<fcntl.h>` 暴露"的注释对该 SDK 成立。
- `build/libonion_platform/CMakeFiles/onion_platform.dir/source/log.c.o` 的 mtime（10:58:59）晚于 `log.c`（10:51:59），`nm -u` 显示 `U flock`，全部 ELF 于 10:59 重新链接完成 —— **改动可编译、可链接，非空实现。**

---

## 2. 并发风险实测与评估

在宿主机上直接编译 `log.c` 并做多进程压力测试（每进程各自调用 `onion_log_configure`，与真实进程模型一致）：

| 场景 | 结果 |
|------|------|
| 4 进程 × 400 条 × ~90B，不轮转 | 1600/1600 行，**0 撕裂，0 重复** |
| 4 进程 × 400 条 × ~90B，cap 512B（高频轮转） | 0 撕裂，0 重复（未命中记录为轮转正常淘汰） |
| 6 进程 × 500 条 × ~4KB，不轮转 | 3000/3000 行，**0 撕裂，0 重复**，各进程均 500 条 |

### 2.1 日志丢失

**未观察到丢失。** 侧车锁正确地串行化了 rename 链，`refresh_file_locked()` 保证写入前描述符始终指向当前 live 文件，因此不会持续写进已改名的备份。

唯一**理论上的错位窗口**（非丢失）：`rotate_lock_release_locked()`（`log.c:233`）到实际 `write()`（`log.c:246`）之间没有锁。若对端恰好在此窗口内完成一次轮转，本进程的记录会落入刚被改名为 `.1`/`.2` 的备份文件——记录仍在磁盘上，但不在"当前日志"里。窗口是几条指令，概率极低。

### 2.2 内容交错

**唯一的交错向量是短写重试。** `write_file_locked()`（`log.c:244-256`）在 `write()` 返回部分长度时循环续写：

```c
size_t off = 0;
while (off < len) {
  const ssize_t w = write(g_fd, msg + off, len - off);
  ...
  off += (size_t)w;
}
```

`O_APPEND` 只保证**单次** `write()` 的原子性。第二次 `write()` 会追加到当时的文件末尾——若对端在此刻插入了一条记录，本记录就被切成两半，夹在对端记录两侧。`LOG_MSG_MAX` 为 4096 字节，压测中 4KB 记录连续 3000 条也未触发短写（UFS/ZFS 上远低于原子写上限），因此**实际风险很低，但代码自身没有消除它**。

### 2.3 文件损坏

**结构上不可能。** 所有写入都是 `O_APPEND` 追加，没有 truncate、没有原地改写；轮转使用 `rename`（同目录内原子操作）。三个进程共享同一路径字符串，侧车锁路径一致，锁语义有效。最坏情况是记录错位或撕裂，不会是文件级损坏。

### 2.4 其他确认的问题

| # | 问题 | 严重度 | 位置 |
|---|------|--------|------|
| A | **阻塞式 `flock` 在持有 `g_lock` 时获取。** `rotate_if_needed_locked()` 由 `write_file_locked()` 在 `g_lock` 内调用，而 `flock(LOCK_EX)` 是阻塞的。若持锁对端被 `SIGSTOP` 或卡在慢速存储上，本进程**所有线程的日志**都会停住（不是死锁——fd 关闭时内核会释放锁——但会造成进程级停顿）。`onion_log_emergency` 的注释明确以"避免死锁"为由绕开锁，而正常路径反而引入了阻塞点。 | 中 | `log.c:227` |
| B | **崩溃日志无锁、无上限、写入者增加。** `OnionHEN_crash.log` 由 daemon / util / ShellUI / bootstrapper 四个进程写入，`onion_log_emergency()` 明确不加锁，且该文件**永不轮转**——`/data` 上的无界增长点。 | 中 | `log.c:394-415` |
| C | **ShellUI 配置的崩溃 sink 没有生产者。** `source/shellui/` 下 grep 不到 `sigaction` / `faulthandler` / `onion_log_emergency`，`prx.cpp:741` 的 `onion_log_configure_crash()` 目前是死配置（daemon 的信号处理器在另一个进程，接不住 ShellUI 的崩溃）。 | 低 | `prx.cpp:741` |
| D | **描述符未设 `O_CLOEXEC`。** `g_fd`、`g_crash_fd`、`g_rotate_lock_fd` 三处 `open()` 均无 `O_CLOEXEC`，而 `elfldr_spawn()` 使用 `rfork_thread(RFPROC \| RFCFDG \| RFMEM, ...)` 后立即 `execve(SceSpZeroConf, ...)`（`elfldr.c:659,627`）——这是代码库中唯一的 fork/exec 路径。泄漏的锁描述符可能把锁的生命周期延伸到无关子进程，泄漏的日志描述符会让已 unlink 的 `.3` 代无法回收。窗口窄，但修复成本为零。 | 低 | `log.c:120,154,337` |
| E | `onion_log_shutdown()` 在生产代码中**从未被调用**（仅测试调用），描述符与锁描述符常驻到进程退出。本身无害，但意味着显式释放路径无实际覆盖。 | 提示 | `log.c:350` |

### 2.5 已核查、确认无问题的项

- `elfldr_log.h` 把 `LOG_PUTS`/`LOG_PERROR`/`LOG_PT_PERROR` 映射到 `LOG_ERROR`（level 1），在默认运行时级别 `INFO`(3) 及任何更严格的设置下都保持可见；`LOG_PRINTF` → `LOG_INFO` 但**已无调用点**（仅宏定义）。原先"无条件 printf+klog"的行为变化实际无影响。
- `rotate_if_needed_locked()` 取锁后的 `>=` 重检与入口的 `<` 早退条件一致，逻辑自洽。
- `lock_path()` / `rotated_path()` 的缓冲区尺寸（`LOG_PATH_MAX+8` / `+16`）足够。

---

## 3. 按进程拆分 vs 生产级方案

### 3.1 拆分的实际收益

按进程拆分（`OnionHEN.log` / `ShellUI.log` / `Bootstrapper.log`）**确实能消除 1.1 节列出的全部并发问题**：

- 每文件单写者 → 2.2 的交错向量彻底消失（连短写重试也不再是问题）
- 不再需要跨进程轮转锁 → 2.4-A 的阻塞点、2.4-D 的锁描述符泄漏一并消失
- 不再有描述符失效 / inode 比对逻辑 → `refresh_file_locked()` 可以简化

### 3.2 拆分的代价

1. **破坏现有的支持流程。** `docs/arch.md` §3.3 与 `README.md` / `README_ZH.md` 都把 `OnionHEN.log` 定义为"主日志"并要求用户附带。拆成三份后，一份 bug 报告需要用户收集三个文件——这是真实的工作量增加。
2. **多份文件并不自动获得全局顺序。** 当前设计的资产是时间戳用 `CLOCK_MONOTONIC`（`format_stamp()`，`log.c:262`），**跨进程可比**，加上 tag 已能区分来源。也就是说，共享文件已经免费提供了近似全局有序；拆开后反而需要额外写一个合并工具来重建你原本就有的东西。
3. N 倍磁盘占用与 N 倍轮转 churn（每次轮转 3 次 rename）。

### 3.3 推荐方案：保留单文件，加固三处

**结论：不建议按进程拆分。** 当前设计在主导风险（内容交错、文件损坏）上已经是正确的——压测 3000 条 4KB 记录 0 撕裂。拆分是"用一个新问题换掉一个已经基本不存在的问题"，同时牺牲支持流程。

按优先级加固：

1. **让写路径"构造即原子"**（消除 2.2，最高性价比）
   一条记录只发一次 `write()`。短写时**丢弃余下部分并补一个截断标记**，而不是循环续写——对给人看的日志而言，一条截断记录比一条夹着别人记录的撕裂记录更可用。
   若必须保留完整记录，则把 `flock` 覆盖到写入本身，但仅对 `len` 超过某阈值（如 1KB）的记录加锁，避免每条日志一次 syscall。

2. **轮转锁改为非阻塞 + 有限退避**（消除 2.4-A）
   `flock(fd, LOCK_EX | LOCK_NB)` 加有界重试；取不到就跳过本轮轮转（下一批记录自然会再触发）。绝不在持有 `g_lock` 时无限等待。

3. **崩溃日志加轮转或上限**（消除 2.4-B）
   `OnionHEN_crash.log` 目前是唯一无界增长的文件，且写入者从 3 个变成 4 个。要么复用同一套轮转策略，要么在 `onion_log_configure_crash` 里记录初始大小并设硬上限。

4. **低成本改进（建议一并做）**
   - 记录头加入 PID：`[stamp] [tag/pid] LEVEL:` —— 多进程交织时可直接归因，几乎零开销。
   - 三处 `open()` 补 `O_CLOEXEC`。
   - `prx.cpp:741` 要么给 ShellUI 注册 faulthandler，要么删掉这行死配置（见 2.4-C）。

### 3.4 若将来必须拆分

推荐**混合**而非全拆：只把高噪声的 ShellUI 拆到 `OnionHEN_shellui.log`，daemon 与 bootstrapper 继续共享 `OnionHEN.log`。这样既隔离了最可能的噪声源，又保住了"用户只需附带一个主日志"的支持流程。

**不推荐** daemon 单写者 + IPC 转发：bootstrapper 在 daemon 启动之前就要写日志，且 ShellUI 运行在游戏进程内，IPC 可用性无法保证——会引入比现在严重得多的丢失路径。

---

## 4. 修复记录

按 3.3 的建议实施，未改动日志格式（记录头保持 `[stamp] [tag] LEVEL:`）。

| 文件 | 改动 |
|------|------|
| `log.c` | `write_file_locked()` 改为**每条记录一次 `write()`**，短写不再循环补写，只补一个换行保证行边界（消除 2.2） |
| `log.c` | `rotate_lock_acquire_locked()` → `rotate_lock_try_locked()`，`LOCK_EX \| LOCK_NB`；返回三态 `HELD`/`BUSY`/`UNAVAILABLE`。`BUSY` 时跳过本轮轮转（消除 2.4-A 的阻塞点），连续 8 次仍忙则判定对端已卡死，退化为无锁轮转以免文件无界增长 |
| `log.c` | 新增 `rotate_crash_if_oversized_locked()`：`onion_log_configure_crash()` 时若崩溃日志超过 1 MiB 则移到 `.1`（消除 2.4-B） |
| `log.c` / `log.h` | 三处 `open()` 补 `O_CLOEXEC`；新增 `ONION_LOG_DEFAULT_CRASH_MAX_BYTES`（消除 2.4-D） |
| `prx.cpp` | 移除无效的 `onion_log_configure_crash()`，改为注释说明 ShellUI 未注册 fault handler、加不加是独立决定（消除 2.4-C） |
| `test_platform_log.c` | 新增 `log.crash_sink_caps_oversized`；`cleanup_log_files()` 一并清理 `.1` |
| `log.h` | `ONION_LOG_DEFAULT_MAX_BYTES` 256 KiB → **768 KiB**，即单进程总保留量 1 MiB → **3 MiB** |
| `settings` / `log_settings` / `toolbox` | 新增 `[logging] max_bytes`：上限不再是硬编码，可由 `config.ini` 或 Toolbox 的「日志文件大小上限」选择框调整（256 KiB / 768 KiB / 2 MiB / 8 MiB 四档）。解析接受 `k`/`m` 后缀，并钳制到 64 KiB–64 MiB；`onion::apply_log_settings` 统一施加，daemon / util / ShellUI 三处调用点自动生效 |
| `arch.md` | §3.3 运行时路径与 libonion_platform 描述同步为新的并发语义 |

**未做**：记录头加 PID。tag 已经能区分进程，PID 只多区分「同一进程被重复注入」，收益不足以换一次用户可见的格式变更。

### 验证

- **宿主机单测**：`test_platform_log_suite` 17/17 通过（含新增用例）。
- **多进程压测**（修改后重跑）：小记录 4 进程 × 400 条，大记录 6 进程 × 500 条 × 4KB，覆盖 cap 默认 / 4KiB / 512B —— 全部 **0 撕裂、0 重复**。
- **保留量实测**：单进程连写 60000 条（约 80 B/条），保留 39306 条（record #20694 起），即 3 MiB 预算下约 3.9 万条历史；改动前 256 KiB 上限只能保留 11714 条。
- **PS5 目标构建**：`PS5_PAYLOAD_SDK=/Users/chenpy/sdk/ps5-payload-sdk LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config ninja -C build` 全量 140/140 通过，全部 ELF 重新链接成功。

> 注意：本机 `LLVM_CONFIG` 需指向 **llvm@18**。homebrew 默认的 llvm 23 会因 `-Wdeprecated-literal-operator`（`libhijacker/include/util.hpp:228`）和 `-Wc2y-extensions`（`freebsd-helper.h` 的 `__COUNTER__`）触发 `-Werror` 而构建失败——这两个都是既有代码问题，与本次改动无关。

---

## 附：复现压测

`/tmp/onion-log-stress/` 下的 `stress.c` / `stress2.c` / `stress3.c` 直接链接 `libonion_platform/source/log.c`，多进程各自 `onion_log_configure` 同一路径，校验行完整性、重复与各进程写入分布。
