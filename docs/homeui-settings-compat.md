# HomeUI / Settings compatibility handoff

本文是给后续 AI/维护者的适配流程文档。目标是把新固件的
`NPXS40002` HomeUI 顶部导航入口和 `NPXS40008` Settings DebugSettings
入口接入 OnionHEN，同时保持当前的 profile/strategy 表结构。

适配时不要把固件判断散落在 hook 里。新增固件应该尽量只扩展兼容表、
字节策略和测试。

## 输入文件

每个固件 dump 至少需要两个已解密 RNPS 文件：

| App | 作用 | 典型文件 |
|-----|------|----------|
| `NPXS40002` | HomeUI 顶部导航入口 | `NPXS40002.bin` |
| `NPXS40008` | Settings / DebugSettings bundle | `NPXS40008.bin` |

推荐目录形态：

```text
/path/to/12.02DUMP/
  NPXS40002.bin
  NPXS40008.bin
```

如果文件来自 PS5 机器，先用带解密功能的 FTP / ftpsrv 导出解密后的
`NPXS*.bin`。不要把未解密文件直接拿来做 profile。

## 已知复用

- `2.30`、`2.50` 的 `NPXS40002.bin` 完全一致（SHA-256
  `3a50628e07431ae4eadfbda45cfc6882a4558c6f548382bc6bab81d769cccf4b`）。
  这两个 HomeUI 没有后续固件的 `Fps` / `ApplicationErrorEventTrigger`
  导航宿主，因此复用调试专用 `StartupAnimation` 槽位并保留 Search、Settings、
  Profile。两个 archive 内的 `NPXS40008.bin` 均为 0 字节，没有可适配的
  Settings dump。
- `3.00`、`3.10`、`3.20`、`3.21` 的 HomeUI 与 Settings dump 分别完全
  一致。HomeUI payload size 为 `0x150130`，SHA-256 为
  `21c91c7044c0d36ee26827260e956db085096fd93264f6d5cb5c0255fc8dcf6f`；
  Settings payload size 为 `0x457210`，使用 standard route。
- `4.03`、`4.50`、`4.51` 的 `NPXS40002.bin` 整文件完全一致（SHA-256
  `6db944372cfe8b7d50328ed4bd47c8cae6917821fb30f20004e7f85c673fe00a`）。
  `4.00`、`4.02` 也与它们一致。这些版本使用旧 RNPS JavaScript bundle，
  不是 Hermes HBC，共用一个 legacy HomeUI profile。
- `5.00`、`5.02` 的 `NPXS40002.bin` 完全一致，payload size 为
  `0x17a690`，SHA-256 为
  `a0be894ce20f2769f43715b0ac75c135d9d0811d6d26d061d311671505b3d66f`；
  两者的 Settings dump 也完全一致，payload size 为 `0x4b8770`。
- `5.10` 的 `NPXS40002` 是独立 legacy RNPS JavaScript bundle，payload size
  为 `0x185c30`，SHA-256 为
  `bfa53c6bd1fd4c468ebf7ee44955db0cf8286f116a8ab1870b756de0efbfc5fb`。
  `5.50` HomeUI 与其完全一致，两者共用 HomeUI profile；AppError 组件源码
  形态与 6.x/7.x 一致。
- `6.00`、`6.02` 的 `NPXS40002.bin` 完全一致（SHA-256
  `b376f7ead9140636beac99354a7d958fc18ea4da40580dda590927be936e0a18`），
  是 payload size `0x185d00` 的 legacy RNPS JavaScript bundle，共用一个
  HomeUI profile。其 AppError 组件源码形态与 7.40/7.61 一致，但整体偏移不同。
- `7.40`、`7.61` 的 `NPXS40002.bin` 完全一致（SHA-256
  `1f884701ec9a490b8149f9873ead915fc2d2d5ee95f55363beff39cc5872e93e`），
  是 payload size `0x19dc10` 的 legacy RNPS JavaScript bundle，共用 HomeUI
  profile。
- `8.00`、`8.40` 的 `NPXS40002` 是直接存放 minified JavaScript 的 RNPS
  payload，大小均为 `0x158070`。两者在 `payload+0x157ef0` 前内容一致，
  差异只位于尾部签名区，因此共用一个 plain-JS HomeUI profile。
- `9.00` 的 `NPXS40002` 使用 Hermes v89，`hbc_file_length=0x1846e0`、
  `source_hash=587635687e0a190e38425232c39092888da5adbe`，使用独立 HomeUI
  profile。
- `4.03` 的 `NPXS40008` 使用 legacy bundle，payload size 为 `0x483280`；
  `4.50`、`4.51` 对应文件完全一致，payload size 为 `0x483fc0`。三者均
  使用 standard route，并通过已知 payload size 和目标字节共同识别。
- `5.10` 的 `NPXS40008` 使用 payload size `0x4b8ac0` 的 legacy bundle，
  SHA-256 为
  `f6fd5f1aba8de0ee3f56e821c69dcea795dec461998db6082a4f58a33ba88ad7`，
  使用 standard route 和独立定点 profile。
- `6.00`、`6.02` 的 `NPXS40008.bin` 完全一致（SHA-256
  `2acc1cfc8421c6cb24c25ad29b8433040f79043a068dd941cbf29e2e7daabc0d`），
  payload size 为 `0x5524a0`，共用 standard-route legacy Settings profile。
- `7.40`、`7.61` 的 `NPXS40008.bin` 完全一致（SHA-256
  `469f723359ec07ab6ab72089328401301eb6f32dbc4a6776bf1421c7195ce853`），
  使用 payload size `0x5e9d20` 的 legacy bundle，共用 standard-route
  定点 profile。
- `5.50`、`6.50`、`7.20` 的 Settings dump 使用各自的 legacy profile；
  `7.00`、`7.01`、`7.01.01` 的 Settings dump 完全一致并共用 profile。
  `7.60` 复用 `7.40/7.61` profile，`8.20`、`8.20.02` 复用 `8.40`
  profile。
- `8.00`、`8.40` 的 `NPXS40008` 仍使用 legacy bundle，payload size 分别为
  `0x64bb80`、`0x654af0`，均使用 standard route 和各自的定点 profile。
- `9.00` 的 `NPXS40008` 使用 Hermes bundle，`hbc_file_length=0x4b1934`、
  `source_hash=72188b52b12bad6af90c90a848b7fd76e5af102d`，route 为 standard。
- `10.2DUMP` 的 `NPXS40002`、`NPXS40008` 分别与 10.4 对应文件整文件
  完全一致，直接复用 `10.4/10.6` HomeUI profile 和 10.4 Settings
  fingerprint；Settings route 为 standard。
- `11.2DUMP` 的 `NPXS40002` 与 11.4/11.6 完全一致；`NPXS40008`
  使用独立 old-route 指纹：`hbc_file_length=0x4f45b8`、
  `source_hash=d03462a912c4b5b8db4a98d044b9d488a2dffc7a`。
- `11.40DUMP` 的 `NPXS40002` HBC 与 11.6 完全一致，复用
  `11.4/11.6` HomeUI profile。`NPXS40008` 使用独立 Settings 指纹：
  `hbc_file_length=0x4f45c4`、
  `source_hash=a7b731571f84b6cdaf7c4227a980ba5ee20004a8`，route 为 old。
- `NPXS40009` 不包含 `debug_settings` / `debug_settings_old`，不能作为
  Settings profile 指纹来源。
- `12.4DUMP` 的 `NPXS40002` 与 12.7、`NPXS40008` 与 12.20 分别整文件
  完全一致，复用对应 HomeUI profile 和 Settings fingerprint；route 为 old。
- `12.6DUMP` 的 `NPXS40002` 与 12.7 完全一致；`NPXS40008` 使用独立
  old-route 指纹：`hbc_file_length=0x4e9028`、
  `source_hash=75747bb5fa7e3a4e22d557882f5281e4d1f12959`。
- `13.0`、`13.2`、`13.4`、`13.42`、`13.6` 五个 dump 的 `NPXS40002.bin`
  **整文件完全一致**（SHA-256
  `6f9ee7f274d286a6d2edf38e9379c149c9a8938ca0036fbb2e268cbb80698284`，
  均为 1798704 字节），因此**不新增 HomeUI profile**，五者共用
  `13.0/13.2/13.4/13.42/13.6 NPXS40002 HomeUI`，nav 偏移完全相同。
- `13.600.007` 实机 ShellUI 读的是
  `/mnt/rnps/apps/NPXS40002/application.ps.bundle`（OTA，`rnps-home`
  `13.2.0+23527`）。解密后的 `NPXS40002.mnt.bin` 是另一份 HBC：
  `file_length=0x1b6594`、
  `source_hash=f73c3e0102be92a4558baa510bd00c17f6fe5f96`。它单独使用
  `13.600.007 OTA NPXS40002 HomeUI`，不要改上面那条 `0x1b6584` profile。
  `requires_image_source_object` 保持 false。
- 13.x 的 `NPXS40008` 分成三支，各自需要一条 old-route 指纹：

  | 固件 | `hbc_file_length` | `source_hash` |
  |------|-------------------|---------------|
  | `13.0` | `0x4eb9b4` | `5fb007c00f7ba34541032b2997bdabd19d534e78` |
  | `13.2`、`13.4`、`13.42` | `0x4ebe08` | `fb05751464291761de1fdcd9576fc26a02930e10` |
  | `13.6` | `0x4ebc8c` | `a400a5ca932efc80cb6512610903100fa03a8717` |

  三支的 Hermes HBC 内 `☆Debug Settings`（UTF-16LE，含 `0x2605` 前缀）都只
  出现 1 次、`icon_setting` 也都只出现 1 次，满足
  `settings_bundle_patch.cpp` Hermes 分支的定点等长替换条件，无需额外的
  label/icon offset。注意三支的 `hbc_file_length` 只差几十到几百字节，
  必须同时匹配 `source_hash`，不能只看长度。
- `13.42DUMP` 与 `13.4DUMP` 在 `NPXS40002` / `NPXS40003` / `NPXS40008` /
  `NPXS40009` 四个 app 上逐一一致，说明 13.42 只是沿用 13.40 的 HomeUI 与
  Settings bundle，差异不在这些组件里。

## 第一轮识别

先用仓库脚本识别 RNPS payload 并提取指纹和关键字符串：

```sh
python3 scripts/analyze_rnps_dump.py /path/to/DUMP --allow-unsupported
```

Hermes bundle 会输出：

- RNPS 内 HBC offset
- HBC version
- `hbc_file_length`
- `source_hash`
- 已知 profile 是否命中
- 关键字符串 offset
- Settings route 推断：`standard` / `old`

3.x/4.x/5.x/6.x/7.x/8.x legacy Settings 与 2.x/3.x/4.x/5.x/6.x/7.x
HomeUI 会改为输出：payload offset、旧 bundle magic、
payload size、整文件 SHA-256、匹配的 legacy profile 和关键源码字符串
offset；Settings 还会输出 profile 对应的 route。

8.x plain-JS HomeUI 会输出 payload offset、payload size、整文件 SHA-256、
匹配的 plain-JS profile 和关键源码字符串 offset。

如果只想给其他工具消费：

```sh
python3 scripts/analyze_rnps_dump.py /path/to/DUMP --json --allow-unsupported
```

判断分支：

- `NPXS40002` 已命中 HomeUI profile：通常不用新增导航 profile。
- `NPXS40002` 未命中：需要新增 HomeUI profile 和 byte set。
- `NPXS40008` 未命中：需要新增 Settings fingerprint。
- Settings route 推断必须和 profile route 一致。

12.0/12.02 适配记录：两个版本的 `NPXS40002` dump 与现有 12.20
HomeUI profile 完全一致，直接复用；`NPXS40008` 也使用同一份 bundle
指纹和 old route：
`hbc_file_length=0x4e7bec`、
`source_hash=fc7c4f15af42929e1d52420c2d174944b4a88043`。

## Settings 兼容

相关文件：

- `source/include/onion/debug_settings_route_policy.hpp`
- `source/shellui/src/settings_bundle_patch.cpp`
- `source/util/tests/test_debug_settings_route_policy.cpp`
- `scripts/analyze_rnps_dump.py`

当前规则：

| 固件范围 | route | URI |
|----------|-------|-----|
| `3.00` 到 `8.60` | `standard` | `function=debug_settings` |
| `9.00` 到 `10.6` | `standard` | `function=debug_settings` |
| `11.x` 及以上 | `old` | `function=debug_settings_old` |

`9.00` 及以上的 Hermes Settings bundle 使用 `hbc_file_length` 和
`source_hash` 指纹。Hermes 字符串 entry 会共享底层存储区，不能对
`icon_setting` 做裸字节全局替换，否则会同时破坏 `_settingInstance` 和相邻
asset path。Hermes 分支只执行标签等长替换，随后重算 HBC footer SHA-1；
`icon_setting` 资源文件保持 Sony 原文件，由 `SettingsPlugin.CxmlUri` hook
在 ShellUI 运行期间将该 URI 临时拦截到
`/system_ex/vsh_asset/onionhen.png`。插件未运行时不会留下任何图标改动。

`3.x/4.x/5.x/6.x/7.x/8.x` Settings 是 pre-Hermes RNPS JavaScript bundle，不能加入 Hermes 指纹表；
它由 `settings_bundle_patch.cpp` 的 legacy profile 按 payload size 和目标 offset 原字节
识别，只执行菜单标签的定点等长替换；图标 ID 保持 `icon_setting`，由同一个
`SettingsPlugin.CxmlUri` hook 运行时拦截：

```text
★Debug Settings -> ★OnionHEN Tools
icon_setting     --(runtime URI interception)--> /system_ex/vsh_asset/onionhen.png
```

旧版如果已经把系统上的 `icon_setting.png` 覆盖为 OnionHEN 图标，新版只能阻止
后续继续覆盖，无法从二进制中推回已丢失的 Sony 原图；这类机器需要从对应固件
的 stock 资源恢复该文件。

legacy 标签在 bundle 内有多个文本命中，必须只修改 categoriesList 菜单项
profile 指定的 offset，不能全局替换错误提示中的同名文本。重复执行 patch
时，新标签也应被 profile 接受，但不得再次修改 bundle；图标资源和图标 ID
均不得被写回系统文件或 bundle。

新增 Hermes Settings 兼容时，不只看 `hbc_file_length`。必须同时匹配：

- `hbc_file_length`
- HBC `source_hash`

这样能避免误匹配其他 Settings bundle。

添加步骤：

1. 从 `analyze_rnps_dump.py` 输出复制 `hbc_file_length` 和 `source_hash`。
2. 按 route 放入：
   - `kStandardSettingsBundles`
   - `kOldRouteSettingsBundles`
3. 在 `scripts/analyze_rnps_dump.py` 的 `KNOWN_SETTINGS_PROFILES` 加同一条。
4. 在 host test 中新增 bundle hash 测试。
5. 如版本边界变了，调整 `kCompatibilityProfiles` 的 `{min, max, route}` 范围，
   并新增版本路由测试。

注意：`debug_settings_old` 包含 `debug_settings` 子串，所以脚本里看到
`debug_settings` count 不为 0 并不代表 route 是 standard。以
`debug_settings_old` 是否存在为优先判断。

## HomeUI 兼容

相关文件：

- `source/shellui/src/homeui_top_nav_patch.cpp`
- `source/shellui/src/homeui_top_nav_profiles.inc`
- `scripts/analyze_rnps_dump.py`
- `scripts/verify_homeui_top_nav_fixes.py`

HomeUI 有三类 bundle：

- 9.00 及当前已知的新固件使用 Hermes HBC，由固件 profile 描述 offset
  和字节。
- 2.30/2.50、3.00-3.21、4.00-4.51、5.00-7.61 使用旧 RNPS JavaScript
  bundle，由各自的 legacy
  profile 执行等长源码替换；不能把它当作 HBC profile。
- 8.00-8.60 使用 RNPS 内的明文 minified JavaScript，由 plain-JS profile
  按 payload size、固定 marker 和目标原字节共同识别。

Hermes 兼容由两层组成：

- `HomeUiPatchProfile`：固件 HBC 指纹和所有 patch offset。
- `HomeUiPatchBytes`：该固件对应的字节替换策略。

新增固件不要直接在 patch 流程里写 `if (version == ...)`，而是新增
`HomeUiPatchBytes` 和表项。

安全的导航结构统一为：

```text
[Search, ApplicationErrorEventTrigger, Settings, Profile]
```

OnionHEN 使用原本 77 字节的 `ApplicationErrorEventTrigger` 按钮函数作为
宿主，`Fps` 保持原实现。不要再劫持 `Fps` 函数体；它在游戏退出、HomeUI
重新挂载时会参与恢复流程，旧方案曾引发 RN JS executor 崩溃。

### 旧 RNPS JavaScript bundle（2.x 到 7.x）

旧 bundle magic 为 `e5 d1 0b fb`，位于 RNPS payload offset（当前 HomeUI
dump 都是 `0xb20`）。3.x 到 7.x 的源码变量及 offset 不同，但兼容策略相同，
均采用三处等长替换：

```text
["Fps","Search","Settings","Profile"]
→ ["Search","App","Settings","Profile"]

t.Fps=P
→ t.App=h
```

同时把 AppError 的 383 字节源码块改成
`useInteractivePress({link:"OnionHEN?NavUI=1"})` 按钮，设置
`iconId:{uri:"/system_ex/vsh_asset/onionhen.png"}`、空标题，并用空格填满剩余
字节。4.x 的 `SystemIcon` 会把 `iconId` 原样传给 `PUI Button.icon`，本地文件
必须使用 ImageSource 对象，不能直接传路径字符串。
旧 bundle 没有 Hermes footer SHA-1，不能调用 HBC footer 更新逻辑。

2.30/2.50 是例外：它们没有 `Fps` / `ApplicationErrorEventTrigger`，使用
`StartupAnimation` 调试按钮作为宿主，导航顺序改为
`[OnionHEN, Search, Settings, Profile]`。替换仍为定点、等长修改，并保留其余
导航项。

### 明文 minified JavaScript bundle（8.x）

8.00/8.40 的 payload 以 `/*! For license information` 开头，不带
`e5 d1 0b fb` magic。补丁保持 bundle 长度不变，执行三处等长替换：

```text
["Fps","Search","Settings","Profile"]
→ ["Search","App","Settings","Profile"]

t.Fps=I
→ t.App=b
```

原 328 字节 `ApplicationErrorEventTrigger` 源码块替换为
`useInteractivePress({link:"OnionHEN?NavUI=1"})` 按钮，使用
`iconId:{uri:"/system_ex/vsh_asset/onionhen.png"}`，剩余空间填 ASCII 空格。
`Fps` 组件实现本体必须保持不变。

### 1. 提取 HBC

分析脚本会告诉你 HBC offset。需要反汇编时可临时抽出 HBC：

```sh
python3 - <<'PY'
from pathlib import Path

src = Path("/path/to/DUMP/NPXS40002.bin")
out = Path(".tmp/hbc/new_fw.hbc")
magic = bytes([0xc6, 0x1f, 0xbc, 0x03, 0xc1, 0x03, 0x19, 0x1f])
data = src.read_bytes()
off = data.find(magic)
if off < 0:
    raise SystemExit("HBC magic not found")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(data[off:])
print(f"HBC offset=0x{off:x} out={out}")
PY
```

反汇编：

```sh
/Users/chenpy/.local/bin/hbc-disassembler .tmp/hbc/new_fw.hbc .tmp/hbc/new_fw.dis
```

`.tmp/` 是临时分析目录，不要提交。

### 2. 填 HomeUiPatchProfile

从分析脚本和反汇编结果填：

| 字段 | 来源 |
|------|------|
| `hbc_version` | HBC header offset `0x08` |
| `file_length` | HBC header offset `0x20` |
| `source_hash` | HBC header offset `0x0c`，20 bytes |
| `title_id` | 字符串 `NPXS40002` offset |
| `app_error_event_trigger` | 字符串 `ApplicationErrorEventTrigger` offset |
| `navigate_to_home` | 字符串 `pshomeui:navigateToHome` 第一个 offset |
| `download_error_string` | 字符串 `download_error` offset |
| `custom_icon_uri` | 字符串 `homeui ApplicationErrorEvent test` offset |
| `top_nav_link_uri` | 字符串 `Trigger AppError` offset |

剩余字段需要二进制/反汇编定位：

| 字段 | 定位方式 |
|------|----------|
| `home_icon_order` | 找 top nav array bytes，通常旧顺序是 `Fps, Search, Settings, Profile` |
| `fps_factory` | top-nav module 中 Fps factory / export 相关字节 |
| `custom_icon_value` | AppError object buffer 里 `download_error` string id 的 2-byte 值 |
| `custom_title_value` | AppError object buffer 里 `Trigger AppError` string id 的 2-byte 值 |
| `fps_body` | Fps function bytecode 起始 offset |
| `app_error_body` | `ApplicationErrorEventTrigger` 的 77-byte function 起始 offset |
| `app_error_props_helper_body` | 仅旧 PUI 需要；被替换后不再使用的 AppError `onPress` function offset，否则填 `0` |

### 3. 导出字符串 ID

在反汇编里找 top nav module 和相关函数：

```sh
rg -n -C 8 "ApplicationErrorEventTrigger|PutById.*Fps|Function #|useInteractivePress|Object: \\{'iconId'" .tmp/hbc/new_fw.dis
```

常用 string id：

- `ApplicationErrorEventTrigger`
- `Fps`
- `Search`
- `Settings`
- `Profile`
- `homeui ApplicationErrorEvent test`
- `Trigger AppError`
- `useInteractivePress`
- `link`
- `jsx`
- `default`
- `onPress`

这些 ID 决定 icon order、对象表替换值和 Fps body replacement。

### 4. 定位 home icon order

目标是把 `ApplicationErrorEventTrigger` 放到 `Search` 后面作为 OnionHEN
入口，并保留 `Fps` 原实现。

旧形态一般是：

```text
[Fps, Search, Settings, Profile]
```

新形态：

```text
[Search, ApplicationErrorEventTrigger, Settings, Profile]
```

用 string id 组成 HBC 里的 `NewArrayWithBuffer` 字节序列搜索。例如某固件：

```text
54 <Fps id le16> <Search id le16> <Settings id le16> <Profile id le16>
```

搜索命令示例：

```sh
python3 - <<'PY'
from pathlib import Path
h = Path(".tmp/hbc/new_fw.hbc").read_bytes()
patterns = {
    "old": bytes.fromhex("54 e3 1b 4d 1c 4e 15 85 16"),
    "new": bytes.fromhex("54 4d 1c 3e 16 4e 15 85 16"),
}
for name, pat in patterns.items():
    print(name, hex(h.find(pat)))
PY
```

只在唯一 offset 命中时写入 profile。

### 5. 定位 AppError object buffer

在反汇编里找：

```text
Object: {'iconId': 'download_error', 'onPress': null, 'title': 'Trigger AppError'}
```

然后在 HBC 原始字节里找对象表附近的 string id：

```text
... 51 <download_error id le16> 01 51 <Trigger AppError id le16> ...
```

`custom_icon_value` 指向 `<download_error id le16>`，replacement 指向
`homeui ApplicationErrorEvent test` 的 string id。之后同一字符串内容会被
替换为 `/system_ex/vsh_asset/onionhen.png`。

`custom_title_value` 指向 `<Trigger AppError id le16>`，replacement 为
`ff 00`，用于显示空标题。

不要只靠肉眼偏移。必须用 Python 检查当前位置的 old bytes 是否符合预期。

### 6. 写 AppError body replacement

使用 `ApplicationErrorEventTrigger` 的完整 77 字节函数作为按钮宿主：

- `useInteractivePress({ link: "OnionHEN?NavUI=1" })`
- `jsx(default, { iconId, onPress, title })`

需要根据新固件 string id 修改 body bytes 中这些位置：

- `useInteractivePress`
- `Trigger AppError` 字符串 ID，后续该字符串内容被替换为
  `OnionHEN?NavUI=1`
- `link`
- `jsx`
- `default`
- object buffer pair
- `onPress`

replacement 必须和 stock AppError body 等长，目前各 profile 都是 77 bytes。
固定数组长度会由编译器校验。`old_fps_body_prefix` 只用于修复历史版本残留的
Fps-body 劫持；新 profile 不能把 OnionHEN body 写到 `fps_body`。

`11.4/11.6` 使用经过验证的单函数形态：AppError 主函数直接调用
`useInteractivePress`，图标值保持 raw path，不改写相邻 `onPress` helper。
9.00 同样使用这个形态，避免 reload 后执行第二个手写 Hermes 函数造成回归。

`requires_image_source_object=true` 仅保留为未来固件的受控 fallback。它会把原
AppError `onPress` 函数（替换主函数后已无调用者）复用为 props factory，动态
生成：

```text
{iconId: {uri: "/system_ex/vsh_asset/onionhen.png"}, onPress, title: ""}
```

主函数保持 77 bytes，helper 保持 76 bytes。没有真机验证和完整反汇编证据时，
不要启用该标志。

### 7. 临时 patch 并反汇编验证

写进源码前，先对 `.tmp/hbc/new_fw.hbc` 做临时 patch，更新 HBC footer SHA1，
再反汇编。

验证至少包含：

```sh
rg -n "Array: \\['Search', 'ApplicationErrorEventTrigger', 'Settings', 'Profile'\\]" .tmp/hbc/new_fw_patched.dis
rg -n "Object: \\{'iconId': '/system_ex/vsh_asset/onionhen.png', 'onPress': null, 'title': ''\\}" .tmp/hbc/new_fw_patched.dis
rg -n "String: 'OnionHEN\\?NavUI=1'" .tmp/hbc/new_fw_patched.dis
```

对 `requires_image_source_object=true` 的 profile，第二条改为确认 helper 中依次
出现 `NewObject`、`uri`、`iconId`、`onPress`、`title`，并确认
`LoadConstString` 引用 `/system_ex/vsh_asset/onionhen.png`。

如果反汇编失败，或者对象/路由没有按预期出现，不要把该 byte set 写进源码。

## 脚本同步

每次新增兼容 profile，都同步 `scripts/analyze_rnps_dump.py`：

- `KNOWN_HOMEUI_PROFILES`
- `KNOWN_LEGACY_HOMEUI_PROFILES`
- `KNOWN_SETTINGS_PROFILES`

这个脚本既是提取工具，也是回归验证工具。只有 HomeUI 在本次适配范围内时，
可带 `--allow-unsupported` 保留未支持 Settings 的诊断；两个 App 都完成后，
脚本应能在不带该参数时返回 PASS。

## 测试矩阵

最低验证清单：

```sh
python3 scripts/analyze_rnps_dump.py /path/to/new/DUMP --allow-unsupported
python3 scripts/analyze_rnps_dump.py /path/to/known/10.01DUMP
python3 scripts/analyze_rnps_dump.py /path/to/known/10.6DUMP
python3 scripts/analyze_rnps_dump.py /path/to/known/11.6DUMP
python3 scripts/verify_settings_bundle_patch.py
python3 scripts/verify_homeui_top_nav_fixes.py
python3 scripts/analyze_rnps_dump.py /path/to/known/12.7DUMP

python3 -m py_compile scripts/analyze_rnps_dump.py \
  scripts/verify_settings_bundle_patch.py \
  scripts/verify_homeui_top_nav_fixes.py
git diff --check
make -C source/util/tests test
cmake --build build --target shellui -j 8
cmake --build build --target bootstrapper -j 8
cmake --build build --target daemon -j 8
```

如果新增了 HomeUI byte set，还要保留临时 patch 的反汇编检查记录在交接说明
或 PR/commit 描述里。

## 常见坑

- `debug_settings_old` 包含 `debug_settings` 子串，route 推断要优先看 old。
- `NPXS40002` 的 file size 和 HBC `file_length` 不是同一个字段，profile 用
  HBC `file_length`。
- HomeUI string offset 是 HBC 内 offset，不是 RNPS 文件内 offset。
- 4.x legacy offset 是相对旧 JavaScript payload 起点，不是 RNPS 文件起点；
  旧 bundle 不更新 Hermes footer SHA-1。
- `custom_icon_value` / `custom_title_value` 是 object buffer 中的 2-byte
  string id 位置，不是字符串内容 offset。
- 替换字符串必须等长：
  - `homeui ApplicationErrorEvent test`
  - `/system_ex/vsh_asset/onionhen.png`
  - `Trigger AppError`
  - `OnionHEN?NavUI=1`
- Runtime patch 会更新 HBC footer SHA1；临时 HBC 验证也要更新 footer。
- 顶部导航按钮使用 `ApplicationErrorEventTrigger` 作为宿主；不要重新劫持
  `Fps` 函数体。
- `.tmp/hbc/*`、反汇编输出、candidate HBC 都是分析产物，不提交。

## 交接模板

给下一个 AI 的最小交接信息：

```text
固件版本：
Dump 目录：

NPXS40002:
- hbc_version:
- hbc_file_length:
- source_hash:
- 是否复用已知 HomeUI profile:
- 如新增 profile，列出 offsets:

NPXS40008:
- hbc_file_length:
- source_hash:
- route: standard / old

HomeUI 临时 patch 反汇编检查:
- nav order:
- icon object:
- OnionHEN?NavUI=1:

已运行验证:
- analyze_rnps_dump.py new dump:
- known dump regressions:
- host tests:
- shellui build:
- daemon build:
```
