# nKits

[![Build and Release](https://github.com/Ziyang-Bai/nKits/actions/workflows/build.yml/badge.svg)](https://github.com/Ziyang-Bai/nKits/actions/workflows/build.yml)

TI-Nspire CX / CX II 的 Ndless 原生工具集。

下载：[Nightly 测试版](https://github.com/Ziyang-Bai/nKits/releases/tag/nightly)。

## 工具

| 工具 | 用途 |
| --- | --- |
| [nHex](nHex) | 十六进制编辑器|
| [nEdit](nEdit) | 纯文本编辑器|
| [nTexts](nTexts) | 纯文本阅读器 |
| [nSee](nSee) | PNG / JPEG / BMP 图片查看器 |
| [nClock](nClock) | 时钟 |
| [nCapture](nCapture) | 按 **Ctrl+.** 截图 |
| [nProtect](nProtect) | 保护文件浏览器入口 |
| [nHide](nHide) | 隐藏文档 |
| [ThemeEditor](ThemeEditor) | 编辑主题 |
| [TaskManager](TaskManager) | 任务管理器 |
| [nUSB](#nusb) | FAT32 / exFAT U盘读写 |

## 界面与构建

各工具的前台界面共用 `common/draw.*`：nTexts 的系统字体、配色、整行高亮和底部按键提示。
系统输入框、设置表单及驻留回调继续使用各自的原生调用方式。
nTexts 保留浅色、深色和护眼主题，以及原有阅读存档格式。

构建需要 WSL/Linux、GNU Make、Python 3，以及包含 `ndialog.h` 的 Ndless r2023 SDK。
CI 固定使用 Ndless `e52415fb568e92cd36ed049067b3e9c2727b84af`。配置 SDK 的 `PATH` 后运行：

```sh
make test
make VERSION=dev COMMIT=local
```

`make` 同时生成各工具和诊断程序的 `.tns` 文件；发布工作流将 nTexts 一起打包。


## 许可证

Copyright (c) 2026 Ziyang-Bai. [GNU GPL v3.0](LICENSE)。

- **nClock**：Copyright (c) 2014 Levak. Contributions by Xavier Andreani (2016). GPL v3+。
- **nSee**：Original mViewer_CX, Copyright (c) 2013-2019 Xavier Andréani, JayTe, Excale。移植版 GPL v3；图像库保留 libpng / IJG 许可证，详见 [nSee/README.md](nSee/README.md)。
- **nCapture**：Copyright (c) 2013 Levak. LGPL v2+。
- **FatFs**：Copyright (c) 2025 ChaN. [BSD-style 1-clause](nUSB/third_party/fatfs/LICENSE.txt)。
- **NspireIO**：Julian Mackeben / compu、Manfred Becker、totorigolo。[LGPL v2.1+](https://github.com/Vogtinator/nspire-io)。
