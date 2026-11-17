# nTexts

[![Build](https://github.com/Ziyang-Bai/nKits/actions/workflows/build.yml/badge.svg)](https://github.com/Ziyang-Bai/nKits/actions/workflows/build.yml)

TI-Nspire CX / CX II 的轻量纯文本阅读器，基于 Ndless SDK 构建，现作为 [nKits](../README.md) 的一部分维护。

## 使用

从 [nKits Release](https://github.com/Ziyang-Bai/nKits/releases) 下载 `nTexts.tns`，传入已安装 Ndless 的计算器后运行。
文本文件命名为 `something.txt.tns`，在书库中打开。

## 特性

- 支持 UTF-8、GB18030 和 UTF-16，自动识别或手动选择编码。
- 书库文件名筛选、最近阅读、自动保存进度和书签。
- 正文搜索、高亮，以及按页码、行号、百分比或章节跳转。
- 支持中文、英文、法语界面，多种字号、主题和边距。

## 按键

| 按键 | 功能 |
| --- | --- |
| Enter | 打开选中项、继续阅读；搜索时查找下一处 |
| 左 / 右 | 翻页 |
| 上 / 下 | 按文本行移动；搜索时查找上一处 / 下一处 |
| Ctrl+上 / 下 | 跳到首尾 |
| Ctrl+左 / 右 | 切换章节 |
| F | 书库中筛选文件名，阅读中搜索 |
| 0 | 书库中清除筛选 |
| B | 添加书签 |
| G | 打开跳转 |
| Menu | 阅读菜单；按 0 选择本书编码 |
| (-) | 主页打开设置 |
| H | 打开快捷键提示 |
| Esc | 关闭提示、结束搜索、返回或退出 |

## 构建

需要 WSL/Linux、Ndless SDK 和 GNU Make。在 nKits 仓库根目录执行：

```sh
cd nTexts
sh tools/build-wsl.sh
make host-test
```

生成文件仍为 `nTexts.tns`，中间文件和主机测试输出位于 `.build/`。
SDK 默认位于 `$HOME/Ndless/ndless-sdk`，可用 `NDLESS_SDK` 指定路径。
SDK 已在 `PATH` 中时，也可在仓库根目录执行 `make -C nTexts`；
`make -C nTexts test` 与 `make -C nTexts host-test` 运行相同的主机行为测试。
构建元数据与其他工具一致：`VERSION`、`COMMIT`、`STAGE`，通过 `../version.h` 显示。
清理构建文件：在 `nTexts/` 中执行 `sh tools/build-wsl.sh clean`。

代码按职责拆分为 `reader.c`（阅读会话、搜索、书签和分页显示）、
`library.c`（最近阅读和文件浏览）、`settings.c`（设置及章节规则）、
`interface.c`（菜单、提示、原生输入和错误页），`app.c` 负责启动和主页控制。
公共绘图和主题使用 `../common/draw.c`；文本解码、索引及存储格式保持不变。

## 许可证

[GNU GPL v3.0](LICENSE)。保留自 [原 nTexts 仓库](https://github.com/Ziyang-Bai/nTexts) 的许可证。
