
请参阅README。有关示例的更多信息，请访问下层示例目录中的Md文件。

## camera_dsi

MIPI摄像头演示，摄像头使用的是 esp32_p4_function_ev_board 开发板带的摄像头，型号为 SC2336；

## display

MIPI的屏幕和LVGL演示，显示屏使用的是 esp32_p4_function_ev_board 开发板带的显示屏，型号为 ek79007 1200X600；

## esp-hosted-c6-Slave

参考 [esp32_p4_function_ev_board.md](https://github.com/espressif/esp-hosted-mcu/blob/ee11e08cd4f0b8f5f5c4c4138fa890697562d3cf/docs/esp32_p4_function_ev_board.md) 如何使用 esp-hosted；

与 iperf 例程一起使用，测试网络通信速度；

ESP32-C6-MINI 需要下载 esp-hosted-mcu 项目中的 slave 固件 (也就是当前目录下的 esp-hosted-c6-slave)，才可以通过 SDIO 与 P4 通信；

下载方式：

1. 使用 usb 转串口工具与 ESP32-C6-MINI 模块连接；C6-MINI 与串口的接线为 TX-RX、RX-TX、GND-GND、BOOT-GND；C6-MINI 的 BOOT 引脚需要接地进入下载模式；

2. 给板子上电，esp32p4 也需要进入下载模式，避免 C6-MINI 在下载程序的被 esp32p4 干扰，导致下载失败；

3. 然后编译下载 esp-hosted-c6-slave 程序，下载完成后断开 C6-MINI 的 BOOT 与 GND 的连接，然后重新上电就可以了；

## halow_spi

暂时不可用

## iperf

网络测试工具，本示例介绍了常用性能测量工具 iperf 使用的协议。性能可以在运行本例的两个ESP目标之间进行测量，也可以在单个ESP目标和运行 iperf 工具的计算机之间进行测量。

参考 [iperf](https://github.com/espressif/esp-hosted-mcu/issues/3)

使用该工具时，ESP32-C6-MINI 模块需要先下载 esp-hosted-c6-Slave 程序；

## ppa_dsi

像素处理加速器 (PPA) 模块，用于实现图像旋转、缩放、镜像和叠加等图像算法的硬件加速。

## touch

触摸测试，触摸型号 GT911；

## video_lcd_display

使用 [esp-video](https://github.com/espressif/esp-video-components/tree/master/esp_video/examples#enable-isp-pipelines) 组件，设置摄像画面一些参数，如曝光，白平衡，锐化等；
