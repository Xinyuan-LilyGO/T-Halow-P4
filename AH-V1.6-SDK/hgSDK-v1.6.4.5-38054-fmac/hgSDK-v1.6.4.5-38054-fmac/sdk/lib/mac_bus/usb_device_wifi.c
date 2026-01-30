/*****************************************************************************
* Module    : usb
* File      : usb_dev_wifi.c
* Author    :
* Function  : USB wifi  huge-ic defined, refer to ch9.h
*****************************************************************************/
#include "typesdef.h"
#include "list.h"
#include "dev.h"
#include "devid.h"
#include "osal/string.h"
#include "osal/mutex.h"
#include "osal/semaphore.h"
#include "osal/task.h"
#include "hal/usb_device.h"
#include "lib/usb_device_wifi.h"

#include "dev/usb/hgusb11_base.h"
#include "dev/usb/hgusb11_dev_ep0.h"
#include "dev/usb/hgusb11_dev_tbl.h"
#include "dev/usb/hgusb11_dev_api.h"


#define USB_WIFI_TX_EP                  1
#define USB_WIFI_RX_EP                  1
#define USB_WIFI_EP_MAX_PKT_SIZE        64

#define USB_VID                         0xA012
#define USB_PID                         0x4002


//设备描述符
const uint8_t tbl_usb_wifi_device_descriptor[18] = {
    18,                 // Num bytes of the descriptor
    1,                  // Device Descriptor type
    0x10, 0x01,         // Revision of USB Spec. (in BCD)
    0,                  // Mass Storage Class
    0,                  // General Mass Storage Sub Class
    0,                  // Do not use Class specific protocol
    0x40,               // Max packet size of Endpoint 0
    (USB_VID >> 0) & 0xff,
    (USB_VID >> 8) & 0xff,         // Vendor ID
    (USB_PID >> 0) & 0xff,
    (USB_PID >> 8) & 0xff,         // Product ID
    0x00, 0x01,         // Device Revision (in BCD)
    1,                  // Index of Manufacture string descriptor
    2,                  // Index of Product string descriptor
    3,                  // Index of Serial No. string descriptor
    1                   // Num Configurations, Must = 1
};

//配置描述符 通用配置
const uint8_t tbl_usb_wifi_config_all_descriptor_gen[9] = {
    9,                  // Num bytes of this descriptor
    2,                  // Configuration descriptor type
    158, 0,             // Total size of configuration
    4,                  // Num Interface, 会根据最后实际接口的数量来配置
    1,                  // Configuration number
    0,                  // Index of Configuration string descriptor
    0x80,               // Configuration characteristics: BusPowerd
    200, //0x32,           // Max current, unit is 2mA
};

//配置描述符 自定义WIFI
const uint8_t tbl_usb_wifi_config_all_descriptor_wifi[23] = {
//Bulk Interface 9 + 7 + 7= 23
    9,                  // Num bytes of this descriptor
    4,                  // Interface descriptor type
    0,                  // Interface Number，暂时填0，会根据最后实际配置来依次递加
    0,                  // Alternate interface number
    2,                  // Num endpoints of this interface
    0xff,               // Interface Class: unknown
    0xff,               // Interface Sub Class: unknown
    0xff,               // Class specific protocol: unknown
    0,                  // Index of Interface string descriptor

    7,                  // Num bytes of this descriptor
    5,                  // Endpoint descriptor type
    USB_WIFI_TX_EP,            //BULKOUT_EP, // Endpoint number, bit7=0 shows OUT
    2,                  // Bulk endpoint
    USB_WIFI_EP_MAX_PKT_SIZE, 0x00,         // Maximum packet size
    0,                  // no use for bulk endpoint

    7,                  // Num bytes of this descriptor
    5,                  // Endpoint descriptor type
    USB_WIFI_RX_EP | 0x80,   // Endpoint number, bit7=1 shows IN
    2,                  // Bulk endpoint
    USB_WIFI_EP_MAX_PKT_SIZE, 0x00,         // Maximum packet size
    0,                  // No use for bulk endpoint
};

//语言
const uint8_t tbl_usb_wifi_language_id[4] = {
    4,              // Num bytes of this descriptor
    3,              // String descriptor
    0x09, 0x04,     // Language ID
};

//厂商信息
const uint8_t tbl_usb_wifi_str_manufacturer[16] = {
    16,             // Num bytes of this descriptor
    3,              // String descriptor
    'G',    0,
    'e',    0,
    'n',    0,
    'e',    0,
    'r',    0,
    'i',    0,
    'c',    0
};

//产品信息
const uint8_t tbl_usb_wifi_str_product[28] = {
    28,             // Num bytes of this descriptor
    3,              // String descriptor
    'U',    0,
    'S',    0,
    'B',    0,
    '2',    0,
    '.',    0,
    '0',    0,
    ' ',    0,
    'D',    0,
    'e',    0,
    'v',    0,
    'i',    0,
    'c',    0,
    'e',    0
};

//序列号
const uint8_t tbl_usb_wifi_str_serial_number[30] = {
    30,         // Num bytes of this descriptor
    3,          // String descriptor
    '2',    0,
    '0',    0,
    '1',    0,
    '7',    0,
    '0',    0,
    '8',    0,
    '2',    0,
    '9',    0,
    '0',    0,
    '0',    0,
    '0',    0,
    '0',    0,
    '0',    0,
    '1',    0
};


static const struct usb_device_cfg usb_dev_wifi_cfg = {
    .vid        = 0xA012,
    .pid        = 0x4002,
    .speed      = USB_SPEED_FULL,
    .p_device_descriptor = (uint8 *)tbl_usb_wifi_device_descriptor,
    .p_config_descriptor_head = (uint8 *)tbl_usb_wifi_config_all_descriptor_gen,

    .p_config_desc = (uint8 *)tbl_usb_wifi_config_all_descriptor_wifi,
    .config_desc_len = sizeof(tbl_usb_wifi_config_all_descriptor_wifi),
    .interface_num  =   1,

    .p_language_id = (uint8 *)tbl_usb_wifi_language_id,
    .language_id_len = sizeof(tbl_usb_wifi_language_id),
    .p_str_manufacturer = (uint8 *)tbl_usb_wifi_str_manufacturer,
    .str_manufacturer_len = sizeof(tbl_usb_wifi_str_manufacturer),
    .p_str_product = (uint8 *)tbl_usb_wifi_str_product,
    .str_product_len = sizeof(tbl_usb_wifi_str_product),
    .p_str_serial_number = (uint8 *)tbl_usb_wifi_str_serial_number,
    .str_serial_number_len = sizeof(tbl_usb_wifi_str_serial_number),

    .ep_nums              =  2,
    .ep_cfg[0].ep_id      =  USB_WIFI_RX_EP,
    .ep_cfg[0].ep_type    =  USB_ENDPOINT_XFER_BULK,
    .ep_cfg[0].ep_dir_tx  =  0,
    .ep_cfg[0].max_packet_size      =  USB_WIFI_EP_MAX_PKT_SIZE,
    .ep_cfg[1].ep_id      =  USB_WIFI_TX_EP,
    .ep_cfg[1].ep_type    =  USB_ENDPOINT_XFER_BULK,
    .ep_cfg[1].ep_dir_tx  =  1,
    .ep_cfg[1].max_packet_size      =  USB_WIFI_EP_MAX_PKT_SIZE,
};

__init int32 usb_device_wifi_open(struct usb_device *p_usb_d)
{
    return usb_device_open(p_usb_d, (struct usb_device_cfg *)&usb_dev_wifi_cfg);
}

int32 usb_device_wifi_close(struct usb_device *p_usb_d)
{
    return usb_device_close(p_usb_d);
}

int32 usb_device_wifi_auto_tx_null_pkt_disable(struct usb_device *p_usb_d)
{
    return usb_device_ioctl(p_usb_d, USB_DEV_IO_CMD_AUTO_TX_NULL_PKT_DISABLE, USB_WIFI_TX_EP);
}

int32 usb_device_wifi_auto_tx_null_pkt_enable(struct usb_device *p_usb_d)
{
    return usb_device_ioctl(p_usb_d, USB_DEV_IO_CMD_AUTO_TX_NULL_PKT_ENABLE, USB_WIFI_TX_EP);
}

int32 usb_device_wifi_write(struct usb_device *p_usb_d, int8 *buff, uint32 len)
{
    return usb_device_write(p_usb_d, USB_WIFI_RX_EP, buff, len, 0);
}

int32 usb_device_wifi_read(struct usb_device *p_usb_d, int8 *buff, uint32 len)
{
    return usb_device_read(p_usb_d, USB_WIFI_TX_EP, buff, len, 0);
}


