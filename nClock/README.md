# nClock

TI-Nspire CX/CX II + Ndless 驻留时钟与倒计时工具。支持全屏时钟、日期、12/24 小时制、可移动小钟和到期提醒。

适配 CX / CX CAS 的 OS 4.5.5.79 和 CX II / CX II CAS 的 OS 6.4.0.74，需要 Ndless r2022 及更新版本。

CX / CX II 实机仍有日期确认选择 Keep 后卡死的报告。已有栈对齐和 IRQ 保护改动经过仿真验证；实机故障原因待下述诊断程序采样定位。

## 安装与启动

运行 `nclock.tns`，首次使用时选择时间格式、秒显示和启动日期检查。选择 `Change` 可修改日期和时间。

主界面提供 `Options`、`Countdown` 和 `Quit`。选择 `Quit` 或按 `Esc` 返回系统，小钟和倒计时继续运行。再次运行程序可打开全屏时钟。

`Options` 的 `Load nClock on startup` 可将程序安装到启动目录。也可手动放入 `/documents/ndless/startup/`；开机重新启用 Ndless 后加载。更新程序前先重启，再重新启用 Ndless 并运行新版。

### 从启动崩溃中恢复

1. 重启后，在 Ndless 启用、准备加载启动程序时按住 `Esc`。Ndless 会跳过整个 `startup` 目录。
2. 进入文档浏览器，将 `ndless/startup/nclock.tns` 移出启动目录；清理其他位置的旧启动副本。
3. 替换新版 nClock。共同驻留的 nCapture、nProtect、nHide 和 ThemeEditor 也须一起更新，然后重启。
4. 重新启用 Ndless，先手动运行 nClock，确认工作正常后再启用启动加载。

驻留 ABI 11 为系统显示、计时、事件及活动回调增加 ARM EABI 栈对齐入口，返回时恢复系统原栈。旧驻留实例通过重启释放；新版会拒绝复用旧 ABI 的钩子。

## 时钟设置

`Options` 提供时间、日期、12/24 小时制、秒显示、小钟位置和显示开关。修改日期保留时间，修改时间保留日期；隐藏小钟后倒计时和提醒继续运行。

`Move mini-clock` 用方向键调整位置，`Shift` 加方向键每次移动十像素；`Enter` 保存，`Esc` 恢复原位置。

`Check time after reboot` 设置启动时是否检查日期和时间。`Idle off timeout` 设置全屏界面的空闲熄屏时间，默认 180 秒；到时直接熄屏，第一个按键立即恢复亮度并用于唤醒。

界面进入、切换和退出直接完成。`Brightness` 调整全屏亮度，退出全屏界面后立即恢复系统亮度。`Moar !` 提供屏幕翻转、到期重启、启动安装和 About。

全屏界面期间保存并屏蔽 CPU 中断，使 SDK 延时和按键释放等待自行确认定时器中断。退出界面时恢复原中断状态；系统对话框沿用 SDK 临时开放中断的流程。

## 倒计时

`Countdown` 提供自定义时长、目标日期、快捷倒计时和停止操作：

- `...delay`：输入天、小时、分钟和秒。
- `...from date`：选择未来的日期和时间。
- `Quick countdowns`：3、5、15、30 分钟及 1、2、3 小时。
- `Stop current countdown`：停止当前倒计时。
- `Reboot on countdown`：开启或关闭到期重启，默认关闭。

剩余五分钟时提醒一次，最后五分钟冒号闪烁，最后一分钟数字闪烁，到期显示 `Time's up`。开启到期重启后，关闭到期提醒会重启计算器。

退出全屏和调整日期时间时，倒计时继续运行。跨日倒计时会显示剩余天数。

## 快捷键

| 按键 | 操作 |
|---|---|
| Arrows / 触摸板边缘 | 选择菜单或移动小钟 |
| Enter / Return / 触摸板点击 | 确认 |
| Shift + Arrows | 移动小钟时每次移动十像素 |
| ON | 全屏界面熄屏；其他按键唤醒 |
| Esc | 返回；主界面退出到系统；移动小钟时取消 |

数字输入框沿用系统操作：用 `Del` 删除已有数字，再输入新值。取消保留原设置。

## 设置与卸载

设置保存在 `/documents/nclock.cfg`，下次运行自动恢复。启动时可通过日期检查校正断电后的时间。已有 48 / 80 字节配置继续可读，历史渐变开关按保留位处理；时间、倒计时和其他选项保持原有格式。

卸载时将程序改名为 `uninstall.tns`，手动运行并确认。时钟和倒计时停止，配置文件删除；可选择重启完成卸载。同时移走启动目录中的程序，防止下次自动加载。

## Keep 卡死诊断

运行独立程序 [`tools/nclock-diag.tns`](tools/nclock-diag.tns)，采集实际 nClock 启动路径中的阶段和硬件状态：

1. 将旧 nClock 移出 `ndless/startup`，重启并重新启用 Ndless。此次重启后先运行诊断程序，释放旧驻留实例对采样的影响。
2. 从文档浏览器手动打开 `nclock-diag.tns`，选择 `Run`。诊断程序沿用 `/documents/nclock.cfg` 的设置；首次设置问题正常选择，出现日期确认时选择 **Keep**。
3. 程序在完成首帧绘制和一次 30 ms 等待后自动结束。结果框显示报告路径。提前按 `Esc` 会留下未完成的报告。
4. 如果卡死，拍下可见的 `Dxxxx` 阶段和文字，再按 reset。重新连接电脑，传回文档根目录的 `nclock-diag-001.txt.tns` 等报告；编号随每次运行递增，已有报告保留。它是纯文本，电脑上可去掉 `.tns` 后缀阅读。
5. 完成启动采样后，诊断程序保留停用的驻留实例。再次诊断或运行正常 nClock 前先重启。

报告包括机型、OS 标记、Ndless revision、构建标识、CPSR/SP、VIC、Timer2 A/B 和 LCD 状态，以及日期对话框、每次亮度读取/写入、SDK 延时、按键释放和画面提交的前后检查点。屏幕顶端的小型阶段条显示当前阶段；屏幕格式或地址无法验证时保留文件采样。

诊断读取原配置，实际保存操作写入自己的 `/documents/nclock-diag.cfg.tns`；启动目录保持原样，主动重启请求记录为 `reboot.request` 并停止诊断。程序仅接受手动运行。

每个文件检查点完成写入和关闭后，才继续待测操作。reset 可能丢失末尾记录，因此应结合屏幕阶段判断；屏幕 D 编号领先于文件最后记录时，该检查点可能停在记录写入过程中。日志和缓存刷新会改变时序，即使本次顺利完成，也请提供完整报告。

缓存对照包用 `make -C tools MARKER_CACHE=FALSE` 构建为 `tools/nclock-diag-nocache.tns`。它保留相同的文件采样和首帧结束条件，只取消阶段条额外的 `clear_cache()`，报告注明 `marker_cache_flush=0`；驻留钩子安装继续使用原有缓存同步。普通诊断报告注明 `marker_cache_flush=1`。两种包之间先重启，再以相同设置手动运行，分别取回报告作对照。

## 构建

```sh
cd /mnt/c/Users/baizi/Documents/GitHub/nKits/nClock
make
make host-test
make device-test
make diagnostic
```

`host-test` 验证时钟模型。`device-test` 构建 `build/test_ui_irq.tns`，需传入支持的计算器或仿真器运行：分别从 IRQ 开启和已屏蔽的状态进入全屏界面，按 `Esc` 退出，再按 `Enter` 关闭结果框并继续。两项都应显示 `PASS: UI wait and IRQ restore`，确认界面可正常退出、原中断状态得到恢复。

## 许可证

nClock 是 nKits 的组成部分，使用仓库根目录中的
[GNU GPL v3 或更新版本](../LICENSE) 授权。
保留 Levak（2014）和 Xavier Andreani / Critor（2016）的版权与署名。
