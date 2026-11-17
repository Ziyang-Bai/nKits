# nSee

TI-Nspire CX / CX II CAS 的 Ndless 图片查看器，界面沿用 nKits 的系统字体、配色、整行高亮和底部提示。许可证按 nKits 使用 [GPL v3](LICENSE)。

## 安装与操作

将 `nsee.tns` 传入文档目录后运行。计算器须已安装与其 OS 匹配的 Ndless；构建使用 r2023 SDK。支持 JPEG、PNG 和 BMP/DIB，包括追加 `.tns` 后缀的文件，如 `photo.png.tns`。图片内容保持原格式即可。

主页提供浏览图片、上次图片、帮助、文件关联和关于；语言跟随系统，支持中文、英文和法文。文件关联由菜单显式注册，并保留已分配给其他程序的扩展名。上次打开路径保存在文档根目录的 `nsee.cfg.tns`。

| 按键 | 功能 |
| --- | --- |
| 浏览页方向键 / 8、2 | 选择文件或文件夹 |
| Enter / 点击 | 打开；看图时切回 1:1 |
| 浏览页 Esc / 左 | 上级目录；文档根目录返回主页 |
| 浏览页 R | 刷新目录 |
| 看图时方向键 / 数字 1–9 | 平移；按住 5 / 点击键时整页移动 |
| 滑动触摸板 | 拖动图片 |
| +、− / ×、÷ | 以画面中心缩放 |
| 0 | 适合当前窗口 |
| F | 全屏切换 |
| (、) | 本目录上一张、下一张 |
| Menu | 操作菜单、帮助、关于 |
| 看图时 Esc | 返回文件浏览器 |

图片解码为 RGB565；图像颜色保持原样，nKits 配色用于页面。内存需求包含每像素 2 字节的解码图像及解码器工作区。大图片可用大小取决于计算器可分配的内存。

## 构建

依赖 GNU Make、Ndless r2023 SDK（含 `ndialog.h`、zlib）及 ARM 工具链。JPEG / PNG 解码源码随项目提供，产物为 `nSee/nsee.tns`。

```sh
make -C nSee VERSION=dev COMMIT=local
```

主机解码测试需要 Python 3（标准库）、C99 编译器及 zlib 开发文件：

```sh
make -C nSee host-test
```

根目录 `make` / `make test` 包含 nSee；CI 的构建产物和发布同时包含程序及许可证说明压缩包。

## 验证

- 已使用与 CI 相同修订的 Ndless r2023 SDK 构建；66 组解码用例通过，覆盖 BMP 位深、调色板、行方向、RLE、PNG Adam7、JPEG 渐进解码及截断输入。
- Firebird CX II CAS 中已实际验证主页、About、空目录、PNG/JPEG/BMP 显示、缩放、平移、全屏、前后切图、上次图片持久化、文件关联启动、损坏图片报错及退出后 OS 显示恢复。
- 文件关联验证保留了原有扩展名设置，并将未分配的图片扩展名注册到 nSee。
- CX 的兼容适配使用同一份 Ndless LCD 接口；CX 运行验证待取得对应系统镜像。实体 CX / CX II CAS 验收仍需在计算器上完成。

## 来源与版权

Original mViewer_CX 4.5.1

Copyright (c) 2013-2019 Xavier Andréani, JayTe, Excale

Copyright (c) 2026 Ziyang-Bai（nKits 移植）

来源为提供的 `mViewerCX4.5.1.zip`，SHA-256：`f46f39eda108dc376449543e14aed5e125b2ebb701128e5fa1aa4cd41a74613f`。

移植保留 mViewer 的 BMP 解码结构、PNG/JPEG 扫描线转换、源像素/屏幕像素缩放模型、中心缩放和平移、同目录切图行为；页面采用 `common/draw.*`、`common/ui.*`。硬件适配通过 Ndless 的 `lcd_init` / `lcd_blit` 处理 CX 与 CX II 的 LCD 布局；程序退出时恢复 OS 显示。旧版计算器表达式启动入口 `mv()` / `mvl()` 依赖固定 OS 地址，本版本使用文档启动和 Ndless 文件关联。

修改日期：2026-11-17。解码适配包括图像尺寸、BMP 行填充/方向和 RLE 数据边界处理，以及 PNG/JPEG 错误路径的资源释放。

第三方代码保留其原有许可证：

- libpng 1.4.5：见 [`third_party/libpng/png.h`](third_party/libpng/png.h) 内完整许可。
- IJG JPEG 6b：见 [`third_party/libjpeg/README`](third_party/libjpeg/README)。This software is based in part on the work of the Independent JPEG Group.
- zlib：来自 Ndless SDK，采用 zlib 许可证。

库源码的适配：`libjpeg/jinclude.h` 改用标准 C 头文件；libpng 改用 SDK/系统 zlib 和标准 C 头文件，启用 `setjmp` 错误恢复，按现有源码启用只读构建，并在展开调色板时检查索引范围。

JPEG/PNG 源码来自原压缩包；JPEG 6b 的原始 README 从 [libjpeg-turbo 保留的原文](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/1.0.0/README) 补齐。发布的 `nsee-notices.zip` 包含 GPL v3、本文和上述图像库许可证。对应源码与构建脚本位于同一发布版本的仓库中。
