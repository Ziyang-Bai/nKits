# nKits

[![Build and Release](https://github.com/Ziyang-Bai/nKits/actions/workflows/build.yml/badge.svg)](https://github.com/Ziyang-Bai/nKits/actions/workflows/build.yml)

TI-Nspire CX / CX II 的 Ndless 原生文件编辑工具集。

## 工具

### [nHex](nHex)

320×240 原生十六进制编辑器。支持分页缓存、Hex/ASCII 同步编辑、创建与扩展文件、Go To、重新载入以及带备份恢复的安全保存。

### [nEdit](nEdit)

320×240 原生纯文本编辑器。支持任意扩展名、Gap Buffer 编辑、创建文件、Go To Line、Caps Lock、Sticky Keys 以及带备份恢复的安全保存。

## 构建

需要 Ndless SDK。进入 WSL/Linux 环境后在仓库根目录执行：

```sh
make test
make
```

生成：

- `nHex/nhex.tns`
- `nEdit/nedit.tns`

也可以单独进入 `nHex` 或 `nEdit` 目录构建。每个子项目的详细功能和快捷键见对应 README。

## CI/CD

GitHub Actions 使用 [`Ziyang-Bai/setup-ndless@v1`](https://github.com/Ziyang-Bai/setup-ndless) 配置 Ndless 工具链：

- 每次 Push 和 Pull Request 运行两个项目的主机测试并交叉编译；
- 上传两个 `.tns` 文件作为工作流构建产物；
- 推送 `v*` 标签时自动创建 GitHub Release，并附加 `nhex.tns` 与 `nedit.tns`。

## 版权

Copyright © 2026 Ziyang-Bai. All rights reserved.
