
#include "typesdef.h"
#include "list.h"
#include "dev.h"
#include "devid.h"
#include "osal/string.h"
#include "osal/mutex.h"
#include "osal/task.h"
#include "osal/semaphore.h"
#include "osal/timer.h"
#include "hal/dma.h"
#include "hal/spi.h"
#include "hal/gpio.h"
#include "lib/ticker_api.h"
#include "lib/skb/skb.h"
#include "lib/mac_bus.h"
#include "lib/hgic_sdspi.h"
#include "lib/hgic_raw.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct mac_bus_spi {
    struct mac_bus        bus;
    struct spi_device    *dev;
    struct os_mutex       tx_lock;
    struct os_semaphore   tx_sema;
    struct os_semaphore   rx_sema;
    union {
        struct {
            struct SYS_TASK        rx_task;
            struct SYS_TASK        test_task;
            struct SYS_TASK        check_task;
            struct SYS_TASK        print_task;
            uint8 txbuf[2048];
            uint8 rxbuf[2048];
            uint32 tx_bytes;
            uint32 test_err;
        } master;
    };
};

#define SPI_DATA_BUF_LEN 512

static int32 spibus_master_write(struct mac_bus *bus, uint8 *buff, int32 len)
{
    int32 ret = RET_OK;
    struct mac_bus_spi *spi_bus = container_of(bus, struct mac_bus_spi, bus);

    if (buff == NULL || len <= 0) {
        os_printf("spi bus write invalid arguments\r\n");
        return RET_ERR;
    }

    ret = os_mutex_lock(&spi_bus->tx_lock, 500);
    if (ret) {
        os_printf("spi bus write wait tx_lock timeout, ret:%d\r\n", ret);
        return RET_ERR;
    }
    
    hgic_sdspi_write(spi_bus, buff, len);

    os_mutex_unlock(&spi_bus->tx_lock);
    return ret;
}

static void spibus_master_rxio_irq(int32 id, enum gpio_irq_event event)
{
    struct mac_bus_spi *spi_bus = (struct mac_bus_spi *)id;

    switch (event) {
        case GPIO_IRQ_EVENT_RISE:
            break;
        case GPIO_IRQ_EVENT_FALL:
            os_sema_up(&spi_bus->rx_sema);
            break;
        default:
            break;
    }
}

static void spibus_master_rx_task(void *data)
{
    //struct sk_buff *skb = NULL;
    struct mac_bus_spi *spi_bus = (struct mac_bus_spi *)data;
    uint32 seed = 0;
    int len;
    unsigned char *p_buf;
    unsigned int p_len;

    while (1) {
        os_sleep_ms(5);
        //os_sema_down(&spi_bus->rx_sema, osWaitForever);
        os_mutex_lock(&spi_bus->tx_lock, osWaitForever);
        len = hgic_sdspi_read(spi_bus, spi_bus->master.rxbuf, sizeof(spi_bus->master.rxbuf));
        if (len != RET_ERR && len > 0) {
            p_buf = spi_bus->master.rxbuf;
            p_len = len;
            if (hgic_raw_rx(spi_bus, &p_buf, &p_len) == HGIC_RAW_RX_TYPE_DATA) {
                if (os_memcmp(spi_bus->master.txbuf, p_buf, p_len) != 0) {
                    spi_bus->master.test_err++;
                } else {
                    spi_bus->master.tx_bytes += 1024;
                }
                // random data seed
                srand(seed++);
                for (uint32_t i = 0; i < 512; ++i) {
                    spi_bus->master.txbuf[i] = rand() & 0xff;
                }
                os_sema_up(&spi_bus->tx_sema);
            }
//            skb  = alloc_tx_skb(len + 24 + 128);
//            if (!skb) {
//                os_printf("alloc skb fail\r\n");
//                os_mutex_unlock(&spi_bus->tx_lock);
//                continue;
//            }
//            skb_reserve(skb, 128);
//            hw_memcpy(skb->data, spi_bus->master.rxbuf, len);
            //spi_bus->bus.recv(&spi_bus->bus, skb, len);
        }
        os_mutex_unlock(&spi_bus->tx_lock);
    }
}

static void spibus_master_test_task(void *data)
{
    struct mac_bus_spi *spi_bus = (struct mac_bus_spi *)data;
    
    while (1) {
        os_sema_down(&spi_bus->tx_sema, 5000);
        os_mutex_lock(&spi_bus->tx_lock, osWaitForever);
        hgic_raw_test2(spi_bus, spi_bus->master.txbuf, 512); // for test
        os_mutex_unlock(&spi_bus->tx_lock);
    }
}

static void spibus_master_check_task(void *data)
{
    struct mac_bus_spi *spi_bus = (struct mac_bus_spi *)data;

    while (1) {
        os_sleep_ms(100);
        os_mutex_lock(&spi_bus->tx_lock, osWaitForever);
        if (hgic_sdspi_detect_alive(spi_bus) == RET_ERR) {
            hgic_sdspi_init(spi_bus);
        }
        os_mutex_unlock(&spi_bus->tx_lock);
    }
}

static void spibus_master_print_task(void *data)
{
    struct mac_bus_spi *spi_bus = (struct mac_bus_spi *)data;

    while (1) {
        os_sleep_ms(1000);
        os_printf("%d KBytes/s, err:%d\r\n", spi_bus->master.tx_bytes/1024, spi_bus->master.test_err);
        spi_bus->master.tx_bytes = 0;
    }
}

void spidrv_write(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
    struct mac_bus_spi *spi_bus = (struct mac_bus_spi *)priv;
    spi_write(spi_bus->dev, data, len);
}

void spidrv_read(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
    struct mac_bus_spi *spi_bus = (struct mac_bus_spi *)priv;
    spi_read(spi_bus->dev, data, len);
}

void spidrv_cs(void *priv, char cs)
{
    struct mac_bus_spi *spi_bus = (struct mac_bus_spi *)priv;
    if (cs) {
        spi_set_cs(spi_bus->dev, SPIBUS_CS_SEL, 0);
    } else {
        spi_set_cs(spi_bus->dev, SPIBUS_CS_SEL, 1);
    }
}

int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int len, char flag)
{
    return 0;
}

/**
  * @brief  Raw send function implementation. API define in hgic_raw.c
  * @param  data: Date buffer to write. PS: malloc size need align sdio block size
            len : Real data length
  * @retval Read handle result
  */
int raw_send(void *priv, unsigned char *data, int len)
{
    struct mac_bus_spi *spi_bus = container_of(priv, struct mac_bus_spi, bus);
    return hgic_sdspi_write(spi_bus, data, len);
}

static int32 spibus_master_init(struct mac_bus_spi *spi_bus)
{
    spi_bus->bus.write = spibus_master_write;
    spi_open(spi_bus->dev, SPIBUS_CLK, SPI_MASTER_MODE, SPI_WIRE_NORMAL_MODE, SPI_CLK_MODE_0);

//    gpio_set_dir(SPIBUS_INTIO, GPIO_DIR_INPUT);
//    gpio_enable_irq(SPIBUS_INTIO, 1);
//    gpio_request_pin_irq(SPIBUS_INTIO, spibus_master_rxio_irq, (uint32)spi_bus, GPIO_IRQ_EVENT_FALL); // No consideration release irq now
    
    while (hgic_sdspi_init(spi_bus) == RET_ERR) {
        os_printf("Bus init error\r\n");
        os_sleep(1);
    }
    for (uint32_t i = 0; i < 512; ++i) {
        spi_bus->master.txbuf[i] = 0x55;
    }
    
    OS_TASK_INIT("spibus", &spi_bus->master.rx_task, spibus_master_rx_task, (uint32)spi_bus, OS_TASK_PRIORITY_ABOVE_NORMAL + 1, 512);
    OS_TASK_INIT("spibus_test", &spi_bus->master.test_task, spibus_master_test_task, (uint32)spi_bus, OS_TASK_PRIORITY_NORMAL, 512);
    OS_TASK_INIT("spibus_check", &spi_bus->master.check_task, spibus_master_check_task, (uint32)spi_bus, OS_TASK_PRIORITY_NORMAL, 256);
    OS_TASK_INIT("spibus_print", &spi_bus->master.print_task, spibus_master_print_task, (uint32)spi_bus, OS_TASK_PRIORITY_NORMAL+1, 512);
    //hgic_raw_test2(spi_bus, (char *)spi_bus->master.txbuf, 512); // for test
    os_sema_up(&spi_bus->tx_sema);
    return 0;
}

struct mac_bus *mac_bus_spi_attach(mac_bus_recv recv, void *priv)
{
    struct mac_bus_spi *spi_bus = os_zalloc(sizeof(struct mac_bus_spi));
    ASSERT(spi_bus && recv);
    spi_bus->bus.type  = MAC_BUS_TYPE_SPI;
    spi_bus->bus.recv  = recv;
    spi_bus->bus.priv  = priv;
    os_mutex_init(&spi_bus->tx_lock);
    os_sema_init(&spi_bus->tx_sema, 0);
    os_sema_init(&spi_bus->rx_sema, 0);
    spi_bus->dev = (struct spi_device *)dev_get(SPIBUS_DEV);
    ASSERT(spi_bus->dev);
    spibus_master_init(spi_bus);
    return &spi_bus->bus;
}

