# nEdit

TI-Nspire CX/CX II + Ndless 原生纯文本编辑器。界面、文件选择、安全保存、帮助和快捷键逻辑与 nHex 保持一致。

## 架构

```text
Ndless keys -> input -> app -> editor -> text_buffer -> file
                         |         |
                         |         +-> cursor/viewport
                         +-> ui -> NspireIO/lcd_blit
```

- `text_buffer`：可增长 gap buffer，支持插入、退格、删除和流式安全保存。
- `editor`：光标、行列导航、翻页、Go To Line、水平与垂直视口。
- `input`：TI-Nspire 键盘和 Ctrl/Shift 组合键映射。
- `ui`：与 nHex 一致的直接 LCD 风格、Open 页面、文件浏览器、保存确认、Help/About。
- `app`：文件生命周期、事件循环和未保存修改处理。

保存不依赖 Ndless 上可能返回 `ENOSYS` 的 `rename()`：先写完整临时文件，再备份目标并复制安装；失败时恢复 `.nedit.backup`。

Open 页面的 Browse 会显示目录中的所有普通文件，不限制 `.txt` 后缀；新建文件路径也允许任意后缀。

Open 页面提供 `Browse`、`Path`、`Create`、`Help`、`About` 和 `Cancel`；`Ctrl+N` 可直接创建文件。

第一版按原始 byte 保存文本，直接编辑可打印 ASCII，保留文件中已有的其他 byte；不做 UTF-8 字符级重排。

## 快捷键

| 按键 | 操作 |
|---|---|
| Arrows | 移动光标 |
| Shift + Up/Down | 上/下一页 |
| Ctrl + Left/Right | 行首/行尾 |
| Ctrl + Shift | 切换 Caps Lock |
| 连按 Ctrl 5 次 | 开启/关闭 Sticky Keys |
| Ctrl + N | Create File |
| Ctrl + O | Open |
| Ctrl + S | Save |
| Ctrl + Shift + S | Save As |
| Ctrl + R | Reload |
| Ctrl + G | Go To Line |
| Enter | 插入换行 |
| Del | 退格删除 |
| Tab | 插入 4 个空格 |
| Menu | Help / About |
| Esc | Exit / Cancel |

Sticky Keys 默认关闭。开启后可先按下并释放 `Ctrl` 或 `Shift`，再按功能键；
关闭时组合键必须在修饰键保持按下期间触发。

## 构建

```sh
cd /mnt/c/Users/baizi/Documents/GitHub/nEdit
make
make host-test
```
