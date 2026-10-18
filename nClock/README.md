# nClock

TI-Nspire CX/CX II + Ndless 驻留时钟与倒计时工具。支持全屏时钟、日期、12/24 小时制、可移动小钟和到期提醒。

适配 CX / CX CAS 的 OS 4.5.5.79 和 CX II / CX II CAS 的 OS 6.4.0.74，需要 Ndless r2022。CX / CX CAS 已在 Firebird 中验证；CX II 系列待完整验证，亮度和睡眠效果待实机确认。

## 安装与启动

运行 `nclock.tns`，首次使用时选择时间格式、秒显示和启动日期检查。选择 `Change` 可修改日期和时间。

主界面提供 `Options`、`Countdown` 和 `Quit`。选择 `Quit` 或按 `Esc` 返回系统，小钟和倒计时继续运行。再次运行程序可打开全屏时钟。

`Options` 的 `Load nClock on startup` 可将程序安装到启动目录。也可手动放入 `/documents/ndless/startup/`；开机重新启用 Ndless 后加载。更新程序前先重启，再重新启用 Ndless 并运行新版。

## 时钟设置

`Options` 提供时间、日期、12/24 小时制、秒显示、小钟位置和显示开关。修改日期保留时间，修改时间保留日期；隐藏小钟后倒计时和提醒继续运行。

`Move mini-clock` 用方向键调整位置，`Shift` 加方向键每次移动十像素；`Enter` 保存，`Esc` 恢复原位置。

`Check time after reboot` 设置启动时是否检查日期和时间。`Idle dim / off timeout` 设置全屏界面的空闲熄屏时间，默认 180 秒；熄屏后的第一个按键用于唤醒。

`Fading transitions` 和 `Brightness` 设置渐变与亮度。退出全屏界面后恢复系统亮度。`Moar !` 提供屏幕翻转、到期重启、启动安装和 About。

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

设置保存在 `/documents/nclock.cfg`，下次运行自动恢复。启动时可通过日期检查校正断电后的时间。

卸载时将程序改名为 `uninstall.tns`，手动运行并确认。时钟和倒计时停止，配置文件删除；可选择重启完成卸载。同时移走启动目录中的程序，防止下次自动加载。

## 构建

```sh
cd /mnt/c/Users/baizi/Documents/GitHub/nKits/nClock
make
make host-test
```

## 许可证

nClock 是 nKits 的组成部分，使用仓库根目录中的
[GNU GPL v3 或更新版本](../LICENSE) 授权。
保留 Levak（2014）和 Xavier Andreani / Critor（2016）的版权与署名。
