# nEdit

TI-Nspire CX/CX II + Ndless 原生纯文本编辑器。支持 UTF-8 数学与希腊符号、计算器按键输入、文件选择和安全保存。

## 架构

```text
Ndless keys -> input -> app -> editor -> text_buffer -> file
                         |         |
                         |         +-> cursor/viewport
                         +-> ui -> common/draw + ngc
```

- `text_buffer`：可增长 gap buffer，支持插入、退格、删除和流式安全保存。
- `editor`：UTF-8 字符级光标、删除、行列导航、翻页、Go To Line、水平与垂直视口；文件偏移按 byte 计。
- `input`：TI-Nspire 实体键、Unicode 字符和 Ctrl/Shift 组合键映射。
- `ui`：独立 OS GC、系统字体符号面板、编辑菜单、Open 首页、文件浏览器、保存确认、Help/About；路径等输入保留系统原生对话框。
- `app`：文件生命周期、事件循环和未保存修改处理。

保存不依赖 Ndless 上可能返回 `ENOSYS` 的 `rename()`：先写完整临时文件，再备份目标并复制安装；失败时恢复 `.nedit.backup`。

Open 页面的 Browse 会显示目录中的所有普通文件，不限制 `.txt` 后缀；新建文件路径也允许任意后缀。

Open 首页提供 `Browse`、`Path`、`Create`、`Help`、`About` 和 `Exit nEdit`；编辑中 `Ctrl+N` 可直接创建文件。

保存或另存为成功后回到 Open 首页。编辑页按 `Esc` 也回到首页；有修改时先选择保存、放弃或取消。取消或保存失败时保留当前编辑状态。在首页按 `Esc` 或选择 `Exit nEdit` 才退出程序。打开失败时可重新选择文件。

所有自绘页面采用 nTexts 风格的白底、系统字体、浅蓝整行选择和底部按键提示。文本用 Regular9 逐字符绘制；字符格宽与可见行数按字体实测尺寸计算，横向滚动及翻页跟随实际视口，不改变文件内容。编辑菜单和文件浏览器支持滚动选择，Help 分两页。

文本按 UTF-8 字符移动、退格和向前删除，一次处理一个 Unicode 码点；保存保留原始 byte，非法 UTF-8 byte 逐个保留和编辑。列号按字符计，`Off` 和 `Size` 按 byte 计。

## 特殊符号与编辑菜单

`Catalog`、`Trig`、`Var` 或编辑菜单的 `Insert symbol` 打开符号面板。方向键选择，`Tab` 翻页，`Enter` 或触摸板点击插入，`Esc` 取消。

面板覆盖全部 ASCII 标点（包括 `[]{}\|~` 和反引号）、24 个小写希腊字母、`ΓΔΘΛΞΠΣΦΨΩ`，以及 `°±²³×÷√∞≤≥≠≈∫∂∇∑∏←→⇒·`。符号和其他 Unicode 码点使用系统字形；无论系统是否有对应字形，仍可整字符移动、删除和原样保存。

编辑页 `Menu` 提供符号输入、保存并返回、另存为并返回、打开、新建、重新载入、跳转行、向前删除、文件开头/末尾、帮助、关于和返回首页。用上下方向键选择，`Enter` 或触摸板点击执行，`Esc` 回到编辑页。

## 快捷键

| 按键 | 操作 |
|---|---|
| Arrows | 移动光标 |
| Shift + Up/Down | 上/下一页 |
| Ctrl + Left/Right | 行首/行尾 |
| Ctrl + Up/Down | 文件开头/末尾 |
| Ctrl + Shift | 切换 Caps Lock |
| 连按 Ctrl 5 次 | 开启/关闭 Sticky Keys |
| Ctrl + N | Create File |
| Ctrl + O | Open |
| Ctrl + S | 保存并返回首页 |
| Ctrl + Shift + S | 另存为并返回首页 |
| Ctrl + R | Reload |
| Ctrl + G | Go To Line |
| Enter | 插入换行 |
| Del | 退格删除 |
| Ctrl + Del | 删除光标后的整个字符 |
| Tab | 插入 4 个空格 |
| Catalog / Trig / Var | 符号选择面板 |
| π / Shift + π | π / Π |
| x² / Shift + x² | ² / √ |
| EE | e（支持 Shift / Caps Lock） |
| xʸ | ^ |
| 负号 / Shift + 负号 | - / _ |
| ( / ) | 左/右圆括号；Shift 输入 `{` / `}` |
| Shift + 0…9 | 依次输入 `) ! @ # $ % ^ & * (` |
| Shift + ÷ | 反斜杠 `\` |
| Shift + × | ~ |
| Shift + = | 反引号 |
| Menu | 编辑操作菜单 |
| Esc | 编辑页返回首页；首页退出；对话框取消 |

Sticky Keys 默认关闭。开启后可先按下并释放 `Ctrl` 或 `Shift`，再按功能键；
关闭时组合键必须在修饰键保持按下期间触发。

Clickpad 型号的独立 `θ` 键输入 `θ`，Shift 输入 `Θ`；分数键输入 `/`，Shift 输入 `÷`。CX / CX II 可通过符号面板输入这些符号。

## 构建

```sh
cd /mnt/c/Users/baizi/Documents/GitHub/nKits/nEdit
make
make host-test
```

## 许可证

nEdit 是 nKits 的组成部分，使用仓库根目录中的
[GNU General Public License v3.0](../LICENSE) 授权。
