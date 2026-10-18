# nKits

[![Build and Release](https://github.com/Ziyang-Bai/nKits/actions/workflows/build.yml/badge.svg)](https://github.com/Ziyang-Bai/nKits/actions/workflows/build.yml)

TI-Nspire CX / CX II 的实用工具集。

## 工具

### [nHex](nHex)

十六进制编辑器，支持 Hex/ASCII 同步编辑、分页缓存、文件创建与扩展、Go To、重新载入和备份恢复。

### [nEdit](nEdit)

纯文本编辑器，支持任意扩展名、Gap Buffer、文件创建、Go To Line、Caps Lock、Sticky Keys 和备份恢复。

## 构建

需要 Ndless SDK。

```sh
make test
make
```

指定版本信息：

```sh
make VERSION=v0.0.1 COMMIT="$(git rev-parse --short=12 HEAD)" STAGE="Alpha Testing"
```

默认值为 `VERSION=dev`、`COMMIT=unknown`、`STAGE=Alpha Testing`。

构建产物：

- `nHex/nhex.tns`
- `nEdit/nedit.tns`

## CI/CD

GitHub Actions 使用 [`Ziyang-Bai/setup-ndless@v1`](https://github.com/Ziyang-Bai/setup-ndless) 配置工具链。

- Push / Pull Request：运行测试并交叉编译两个项目。
- 构建信息：写入版本标签（无标签时为 `dev`）、12 位 commit hash 和 `Alpha Testing`。
- Artifacts：`.tns` 文件。
- `v*` 标签：创建 GitHub Release。
## 许可证

Copyright © 2026 Ziyang-Bai.

采用 [GNU General Public License v3.0](LICENSE) 授权。
