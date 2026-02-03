

<h1 align = "center">🏆T-Halow-P4🏆</h1>

![alt text](image.png)

## :zero: Version 🎁

### 1、Version

### 2、Where to buy.

## :one: Product 🎁

1. ESP32P4 支持 SPI、I2S、I2C、LED PWM、MCPWM、RMT、ADC、UART 和 TWAI™ 等常用外设。它还支持 USB OTG 2.0 HS、以太网和 SDIO Host 3.0；封装内叠封32 MB PSRAM，QSPI接口连接16MB Nor Flash；
2. 板载 MIPI-DSI 显示屏接口，支持 JPEG 图像解码(1080P 30fps)，PPA(像素处理加速器)，2D DMA(2D图像加速器)；
3. 板载 MIPI-CSI 摄像他接口，1080P画面，ISP，H264编码，JPEG编码；
4. 外挂 ESP32-C6-MINI，使用 SDIO 扩展 WIFI6 或 BLE5 无线功能 (使用 esp-hosted-mcu 方案)；
5. 带有 T-Halow 模块，支持 WIFI Halow，在提供2.4GHz和5GHz相同传输功率的情况下，传输距离更远；


## :two: Module 🎁

### 1、Halow 模块

**1）快速开始**

[examples/halow_spi_single](./examples/halow_spi_single/) 展示了Halow模块如何收发的过程，数据传输链路如下；

~~~
ESP32P4 -> SPI/SDIO -> Halow -> RF(AP) -> RF(STA) -> SPI/SDIO -> Halow -> ESP32P4
~~~

Halow 模块通过 SPI + UART 与 ESP32P4 连接；

SPI 用于数据传输，使用的是泰芯官方的提供的驱动程序 [taixin-nonos-driver](https://www.taixin-semi.com/upload/files/productFile/20251204/taixin-nonos-driver_20251204162053.zip)，驱动的一直和 API 接口参考文档 [泰芯non-os_WiFi驱动开发指南.pdf](./hardware/泰芯non-os_WiFi驱动开发指南.pdf)；


UART 用于收发AT命令和显示运行信息，AT使用参考文档 [AT_cmd.md](./doc/AT_cmd.md)，更多细节参考[泰芯AH模组AT指令开发指南.pdf](./hardware/泰芯AH模组AT指令开发指南.pdf)

![alt text](image-1.png)

**2）AH-V1.6-SDK**

提供了生成 Halow 固件的工程，参考 [AH-V1.6-SDK说明](./AH-V1.6-SDK/readme_cn.md)

### 2、ESP32C6


## :three: Quick Start 🎁

### 1、esp-idf 环境搭建

项目的例程都是在 esp-idf v5.4.1 环境下编译，所以使用 T-Halow-P4 的例程时，需要保证 esp-idf 的版本 >= 5.4.1;

esp-idf 编译环境的搭建，可以参考esp32官方的文档；

https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/get-started/index.html


### 2、项目编译

1）配置工程

请进入 examples/xxx 目录，设置 ESP32P4 为目标芯片，然后运行工程配置工具 menuconfig；

examples 下面的例程已经通过 `sdkconfig.defaults` 文件配置了项目，可以直接使用 `idf.py build` 编译项目；

~~~bash
cd ~/examples/xxx
idf.py set-target esp32p4
idf.py menuconfig
~~~

2）编译烧录

请使用以下命令，编译烧录工程：

~~~bash
idf.py build
~~~

在下载程序前，因该将 ESP32P4 开发板按照下面的方式设置为下载模式；

~~~
1. 插上USB，并打开串口工具；
2. 按住 P4 的 BOOT 键不松口；
3. 然后按下 P4 的 RST 键，马上松手，看见串口输出 wait for download，表示已经进入下载模式；
4. 松开 BOOT 键，关闭串口；
~~~

进入下载模式后，请运行以下命令，将刚刚生成的二进制文件烧录至 ESP32P4 开发板：

~~~bash
idf.py -p PORT flash
~~~

在 Windows 环境中 PORT 通常是 `COMxx` ，在 Linux 环境中 PORT 通常是 `/dev/ttyxx`

程序下载完成后，复位设备，运行程序；

## :four: Pins 🎁


~~~c

/******************************
 *          I2C Pin
 ******************************/

 I2C_SCL   ---   IO8
 I2C_SDA   ---   IO7

/******************************
 *          Halow Pin
 ******************************/

AH_CMD   ---   IO44
AH_CLK   ---   IO43
AH_D3    ---   IO42
AH_D2    ---   IO41
AH_D1    ---   IO40
AH_D0    ---   IO39
AH_TX    ---   IO12
AH_RX    ---   IO13

/******************************
 *         ESP32C6 Pin
 ******************************/

ESP32C6_CMD    ---   IO19
ESP32C6_CLK    ---   IO18
ESP32C6_D3     ---   IO17
ESP32C6_D2     ---   IO16
ESP32C6_D1     ---   IO15
ESP32C6_D0     ---   IO14
ESP32C6_WAKEUP ---   IO6

/******************************
 *         Camera/LCD Pin
 ******************************/

// By default, there is no Camera and LCD.
// The MIPI interface used by the Camera and LCD, 
// and the connection method with esp32P4 is as follows

TOUCH_SCL    ---   IO8
TOUCH_SDA    ---   IO7
TOUCH_INT    ---   IO11
TOUCH_RST    ---   IO10
MIPI_DSI_RST ---   IO9



                                   GND                                                                   GND
                ┌────────────────────────────────────────────────┐             ┌─────────────────────────────────────────────────────────┐
                │                                                │             │                                                         │
                │                                                │             │                                                         │
                │                                                │             │                                                         │
                │                                                │             │                                                         │
                │                                                │             │                                                         │
                │                                                │             │                                                         │
                │                                                │             │                                                         │
                │                                                │             │                                                         │
                │                                ┌───────────────┴─────────────┴──────────────────┐                                      │
                │                                │                                                │                           ┌──────────┴───────────┐
                │                                │                                                │      DSI DATA 1P          │                      │
                │                                │                                                ├───────────────────────────┤                      │
    ┌───────────┴─────────┐ CSI DATA 1P          │                                                │                           │                      │
    │                     ├──────────────────────┤                                                │      DSI DATA 1N          │                      │
    │                     │                      │                                                ├───────────────────────────┤                      │
    │                     │ CSI DATA 1N          │                  ESP32-P4                      │                           │                      │
    │       Camera        ├──────────────────────┤                                                │      DSI CLK N            │      LCD Screen      │
    │                     │                      │                                                ├───────────────────────────┤                      │
    │                     │ CSI CLK N            │                                                │                           │                      │
    │                     ├──────────────────────┤                                                │      DSI CLK P            │                      │
    │                     │                      │                                                ├───────────────────────────┤                      │
    │                     │ CSI CLK P            │                                                │                           │                      │
    │                     ├──────────────────────┤                                                │      DSI DATA 0P          │                      │
    │                     │                      │                                                ├───────────────────────────┤                      │
    │                     │ CSI DATA 0P          │                                                │                           │                      │
    │                     ├──────────────────────┤                                                │      DSI DATA 0N          │                      │
    │                     │                      │                                                ├───────────────────────────┤                      │
    │                     │ CSI DATA 0N          │                                                │                           │                      │
    │                     ├──────────────────────┤                                                │                           └──────────────────────┘
    │                     │                      │                                                │
    └───────┬──┬──────────┘                      │                                                │
            │  │           I2C SCL               │                                                │
            │  └─────────────────────────────────┤                                                │
            │              I2C SDA               │                                                │
            └────────────────────────────────────┤                                                │
                                                 └────────────────────────────────────────────────┘


~~~

## :five: Test 🎁

## :six: FAQ 🎁

## :seven: Schematic & 3D 🎁

NULL

# This warehouse is temporarily unavailable

## ESP32P4-Halow 核心板

### 功能特性：



### 编译环境

esp-idf v5.4 


### 使用的idf的库

- [ESP Registry](https://components.espressif.com/)
- [ESP Registry Document](https://docs.espressif.com/projects/idf-component-manager/en/latest/index.html)

~~~bash

"espressif/esp_hosted^1.4.1"

"espressif/esp_wifi_remote^0.8.5"

"espressif/esp_lcd_ek79007^1.0.2"

"lvgl/lvgl"

~~~