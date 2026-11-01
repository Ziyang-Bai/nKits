# ThemeEditor

nKits 的 TI-Nspire 原生系统配色编辑器。面向 CX / CX II 的 CAS 与非 CAS 型号，需要 Ndless r2022；CX 使用 OS 4.5.5.79，CX II 使用 OS 6.4.0.74。各型号验证情况见[项目说明](../README.md)。

## 功能

- 浏览当前系统的全部命名色项、颜色预览、RGB 十进制和十六进制编辑。
- 单项或整组反色、随机配色；CX 可分别编辑彩色与黑白列。
- 打开、保存、另存为 `.theme.tns`；可读取原版主题，跨系统时忽略不存在的色项。
- 新建主题、恢复首次运行时的原始颜色；退出后系统继续使用已应用的颜色。
- 系统“设置”菜单入口；列表、实时对话框、两者结合三种模式。
- 记住主题路径、选中项、界面模式和菜单入口选项。

## 使用

打开 `themeeditor.tns`。默认进入列表；选中色项后按 Enter 编辑，确认后应用。
按 Esc 保存当前主题并退出；首次修改默认保存到 `mytheme.theme.tns`。
按 Menu 打开“Colors / Files / Options”，可另存为、新建、恢复原色或卸载。
实时模式中，每次确认一个 RGB 分量即刻应用；Esc 不撤销已经确认的改动。

| 按键 | 列表 | RGB 拾色器 |
| --- | --- | --- |
| ↑ / ↓ | 上一项 / 下一项；Shift 加速 | 调整当前分量，Shift 加速 |
| ← / → | 横向滚动名称 | 选择 R / G / B |
| Enter | 编辑选中色项 | 确认并应用 |
| Esc | 保存并退出 | 取消这次拾色 |
| 0–9、A–F | — | 输入当前分量的两位十六进制值 |
| I / R | 整组反色 / 随机 | 当前颜色反色 / 随机 |
| Tab | 切换彩色 / 黑白列（CX） | — |
| Menu | 文件、实时编辑、选项 | — |

“Options → UI”选择 Both、Live 或 List。默认 Both：文件进入列表，
“主屏幕 → 设置 → Theme Editor”进入实时对话框。
实时对话框按编号选择；替换已有数字前先按 Del 清空。“Colors → Item”选择色项，
“Colors → Column”切换彩色／黑白列，“Options → Entry”开关系统菜单入口。
在文件选择框中打开主题时，系统按钮显示“保存”，选定后读取主题。

首次运行会登记 `.theme.tns` 关联，之后可从文档浏览器直接打开主题。
需要开机恢复主题时，将程序复制到 Ndless 的 `startup` 文件夹；启动时只加载上次主题，不弹出编辑器。
“Options → Reset”恢复本机首次备份并保留已保存的主题；重启计算器会恢复系统内置配色。
“Options → Uninstall”恢复原色、移除菜单入口、关联和配置，保留主题文件；启动副本需自行删除。
也可将程序命名为 `uninstall.tns` 后运行卸载。

首次运行生成 `themeeditor.original` 原色备份，设置保存在 `themeeditor.cfg`。
请勿删除或从其他系统复制原色备份。保存失败时不要强制退出；可换路径另存为或恢复原色。
若出现 `.themeeditor.backup`，请先保留并取回其中原文件，再删除该恢复副本后重试。
主题只改内存中的系统配色，不修改固件；建议先在独立测试环境中试配色。

## 构建

在 Ndless SDK 环境进入本目录：

```sh
make
make host-test
```

输出为 `themeeditor.tns`。

## 来源与许可证

原版为 Levak 的 [ThemeEditor v2.0](https://tiplanet.org/forum/archives_voir.php?id=6070)（2013），
操作与主题格式参考其[原版说明](https://tiplanet.org/forum/archives/zipview.php?id=6070&zipFileID=2)；
色项含义可参考 Persalteas / Levak 的[配色说明](https://tiplanet.org/forum/archives_voir.php?id=6735)。
本项目以原版公开功能和说明为依据独立重制，属于 nKits，使用仓库根目录的
[GNU General Public License v3.0](../LICENSE)；位图字形沿用 nClock 的 GPL 授权实现。
