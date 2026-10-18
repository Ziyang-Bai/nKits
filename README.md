# nKits

[![Build and Release](https://github.com/Ziyang-Bai/nKits/actions/workflows/build.yml/badge.svg)](https://github.com/Ziyang-Bai/nKits/actions/workflows/build.yml)

TI-Nspire CX / CX II 的实用工具集。

## 工具

### [nHex](nHex)

十六进制编辑器，支持 Hex/ASCII 同步编辑、分页缓存、文件创建与扩展、Go To、重新载入和备份恢复。

### [nEdit](nEdit)

纯文本编辑器，支持任意扩展名、Gap Buffer、文件创建、Go To Line、Caps Lock、Sticky Keys 和备份恢复。

### [nClock](nClock)

驻留时钟与倒计时工具，支持全屏时钟、日期、12/24 小时制、可移动小钟、到期提醒和启动加载。

### [nCapture](nCapture)

驻留截图工具，支持快捷键截图、预览、系统另存为、重复保存和覆盖确认。输出 BMP，传到电脑后去掉文件名末尾的 `.tns` 即可打开。

nClock 和 nCapture 可同时驻留。适配 CX / CX CAS 的 OS 4.5.5.79 及 CX II / CX II CAS 的 OS 6.4.0.74，需要 Ndless r2022。CX / CX CAS 已在 Firebird 中验证；CX II 系列待完整验证。

## 构建

需要 Ndless SDK 和 Python 3。

```sh
make test
make
```

指定版本信息：

```sh
make VERSION=v0.0.2 COMMIT="$(git rev-parse --short=12 HEAD)" STAGE="Alpha Testing"
```

默认值为 `VERSION=dev`、`COMMIT=unknown`、`STAGE=Alpha Testing`。

构建产物：

- `nHex/nhex.tns`
- `nEdit/nedit.tns`
- `nClock/nclock.tns`
- `nCapture/ncapture.tns`

## CI/CD

GitHub Actions 使用 [`Ziyang-Bai/setup-ndless@v1`](https://github.com/Ziyang-Bai/setup-ndless) 配置工具链。

- Push / Pull Request：运行测试并交叉编译四个项目。
- 构建信息：写入版本标签（无标签时为 `dev`）、12 位 commit hash 和 `Alpha Testing`。
- Artifacts：四个 `.tns` 文件及 `LICENSE`。
- `v*` 标签：创建 `nKits <版本> - Alpha Testing` 预发布，附加四个程序和许可证。

## 许可证

Copyright © 2026 Ziyang-Bai.

采用 [GNU General Public License v3.0](LICENSE) 授权。
nClock 保留 Levak、Xavier Andreani / Critor 的版权与 GPL v3+ 声明；
nCapture 保留 Levak 的版权与 LGPL v2+ 声明。
