# nProtect

nKits 的 TI-Nspire 原生文件浏览器访问保护。面向 CX / CX CAS OS 4.5.5.79、CX II / CX II CAS OS 6.4.0.74，需要 Ndless r2023。界面随计算器系统语言自动使用中文、英文或法文，其他语言使用英文。各型号验证情况见[项目说明](../README.md)。

## 使用

- 传入并运行 `nprotect.tns`，默认启用保护，秘密键是 `=`。
- 打开“浏览 / 我的文档”时，先按住秘密键，再选择入口。Home → 2、选中 Browse 后按 Enter 等入口使用同一规则。进入后可以松开秘密键，正常操作文件。
- 主菜单用 ↑/↓ 选择 **保护设置 / 关于 / 返回**，Enter 立即打开，Esc 返回。每次进入保护设置时，须按住当前秘密组合并选择 **解锁 / Unlock / Déverrouiller**；首次运行且无有效配置时免验证，保护关闭时也无需验证。原生表单中的 **保护** 复选框和 **密钥** 输入框分别控制开关与按键，输入框预填当前组合；选择系统的 **保存** 后才应用，**取消 / Esc** 不作修改并返回菜单。
- 自动启动：将程序放入 `/documents/ndless/startup/nprotect.tns`。开机按 `/documents/nprotect.cfg` 中的设置工作；首次或配置损坏时使用默认 `=`。移出 startup 后不会在下次开机自动加载。

CX 模拟器使用默认 `=` 时，如 Home → 2 后仍停在主菜单，继续按住 `=` 约 2 秒，松开后再按 Enter 显示文件列表。

组合使用 `+` 分隔，支持 1–4 个不同的按键，例如 `CTRL+SHIFT+A`。按键名称始终使用 ASCII，输入不区分大小写；保存新组合时必须实际按住它并选择 **确认 / Confirm / Confirmer**，取消或验证失败会保留全部原设置。重新启用保护还需确认，避免误锁。

支持 `=`、`CTRL`、`SHIFT`、`A`–`Z`、`0`–`9`，以及以下名称：

```text
PLUS MINUS MULTIPLY DIVIDE LP RP PERIOD SPACE VAR TRIG PI EXP
SQU EE CAT FRAC NEGATIVE DEL TAB
```

这些名称代表键帽按键，不是其 Shift 第二功能。组合需要同时按住，不能依靠 OS 的锁定修饰键。不要使用容易与打开浏览器动作重合的组合，例如单独的 `2`。确认新组合时也可检查键盘是否支持这些键同时按下。

Home 将 `A` 等键用作 Scratchpad 快捷键。使用这类秘密键时，先在其他页面按住，再回到 Home 选择浏览器。

## 注意

这是浏览器入口限制，不是加密或防篡改工具。它不保护“最近使用的文档”、文档标题、USB 访问或其他原生程序的文件选择器；已经打开的浏览器也不会被强制关闭。重启后未自动加载时不受保护。

程序不改名、不搬动、不修改用户文档。未知 OS 或冲突的浏览器入口不会被写入，程序会提示未启用保护。配置保存失败会提示本次设置仅在当前会话生效，下次启动可能恢复默认键。忘记组合时，停止自动启动并重启，移除 `/documents/nprotect.cfg` 后重新设置。

可与 nClock、nCapture、nHide 和 ThemeEditor 一起驻留。与 nHide 同时使用时，打开根目录须同时满足两者的按键要求。

## 构建

```sh
make
make host-test
```

输出为 `nprotect.tns`。

## 来源与许可证

行为参考 Excale 的 [nProtect v2.1](https://tiplanet.org/forum/archives_voir.php?id=17343) 和[原作者说明](https://tiplanet.org/forum/archives/zipview.php?id=17343&zipFileID=2)，以其公开功能和说明为依据独立重制。

本项目是 nKits 的组成部分，使用仓库根目录的 [GNU GPL v3.0](../LICENSE) 授权。
