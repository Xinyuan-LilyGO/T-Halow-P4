#include "sys_config.h"
#include "typesdef.h"
#include "list.h"
#include "dev.h"
#include "devid.h"
#include "osal/string.h"
#include "osal/mutex.h"
#include "osal/semaphore.h"
#include "osal/task.h"
#include "osal/timer.h"
#include "hal/spi.h"
#include "hal/gpio.h"
#include "dev/spi/hgic_spi_slave.h"
#include "hgic_raw.h"

#define SPI_BUS_RXBUFF_SIZE   (2048)

struct spi_master_bus {
    struct os_mutex     tx_lock;
    struct os_task      rx_task;
    struct os_semaphore rx_sema;
    struct spi_device  *dev;
    uint8               rxbuff[SPI_BUS_RXBUFF_SIZE];
};

void spidrv_write(void *priv, unsigned char *data, unsigned int len)
{
    spi_write((struct spi_device *)priv, data, len);
}

void spidrv_read(void *priv, unsigned char *data, unsigned int len)
{
    spi_read((struct spi_device *)priv, data, len);
}

void spidrv_cs(void *priv, char enable)
{
    spi_set_cs((struct spi_device *)priv, 0, !enable);
}

int raw_send(unsigned char *data, unsigned int len)
{
    struct spi_device *spi = (struct spi_device *)dev_get(HG_SPI0_DEVID);
    return hgic_spi_slave_write(spi, data, len);
}
//////////////////////////////////////////////////////////////

static void spi_master_rx_task(void *arg)
{
    struct spi_master_bus *spi_bus = (struct spi_master_bus *)arg;
    int32 len = 0;
    uint32 speed_tick;
    uint32 restart_tick;
    uint32 tx_bytes = 0;
    unsigned char *p_buf;
    unsigned int p_len;
    HGIC_RAW_RX_TYPE resp;

    speed_tick = os_jiffies();
    restart_tick = os_jiffies();
    while (1) {
        os_sema_down(&spi_bus->rx_sema, osWaitForever);
        if (gpio_get_val(PIN_SPI0_INT) != 0) {
            continue;
        }
        os_mutex_lock(&spi_bus->tx_lock, osWaitForever);
        len = hgic_spi_slave_read(spi_bus->dev, spi_bus->rxbuff, sizeof(spi_bus->rxbuff));
        if (len > 0) {
            p_buf = spi_bus->rxbuff;
            p_len = len;
            resp = hgic_raw_rx(&p_buf, &p_len);
            if (resp == HGIC_RAW_RX_TYPE_DATA) {
                hgic_raw_test2(spi_bus->rxbuff, 1024);
                tx_bytes += 2048;
            }
        }
        os_mutex_unlock(&spi_bus->tx_lock);
        if (TIME_AFTER(os_jiffies(), speed_tick + 1000)) {
            speed_tick = os_jiffies();
            os_printf("%d KBytes/s\r\n", tx_bytes/1024);
            hgic_raw_get_connect_state();
            tx_bytes = 0;
        }
    }
}

static void spi_bus_int_io_irq_hdl(int32 id, enum gpio_irq_event event)
{
    struct spi_master_bus *spi_bus = (struct spi_master_bus*)id;
    
    switch (event) {
        case GPIO_IRQ_EVENT_RISE:
            //os_printf("R\r\n");
            break;
        case GPIO_IRQ_EVENT_FALL:
            //os_printf("F\r\n");
            os_sema_up(&spi_bus->rx_sema);
            break;
        default:
            break;
    }
}

void user_init(void)
{
    struct spi_device *spi = (struct spi_device *)dev_get(HG_SPI0_DEVID);
    struct spi_master_bus *spi_bus = os_zalloc(sizeof(struct spi_master_bus));
    
    spi_bus->dev = spi;
    os_mutex_init(&spi_bus->tx_lock);
    os_sema_init(&spi_bus->rx_sema, 0);
    OS_TASK_INIT("spim", &spi_bus->rx_task, spi_master_rx_task, spi_bus, OS_TASK_PRIORITY_HIGH-1, 512);
    gpio_set_dir(PIN_SPI0_INT, GPIO_DIR_INPUT);
    gpio_driver_strength(PIN_SPI0_CLK, GPIO_DS_12MA);
    gpio_driver_strength(PIN_SPI0_IO0, GPIO_DS_12MA);
    gpio_set_mode(PIN_SPI0_INT, GPIO_PULL_UP, GPIO_PULL_LEVEL_10K);
    // make sure switch wiremode
    for (uint8 i = 0; i < 4; ++i) {
        spi_close(spi_bus->dev);
        spi_open(spi_bus->dev, 8000000, SPI_MASTER_MODE, i, SPI_CLK_MODE_0);
        hgic_spi_slave_wire_mode(spi_bus->dev, HGIC_SPI_WIRE_MODE_QUAD);
    }
    gpio_request_pin_irq(PIN_SPI0_INT, spi_bus_int_io_irq_hdl, (uint32)spi_bus, GPIO_IRQ_EVENT_FALL);
    os_printf("SPIM init done\r\n");
    spi_close(spi_bus->dev);
    spi_open(spi_bus->dev, 20000000, SPI_MASTER_MODE, SPI_WIRE_QUAD_MODE, SPI_CLK_MODE_0);
    spi_ioctl(spi_bus->dev, SPI_SAMPLE_DELAY, 6, 0);
    hgic_raw_test2(spi_bus->rxbuff, 1024);
}