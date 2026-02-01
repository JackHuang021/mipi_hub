linux-headers-6.1.84-999-rk2410_6.1.84-999_arm64.deb
linux-image-6.1.84-999-rk2410_6.1.84-999_arm64.deb

将上面两个 deb 包复制到板子上使用 dpkg 指令安装即可完成内核安装

sudo dpkg -i linux-headers-6.1.84-999-rk2410_6.1.84-999_arm64.deb
sudo dpkg -i linux-image-6.1.84-999-rk2410_6.1.84-999_arm64.deb
sudo reboot

重启完成后，可以通过 uname -a 查看当前启动的内核版本号，来查看是否安装成功

dtb+touch 支持屏幕和触摸
touch 仅支持触摸