# nCapture

TI-Nspire CX/CX II + Ndless 驻留截图工具。支持快捷键截图、预览、系统另存为和重复保存。

适配 CX / CX CAS 的 OS 4.5.5.79 和 CX II / CX II CAS 的 OS 6.4.0.74，需要 Ndless r2022。CX / CX CAS 已在 Firebird 中验证；CX II 系列待完整验证。截图用于系统页面。

## 安装与启动

运行 `ncapture.tns`，关闭安装提示和状态窗口后，按 `Ctrl+.` 截图。可与 nClock 同时使用。

将程序放入 `/documents/ndless/startup/`，开机重新启用 Ndless 后自动加载。更新程序、系统或 Ndless 前先重启，启动目录保留一个版本。

再次运行程序打开状态窗口，可查看保存次数、最后路径和错误提示。`Enable` / `Disable` 开关截图快捷键，`Output` 设置默认文件夹和文件名。

## 截图与保存

`Ctrl+.` 截取完整 320×240 系统屏幕并打开预览。`Enter` 或 `Save as` 打开系统另存为对话框，选择已有目录和文件名后保存。

保存或取消另存后都回到当前截图的预览，可再次按 `Enter` 保存到其他名称或文件夹。按 `Esc` 或选择 `Close` 关闭预览，回到系统；下次快捷键截取新画面。

已有同名文件时显示 `Cancel` / `Replace`，默认取消。用 `Tab` 选择 `Replace` 后按 `Enter` 覆盖；按 `Esc` 或直接确认默认选项会保留原文件。

预览打开期间再次按快捷键会保留当前截图。保存成功后增加保存次数，取消另存保留当前截图和次数。

## 快捷键

| 按键 | 操作 |
|---|---|
| Ctrl + . | 截取系统屏幕并打开预览 |
| Enter | 打开另存为；保存后可再次另存 |
| Esc | 取消另存；预览中关闭并返回系统 |
| Tab | 切换系统对话框中的选项 |

## 输出设置

默认路径为 `/documents/capture.bmp.tns`。保存为 24 位 BMP，传到电脑后去掉末尾的 `.tns` 即可打开。

`Output` 的 `Folder` 为 `/documents` 下的已有目录，默认 `/`；`Name` 包含 `.bmp`，例如 `capture.bmp` 或 `My capture.bmp`。也可在另存为对话框中修改目录和名称。

另存为会记住最近的目录和文件名。输出设置、快捷键开关和保存次数保留到本次重启。

## 构建

```sh
cd /mnt/c/Users/baizi/Documents/GitHub/nKits/nCapture
make
make host-test
```

## 许可证

nCapture 是 nKits 的组成部分，保留 Levak（2013）的版权与署名，
沿用 GNU LGPL v2 或更新版本。
