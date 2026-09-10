# 荔枝派 4A → TL060FVXS07

本目录仅提供 LicheePi 4A（TH1520）的 **Linux DRM/KMS 显示和背光适配**。
屏幕竖屏分辨率为 1080 × 2160；不包含 Android 专属构建、启动、显示 HAL 或编码器修改。

## 转接板与触控限制

**当前转接板不兼容荔枝派 4A 的完整显示＋触控接口，不能当成即插即用转接板。**
显示实测点亮不代表供电、触控与所有引脚已经兼容。使用前仍需按荔枝派扩展板、
转接板及屏幕的原理图核对接线、供电和限流；不要只按连接器外形或泰山派配置接线。

尤其是触控：荔枝派扩展板 TP 供电为 2.8 V，I2C3 的板端外部上拉为 1.8 V；
泰山派参考接口的 TP 供电和 I²C 上拉为 3.3 V。屏幕支持某种供电电压，
不代表触控 I/O 电平也已匹配。SoC 内部上拉不能替代对此处外部上拉/电平连接的核对。
**触控等转接板硬改、确认电气兼容后再适配，本次不加入触控驱动或触控设备树节点。**

## 支持范围

- DSI0，4 lanes，RGB888，burst video；DPU output 0 → DSI。
- 保留 output 1 的 HDMI，不替换已有的下游 DT graph。
- 像素时钟 148.5 MHz，lane rate 950 Mbit/s，实际刷新率约 **56.2 Hz**，不是 60 Hz。
- 时序沿用泰山派目录的 porches：水平 115/3/13，垂直 10/2/10；
  TH1520 使用已验证的 148.5 MHz 像素时钟，不照搬参考 DTS 的 157 MHz。
- 面板命令为 `0x11`（退出休眠，等待 120 ms）、`0x29`（开显示）；
  停止显示时发送 `0x28`、`0x10`。依赖面板上电复位，未假定泰山派 reset GPIO 可直接复用。
- PWM0 背光固定 **40 kHz / 20%**，卸载背光模块关闭并恢复配置；不写 PWM1 风扇寄存器。
  20% 是此接线下的实测亮度设置，不是电气安全保证，也不能替代硬件峰值电流限制。

这是固定板型的 bring-up 实现，尚不是通用 DT panel/DSI host/PWM 驱动。
MMIO 地址、时钟和 GPIO 对应 TH1520/LPi4A，默认不开启；不能用于其他板型。
背光没有实现通用 `pwm-backlight` 亮度调节接口，保持固定 20%。

## 内核前提

适用于 Linux 7.1 开发期的 `drivers/gpu/drm/verisilicon` 驱动接口。
对齐基线为 [`648fd74a4d2eaed87fe242e26506f724619dacff`](https://android.googlesource.com/kernel/common/+/648fd74a4d2eaed87fe242e26506f724619dacff)，
使用其中 Linux 的 DRM bridge API、TH1520 AP/VO clock 与 GPIO provider。
补丁本身只有 Linux Kconfig/Kbuild/C 代码，不依赖 AOSP 构建工具。

**不适用于直接套用到 vendor/RevyOS 5.10 的另一套 DRM/DSI 驱动。**
若内核已经有 DSI0 host、面板 graph 或 PWM0 驱动，请用该内核的原生框架接入，
不要让两套驱动同时操作相同寄存器。当前 DPU output 0 必须没有已有的下游 graph；
TH1520 AP/VO clock、GPIO 与 Verisilicon DC 的基础支持需要事先具备。

## 集成与使用

在符合上述前提的 Linux 内核源码树中，按顺序检查并应用本目录的两个补丁：

```sh
git apply --check /path/to/荔枝派/linux/0001-lpi4a-tl060-dsi.patch
git apply /path/to/荔枝派/linux/0001-lpi4a-tl060-dsi.patch
git apply --check /path/to/荔枝派/linux/0002-lpi4a-tl060-backlight.patch
git apply /path/to/荔枝派/linux/0002-lpi4a-tl060-backlight.patch
```

在原有可启动的荔枝派内核配置上启用：

```text
CONFIG_DRM_VERISILICON_DC=m
CONFIG_PWM=y
# CONFIG_PWM_TH1520 is not set
CONFIG_PWM_TH1520_LPI4A_TL060=m
```

按所用 Linux 发行版的正常流程构建/安装内核及匹配模块。确认接线和转接板后，
在图形会话启动前加载（不要在使用中的 DRM 驱动上强制卸载/重载）：

```sh
modprobe verisilicon-dc lpi4a_dsi0=1
modprobe lpi4a_tl060_backlight enable=1
```

若 DC 已在开机加载，需要预先通过该发行版的 modprobe 配置传入
`options verisilicon_dc lpi4a_dsi0=1`，而不是在已加载后再传参数。
使用 `modetest -c` / `modetest -p` 查询本机 connector/CRTC/plane ID，再选择
DSI 的 1080x2160 模式；不要硬编码其他机器的 DRM 对象 ID。

## 验证与后续

原显示/背光实现已在荔枝派 4A 实机通过真实 DRM framebuffer 输出与物理屏幕显示，
并以固定 20% 背光运行。本次从该实现精简诊断读取/日志和可变亮度测试入口，
保留显示时序、寄存器配置、资源冲突检查与背光输出检查。
精简后的补丁做独立应用和模块编译检查，**不把原实现的板测等同于本次精简版
在所有发行版上的重新验证**。休眠/恢复、电源管理和通用 DT 接入仍需进一步完善。

参考：本仓库 [泰山派](../泰山派/) 的屏幕时序与命令，以及
[Sipeed 荔枝派 4A 外设说明](https://wiki.sipeed.com/hardware/zh/lichee/th1520/lpi4a/6_peripheral.html)。
DSI/D-PHY 初始化保留原实现中 RevyOS/VeriSilicon 驱动来源与 GPL 标识。
