/**
  ******************************************************************************
  * @file    hgic_sdspi.c
  * @author  HUGE-IC Application Team
  * @version V1.0.0
  * @date    28-07-2022
  * @brief   SPI slave mode driver
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2022 HUGE-IC</center></h2>
  *
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hgic_spi_slave.h"

/* Private typedef -----------------------------------------------------------*/
#define RET_OK (0)
#define RET_ERR (-1)

#define CS_DISABLE (0)
#define CS_ENABLE (1)

#define SPI_DATA_RETRY_TIMEOUT      3
#define SPI_STATUS_READY_TIMEOUT    64

#define SPI_CMD_RD_STATUS           0x05
#define SPI_CMD_WIRE_MODE           0x69
#define SPI_CMD_RX_DATA             0x03
#define SPI_CMD_TX_DATA             0x02

#define SPI_REG_RX_DATA_LEN_SHIFT   8
#define SPI_REG_RX_DATA_LEN_MASK    0x0000ff00
#define SPI_REG_SW_RESERVED_BIT     (1 << 5)
#define SPI_REG_WRONG_CMD_BIT       (1 << 4)
#define SPI_REG_RX_READY_BIT        (1 << 1)


void hgic_spi_slave_wire_mode(void *priv, unsigned char wire_mode)
{
    unsigned char cmd = 0;
    
    spidrv_cs(priv, CS_ENABLE);
    cmd = SPI_CMD_WIRE_MODE;
    spidrv_write(priv, &cmd, 1);
    spidrv_write(priv, &wire_mode, 1);
    spidrv_cs(priv, CS_DISABLE);
    printf("switch wire mode:%d\r\n", wire_mode);
}

int hgic_spi_slave_read(void *priv, unsigned char *buf, unsigned int len)
{
    unsigned int data_len = 0;
    unsigned int reg = 0;
    unsigned short retry = SPI_STATUS_READY_TIMEOUT;
    unsigned char cmd = 0;
    
    /* 1. Query data length */
    while (retry--) {
        spidrv_cs(priv, CS_ENABLE);
        cmd = SPI_CMD_RD_STATUS;
        spidrv_write(priv, &cmd, 1);
        spidrv_read(priv, (unsigned char *)&reg, 4);
        spidrv_cs(priv, CS_DISABLE);
        if (!(reg & SPI_REG_RX_READY_BIT) && retry == 0) {
            printf("spi slave not ready tx\r\n");
            return RET_ERR;
        } else if (reg & SPI_REG_RX_READY_BIT) {
            break;
        }
    }
    data_len = (reg & SPI_REG_RX_DATA_LEN_MASK) >> SPI_REG_RX_DATA_LEN_SHIFT;
    data_len = (data_len + 1) * 8;
    if (len < data_len) {
        printf("no buff\r\n");
        spidrv_cs(priv, CS_ENABLE);
        cmd = SPI_CMD_RX_DATA;
        spidrv_write(priv, &cmd, 1);
        spidrv_cs(priv, CS_DISABLE);
        return RET_ERR;
    }
    /* 2. Read data */
    retry = SPI_DATA_RETRY_TIMEOUT;
    while (retry--) {
        spidrv_cs(priv, CS_ENABLE);
        cmd = SPI_CMD_RX_DATA;
        spidrv_write(priv, &cmd, 1);
        spidrv_read(priv, buf, data_len);
        spidrv_cs(priv, CS_DISABLE);
        
        /* 3. Query status */
        spidrv_cs(priv, CS_ENABLE);
        cmd = SPI_CMD_RD_STATUS;
        spidrv_write(priv, &cmd, 1);
        spidrv_read(priv, (unsigned char *)&reg, 4);
        spidrv_cs(priv, CS_DISABLE);
        if (!(reg & SPI_REG_WRONG_CMD_BIT) && !(reg & SPI_REG_WRONG_CMD_BIT)) {
            break;
        }
    }
    return data_len;
}

int hgic_spi_slave_write(void *priv, unsigned char *data, unsigned int len)
{
    unsigned int reg = 0;
    unsigned short retry = SPI_STATUS_READY_TIMEOUT;
    unsigned char cmd = 0;
    
    if (data == NULL || len == 0) {
        return RET_ERR;
    }
    /* 1. Query status */
    while (retry--) {
        spidrv_cs(priv, CS_ENABLE);
        cmd = SPI_CMD_RD_STATUS;
        spidrv_write(priv, &cmd, 1);
        spidrv_read(priv, (unsigned char *)&reg, 4);
        spidrv_cs(priv, CS_DISABLE);
        if ((reg & SPI_REG_SW_RESERVED_BIT) && retry == 0) {
            printf("spi slave not ready rx\r\n");
            return RET_ERR;
        } else if (!(reg & SPI_REG_SW_RESERVED_BIT)) {
            break;
        }
    }
    /* 2. Write data */
    retry = SPI_DATA_RETRY_TIMEOUT;
    while (retry--) {
        spidrv_cs(priv, CS_ENABLE);
        cmd = SPI_CMD_TX_DATA;
        spidrv_write(priv, &cmd, 1);
        // 速度太高时需要加入delay
        spidrv_write(priv, data, len);
        spidrv_cs(priv, CS_DISABLE);
        
        /* 3. Query status */
        spidrv_cs(priv, CS_ENABLE);
        cmd = SPI_CMD_RD_STATUS;
        spidrv_write(priv, &cmd, 1);
        spidrv_read(priv, (unsigned char *)&reg, 4);
        spidrv_cs(priv, CS_DISABLE);
        if (!(reg & SPI_REG_WRONG_CMD_BIT) && !(reg & SPI_REG_WRONG_CMD_BIT)) {
            break;
        }
    }
    
    return len;
}
