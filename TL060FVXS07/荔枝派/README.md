# 荔枝派 4A → TL060FVXS07

荔枝派4A（TH1520）的Linux显示、背光和Samsung S6D6FT0触控适配。
直接提供两个**外挂内核模块源码**及设备树，不再通过patch修改内核源码；
不包含AOSP/HAL改动、预编译模块、系统镜像或调试日志。

## 接线

6pin触摸排线使用正向线；如果使用反向线，**荔枝派端铜触点朝PCB外**。
屏幕端按连接器触点方向插入，不要仅凭蓝色加强片判断。插拔排线前断电。
触摸使用扩展板J1901、I2C3地址0x48，中断为AON GPIO3。

## 已验证环境与范围

- 荔枝派4A，8G RAM / 32G eMMC，TL060FVXS07。
- 官方[RevyOS 20260504](https://mirror.iscas.ac.cn/revyos/extra/images/lpi4a/20260504/)、
  官方U-Boot及未修改的官方内核 **6.6.137-th1520**。
  内核源码提交：`cca7502e0443b90c5ab24fd47487f1747e450b38`。
- DSI0四lane、RGB888 burst，1080×2160，148.5MHz像素时钟、950Mbit/s lane rate，
  实际刷新率约 **56.2Hz**，不是60Hz。水平porch/sync为115/3/13，垂直为10/2/10。
- 使用现有Linux regulator / pwm-backlight，PWM0为40kHz，实测背光20%；
  PWM1风扇不由本模块操作。20%占空比不等于峰值电流限制或电气安全保证。
- Type-B多点触控，已验证点击、Chromium双指缩放、桌面旋转后的触控映射。
  声明10个槽位，但没有完成十指全量测试。
- 已验证整镜刷入后的自动亮屏、登录软键盘和触控旋转跟随。
  16G设备树做编译检查，**未在16G实机验证**。

这是固定板型/面板的适配，不是通用DSI host驱动。不能直接用于RevyOS5.10、
其他面板或任意主线内核；DT标签、clock provider ABI和DRM接口需要匹配目标内核。
深度休眠、触摸唤醒、HDMI/DSI多屏组合及长时间稳定性仍需进一步测试。

## 文件与资源所有权

`linux/`包含显示模块`lpi4a_tl060.c`、触控模块`s6d6ft0.c`、对应头文件、
`th1520-lichee-pi-4a-tl060.dts`、Makefile和模块配置。
`desktop/`是可选的RevyOS Xfce/LightDM接入，不是内核补丁。

显示模块接管DSI0及对应PHY，保留官方DC/HDMI驱动；DTS禁用原生DSI0 PHY并更换host
compatible，避免两套实现争用。模块按clock provider选择时钟ID，不能将主线ID
照搬到RevyOS vendor provider。DCS电源状态只检查有效状态位，不要求保留位D7为1。

触控不升级固件、不写校准/NVM，也不独立操作共享面板复位线。控制器需要有有效的原厂固件。
该面板接触时压力值仍为0，因此不声明这个无效压力轴。触控DT使用直接IRQ资源；
不要重复添加`irq-gpios`或创建第二个0x48客户端。

## 构建

必须使用**实际运行内核对应**的配置、准备好的构建目录及`Module.symvers`。
不要为消除编译错误随意关闭配置，尤其不要改变`CONFIG_DEBUG_INFO_BTF_MODULES`
后仍把模块用于原版内核。仅vermagic相同不能证明完整ABI兼容。

在有匹配头文件/构建目录的板上：

```sh
cd linux
make KDIR=/lib/modules/$(uname -r)/build
```

交叉编译示例使用已验证的GCC14，路径替换成自己的目标内核目录：

```sh
make KDIR=/path/to/kernel-src KOUT=/path/to/kernel-out \
  ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- CC=riscv64-linux-gnu-gcc-14 \
  LOCALVERSION=-th1520
make dtbs KERNEL_SRC=/path/to/kernel-src CC=riscv64-linux-gnu-gcc-14
```

输出`lpi4a_tl060.ko`、`s6d6ft0.ko`和分别基于官方8G/16G板级DTS的两个DTB。
DTB构建需要完整目标内核的DTS/include文件。不要给16G板使用8G内存描述。
`LOCALVERSION`示例对应上述官方内核，其他目标应保持自己的完整内核版本，不要重复追加后缀。

## 安装与自动启动

先备份原boot/DTB/initramfs，并保留串口恢复路径。以下命令在目标板上执行：

```sh
sudo install -d /lib/modules/$(uname -r)/extra
sudo install -m 644 lpi4a_tl060.ko s6d6ft0.ko /lib/modules/$(uname -r)/extra/
sudo install -m 644 lpi4a-tl060.conf /etc/modprobe.d/
sudo depmod -a
```

按发行版启动配置选择本板型的新DTB。官方RevyOS通过extlinux的`fdtdir`选择DTB，
通常位于`/boot/dtbs/linux-image-$(uname -r)/thead/`：
8G板对应`th1520-lichee-pi-4a.dtb`，16G板对应`th1520-lichee-pi-4a-16g.dtb`。
备份后用对应生成文件替换，或建立独立extlinux测试项指向它。
**不是只装ko就能省略DTB**，也不要同时启用原生DSI0 host/PHY。

将下面几行加入`/etc/initramfs-tools/modules`，随后运行`sudo update-initramfs -u`：

```text
fixed
pwm_bl
pwm_fan
lpi4a_tl060 backlight=1
s6d6ft0
```

最终initramfs必须包含官方供电、PWM依赖和新增模块。若自行拆包，必须合并所有
early/main段后生成完整模块索引，不能只对新增模块运行depmod覆盖原索引。
使用`lsinitramfs`核对文件，并以重启后的实际绑定结果验收。

默认`backlight=0`供手动测试，自动亮屏配置使用`backlight=1`。背光表为0/20/100，
sysfs亮度索引1才是本次验证的20%；**索引2为100%，未经本次验证**。
显示重新启用时保留用户亮度，不再覆盖为0。不要在使用中的DRM桥上强制卸载模块或解绑设备。

## 可选：RevyOS桌面接入

- **首次显示规避措施**：`lpi4a-display-ready`由LightDM的display-setup-script调用，
  在X启动时执行一次正常DPMS关闭/开启，恢复20%背光并关闭自动熄屏，
  规避当前首次扫描输出偶发黑屏。这不是根因已修复的声明，也不是后台反复点亮。
- **触控旋转**：`lpi4a-touch-map.py`在Xfce会话中监听XRandR事件，调用系统
  `xinput map-to-output`把Samsung触控映射到DSI-1，处理旋转/反射/输出位置。
  只改这一触控设备，不改蓝牙鼠标或底层校准；不采用轮询。
- **登录键盘**：安装发行版的`onboard`软件包，使用`80-tl060-keyboard.conf`。
  只保证登录键盘；桌面输入框自动弹出是另一项功能。
- `xfce4-power-manager.desktop`禁用电源管理器自启动，以维持当前测试的20%背光。
  这是当前验证配置，不代表已经完成通用电源管理。

从本目录安装桌面接入文件：

```sh
sudo install -d /usr/local/libexec /etc/lightdm/lightdm.conf.d \
  /etc/lightdm/lightdm-gtk-greeter.conf.d /etc/xdg/autostart
sudo install -m 755 desktop/lpi4a-display-ready desktop/lpi4a-touch-map.py /usr/local/libexec/
sudo install -m 644 desktop/50-tl060-display.conf /etc/lightdm/lightdm.conf.d/
sudo install -m 644 desktop/80-tl060-keyboard.conf /etc/lightdm/lightdm-gtk-greeter.conf.d/
sudo install -m 644 desktop/lpi4a-touch-map.desktop desktop/xfce4-power-manager.desktop /etc/xdg/autostart/
```

已有LightDM display-setup-script时应合并调用，不能覆盖其他必要初始化。
安装后注销/重启验证，不要在有未保存工作的会话中直接重启显示管理器。
6寸屏建议在Xfce设置2×缩放后重新登录；本次热切换菜单裁剪异常在重启后恢复。

参考：[泰山派时序与Samsung协议源码](../泰山派/)、
[Sipeed外设说明](https://wiki.sipeed.com/hardware/zh/lichee/th1520/lpi4a/6_peripheral.html)。
DSI/D-PHY代码保留RevyOS/VeriSilicon来源及GPL标识；触控协议参考Samsung sec_ts，
本实现不包含原版固件升级路径。
