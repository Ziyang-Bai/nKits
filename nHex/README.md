# nHex

TI-Nspire CX/CX II + Ndless 的轻量级、byte-oriented 十六进制编辑器。

## 第一阶段架构

```text
Ndless keys -> input -> app -> editor -> file_buffer -> source file
                                  |
                                  +-> fixed page cache -> journal
file_buffer -> editor state -> ui (NspireIO/lcd_blit) -> 320x240 display
```

- `src/file_buffer.c`：4 KiB、6 槽 LRU 页面缓存；脏页逐出到旁路 journal，不写原文件；流式安全保存。
- `src/editor.c`：唯一的 byte 编辑模型、光标、nibble 状态、导航和 Go To 解析。Hex 和 ASCII 都调用 `editor_write_byte()`。
- `src/input.c`：Ndless `isKeyPressed()` 映射，不接触文件 I/O。
- `src/ui.c`：使用 NspireIO 的离屏缓冲和 Ndless `lcd_blit()` 直接绘制 320x240 界面，不依赖 OS GC 内部布局，也不修改数据。
- `src/app.c`：文件生命周期、对话框、未保存确认和事件循环。
- `src/main.c`：启动入口；优先打开 `argv[1]`，否则显示路径输入框。

核心状态只有一份字节来源：

```text
Hex digit -> replace high/low nibble --+
                                         +-> editor_write_byte(offset, value)
ASCII key -> printable byte ------------+             |
                                                       v
                                                file_buffer_write()
```

UI 的 Hex 与 ASCII 栏在每次绘制时都调用 `file_buffer_read()`；不维护显示副本。模式切换只改变 `Editor.mode`，不会改变 `cursor_offset`。

## 缓存与保存不变量

- 不按文件大小分配内存。常驻页数据固定为 `6 * 4096` bytes。
- 脏页逐出时写入旁路 journal；内存只保留每个已修改页的小型索引。
- 编辑期间原文件始终只读。
- 保存时逐页生成同目录临时文件；每页从原文件或 journal 取最新版本。
- 临时文件完整写入、`fflush()`、`fclose()` 成功后，先流式备份目标，再复制安装新文件。
- Ndless 的 `rename()` 可能返回 `ENOSYS`，因此保存不依赖 rename；安装失败时从 `.nhex.backup` 恢复原文件。
- 支持在文件尾部扩展并以 `00` 填充新增区域；不实现中间 Insert/Delete。

文件大小和编辑 offset 使用 `uint64_t`。实际 seek 通过 SDK 的 `off_t`；超出平台 `off_t` 可表示范围时明确报错，不截断为 `int`。

Open 页面和 `Ctrl+N` 可以创建新文件；创建时可用十进制或 `0x` 十六进制指定初始大小。`Ctrl+E` 可把现有文件扩展到更大的绝对大小。

## TI-Nspire 操作

屏幕每行显示 8 bytes，以适应 320px 宽度；同一光标会在活动区高亮、在另一栏显示对应高亮。Hex 模式状态栏显示 `HIGH`/`LOW`。

| 按键 | 操作 |
|---|---|
| 方向键 | 前后 1 byte / 8 bytes |
| Shift + Up/Down | 上/下一屏 |
| Ctrl + Left/Right | 行首/行尾 |
| Tab | HEX / ASCII 切换 |
| Ctrl + G | Go To（十进制、`0x`、`+0x`、`-0x`） |
| Ctrl + N | Create File |
| Ctrl + E | Extend File Size |
| Ctrl + O | Open |
| Ctrl + S | Save |
| Ctrl + Shift + S | Save As |
| Ctrl + R | Reload |
| Menu | 打开 Help / About 页面 |
| Esc | Exit；脏文件显示 Yes / No / Cancel |
| Open 页面 | `Browse`、`Path`、`Create`、`Help`、`About`、`Cancel` |

ASCII 模式直接映射字母、数字、空格和计算器上的常用标点；Shift 产生大写或对应标点。数据仍是原始单 byte，不解释 UTF-8。

## 构建

Ndless SDK 安装在 WSL 并已将 `nspire-gcc`、`nspire-ld`、`genzehn`、`make-prg` 放入 `PATH` 后：

```sh
cd /mnt/c/Users/baizi/Documents/GitHub/nKits/nHex
make
```

生成 `nhex.tns`。主机核心行为验证：

```sh
make host-test
```

## 许可证

nHex 是 nKits 的组成部分，使用仓库根目录中的
[GNU General Public License v3.0](../LICENSE) 授权。
