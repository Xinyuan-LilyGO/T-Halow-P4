/**
  ******************************************************************************
  * @file    hgic_sdspi.c
  * @author  HUGE-IC Application Team
  * @version V1.0.0
  * @date    28-01-2021
  * @brief   SDIO host in SPI mode driver
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2021 HUGE-IC</center></h2>
  *
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hgic_sdspi.h"

/* Private typedef -----------------------------------------------------------*/
/* SDIO cmd struct */
typedef struct sdio_cmd_format
{
    unsigned char cmd;
    unsigned char arg1;
    unsigned char arg2;
    unsigned char arg3;
    unsigned char arg4;
    unsigned char flags;
    unsigned char data_token;
    int len;
    unsigned char resp[4];
} SDIO_CMD;

/* "scratch" is per-{command,block} data exchanged with the card */
struct scratch
{
    unsigned char status[29];
};

/* SDIO Function enum */
typedef enum
{
    FUNCTION_0,
    FUNCTION_1,
    FUNCTION_2,
    FUNCTION_3,
    FUNCTION_4,
    FUNCTION_5,
    FUNCTION_6,
    FUNCTION_7,
} SDIO_FUNC;

#define RET_OK (0)
#define RET_ERR (-1)

#define CMD_CRC (0)
#define DATA_CRC (1)

#define CPU_TRANS (0)
#define DMA_TRANS (1)

#define CS_DISABLE (0)
#define CS_ENABLE (1)

#define FLAGS_INBOOT (1 << 0)

#define SDIO_RESP_RETRY_CNT (8)
#define SDIO_BUSY_RETRY_CNT (2048)
#define SDIO_BLOCK_SIZE (64)

/* SDIO CIA registers */
#define CCCR_SDIO_VER (0x00)
#define SD_SPEC_VER (0x01)
#define IO_ENABLE (0x02)
#define IO_READY (0x03)
#define INT_ENABLE (0x04)
#define INT_PENDING (0x05)
#define IO_ABORT (0x06)
#define BUS_IF_CTRL (0x07)
#define CARD_CAPABILITY (0x08)
#define COM_CIS_POINTER (0x09)
#define BUS_SUSPEND (0x0C)
#define FUNCTION_SELECT (0x0D)
#define EXCE_FLAGS (0x0E)
#define READY_FLAGS (0x0F)
#define FUNC0_IO_BLOCK_SIZE (0x10)
#define POWER_CONTROL (0x12)
#define HIGH_SPEED (0x13)

#define FUNC1_IO_BLOCK_SIZE (0x110)

/* SDIO slave function_1 register */
#define SLAVE_DATA_PORT (0x00)
#define SLAVE_INTR_ID (0x04)
#define SLAVE_CLK_WKUP (0x2C)
#define SLAVE_AHB_TRANS_CNT_BL (0x1C)
#define SLAVE_AHB_TRANS_CNT (0x49)

/* SDIO CIS infomation */
#define CISTPL_NULL (0x00)
#define CISTPL_CHECKSUM (0x10)
#define CISTPL_VERS_1 (0x15)
#define CISTPL_ALTSTR (0x16)
#define CISTPL_MANFID (0x20)
#define CISTPL_FUNCID (0x21)
#define CISTPL_FUNCE (0x22)
#define CISTPL_SDIO_STD (0x91)
#define CISTPL_SDIO_EXT (0x92)
#define CISTPL_END (0xff)

#define R1_SPI_IDLE (1 << 0)
#define R1_SPI_ILLEGAL_COMMAND (1 << 2)
#define R1_SPI_COM_CRC (1 << 3)
#define R1_SPI_FUNCTION_NUMBER (1 << 4)
#define R1_SPI_PARAMETER (1 << 6)

#define RSP_SPI_S1 (1 << 0)
#define RSP_SPI_S2 (1 << 1)
#define RSP_SPI_B4 (1 << 2)
#define RSP_SPI_BUSY (1 << 3)
#define CMD_READ (1 << 4)
#define CMD_WRITE (1 << 5)

#define RSP_SPI_R1 (RSP_SPI_S1)
#define RSP_SPI_R1B (RSP_SPI_S1 | RSP_SPI_BUSY)
#define RSP_SPI_R2 (RSP_SPI_S1 | RSP_SPI_S2)
#define RSP_SPI_R3 (RSP_SPI_S1 | RSP_SPI_B4)
#define RSP_SPI_R4 (RSP_SPI_S1 | RSP_SPI_B4)
#define RSP_SPI_R5 (RSP_SPI_S1 | RSP_SPI_S2)
#define RSP_SPI_R7 (RSP_SPI_S1 | RSP_SPI_B4)

#define spi_resp_type(cmd) ((cmd)->flags & \
                            (RSP_SPI_S1 | RSP_SPI_BUSY | RSP_SPI_S2 | RSP_SPI_B4))

static struct scratch sd_mgr;

/*
 * Table for CRC-7 (polynomial x^7 + x^3 + 1).
 * This is a big-endian CRC (msbit is highest power of x),
 * aligned so the msbit of the byte is the x^6 coefficient
 * and the lsbit is not used.
 */
const unsigned char crc7_be_syndrome_table[256] = {
    0x00, 0x12, 0x24, 0x36, 0x48, 0x5a, 0x6c, 0x7e,
    0x90, 0x82, 0xb4, 0xa6, 0xd8, 0xca, 0xfc, 0xee,
    0x32, 0x20, 0x16, 0x04, 0x7a, 0x68, 0x5e, 0x4c,
    0xa2, 0xb0, 0x86, 0x94, 0xea, 0xf8, 0xce, 0xdc,
    0x64, 0x76, 0x40, 0x52, 0x2c, 0x3e, 0x08, 0x1a,
    0xf4, 0xe6, 0xd0, 0xc2, 0xbc, 0xae, 0x98, 0x8a,
    0x56, 0x44, 0x72, 0x60, 0x1e, 0x0c, 0x3a, 0x28,
    0xc6, 0xd4, 0xe2, 0xf0, 0x8e, 0x9c, 0xaa, 0xb8,
    0xc8, 0xda, 0xec, 0xfe, 0x80, 0x92, 0xa4, 0xb6,
    0x58, 0x4a, 0x7c, 0x6e, 0x10, 0x02, 0x34, 0x26,
    0xfa, 0xe8, 0xde, 0xcc, 0xb2, 0xa0, 0x96, 0x84,
    0x6a, 0x78, 0x4e, 0x5c, 0x22, 0x30, 0x06, 0x14,
    0xac, 0xbe, 0x88, 0x9a, 0xe4, 0xf6, 0xc0, 0xd2,
    0x3c, 0x2e, 0x18, 0x0a, 0x74, 0x66, 0x50, 0x42,
    0x9e, 0x8c, 0xba, 0xa8, 0xd6, 0xc4, 0xf2, 0xe0,
    0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54, 0x62, 0x70,
    0x82, 0x90, 0xa6, 0xb4, 0xca, 0xd8, 0xee, 0xfc,
    0x12, 0x00, 0x36, 0x24, 0x5a, 0x48, 0x7e, 0x6c,
    0xb0, 0xa2, 0x94, 0x86, 0xf8, 0xea, 0xdc, 0xce,
    0x20, 0x32, 0x04, 0x16, 0x68, 0x7a, 0x4c, 0x5e,
    0xe6, 0xf4, 0xc2, 0xd0, 0xae, 0xbc, 0x8a, 0x98,
    0x76, 0x64, 0x52, 0x40, 0x3e, 0x2c, 0x1a, 0x08,
    0xd4, 0xc6, 0xf0, 0xe2, 0x9c, 0x8e, 0xb8, 0xaa,
    0x44, 0x56, 0x60, 0x72, 0x0c, 0x1e, 0x28, 0x3a,
    0x4a, 0x58, 0x6e, 0x7c, 0x02, 0x10, 0x26, 0x34,
    0xda, 0xc8, 0xfe, 0xec, 0x92, 0x80, 0xb6, 0xa4,
    0x78, 0x6a, 0x5c, 0x4e, 0x30, 0x22, 0x14, 0x06,
    0xe8, 0xfa, 0xcc, 0xde, 0xa0, 0xb2, 0x84, 0x96,
    0x2e, 0x3c, 0x0a, 0x18, 0x66, 0x74, 0x42, 0x50,
    0xbe, 0xac, 0x9a, 0x88, 0xf6, 0xe4, 0xd2, 0xc0,
    0x1c, 0x0e, 0x38, 0x2a, 0x54, 0x46, 0x70, 0x62,
    0x8c, 0x9e, 0xa8, 0xba, 0xc4, 0xd6, 0xe0, 0xf2};

/** CRC table for the CRC ITU-T V.41 0x1021 (x^16 + x^12 + x^15 + 1) */
const unsigned short crc_itu_t_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0};

static inline unsigned char crc7_be_byte(unsigned char crc, unsigned char data)
{
    return crc7_be_syndrome_table[crc ^ data];
}

static inline unsigned short crc_itu_t_byte(unsigned short crc, const unsigned char data)
{
    return (crc << 8) ^ crc_itu_t_table[((crc >> 8) ^ data) & 0xff];
}

/**
 * crc7 - update the CRC7 for the data buffer
 * @crc:     previous CRC7 value
 * @buffer:  data pointer
 * @len:     number of bytes in the buffer
 * Context: any
 *
 * Returns the updated CRC7 value.
 * The CRC7 is left-aligned in the byte (the lsbit is always 0), as that
 * makes the computation easier, and all callers want it in that form.
 *
 */
unsigned char crc7_be(unsigned char crc, const unsigned char *buffer, unsigned int len)
{
    while (len--)
        crc = crc7_be_byte(crc, *buffer++);
    return crc;
}

/**
 * crc_itu_t - Compute the CRC-ITU-T for the data buffer
 *
 * @crc:     previous CRC value
 * @buffer:  data pointer
 * @len:     number of bytes in the buffer
 *
 * Returns the updated CRC value
 */
unsigned short crc_itu_t(unsigned short crc, const unsigned char *buffer, unsigned int len)
{
    while (len--)
        crc = crc_itu_t_byte(crc, *buffer++);
    return crc;
}

static int sd_skip(void *priv, unsigned int retry, unsigned int len, unsigned char byte, char dma_flag)
{
    unsigned char *cp = sd_mgr.status;
    int i;

    while (retry--)
    {
        spidrv_read(priv, cp, len, dma_flag);
        for (i = 0; i < len; ++i)
        {
            if (cp[i] != byte)
                return 0;
        }
    }
    return -145;
}

static inline int sd_wait_unbusy(void *priv, unsigned int retry)
{
    return sd_skip(priv, retry, sizeof(sd_mgr.status), 0, DMA_TRANS);
}

static inline int sd_wait_readtoken(void *priv, unsigned int retry)
{
    return sd_skip(priv, retry, 1, 0xff, CPU_TRANS);
}

static int sd_response_get(void *priv, SDIO_CMD *command)
{
    unsigned char *cp = sd_mgr.status;
    unsigned char *end = cp + command->len;
    int i, value = 0;
    unsigned char temp;

    for (i = 0; i < 8; ++i)
    {
        if (*cp++ != 0xff)
        {
            value = -1; /* error */
            goto __done;
        }
    }
    while (cp < end && *cp == 0xff)
        cp++;
    if (cp == end)
    {
        if (command->flags & CMD_READ)
        {
            for (i = 0; i < 8; ++i)
            {
                spidrv_read(priv, &temp, 1, CPU_TRANS);
                if (temp != 0xff)
                    goto __checkstatus;
            }
        }
        value = -145; /* Timeout */
        goto __done;
    }
__checkstatus:
    if (*cp & 0x80)
    {
        /* bit-shifted not handle yet */
    }
    else
    {
        command->resp[0] = *cp++;
    }
    if (command->resp[0] != 0)
    {
        if (R1_SPI_PARAMETER & command->resp[0])
            value = -14; /* Bad address */
        else if (R1_SPI_ILLEGAL_COMMAND & command->resp[0])
            value = -89; /* Function not implemented */
        else if (R1_SPI_COM_CRC & command->resp[0])
            value = -88; /* Illegal byte sequence */
        else if (R1_SPI_FUNCTION_NUMBER & command->resp[0])
            value = -5; /* I/O error */
        /* else R1_SPI_IDLE, "it's resetting" */
    }
    switch (spi_resp_type(command))
    {
    case RSP_SPI_R1B:
        /* todo */
        break;
    case RSP_SPI_R2:
        if (cp == end)
        {
            spidrv_read(priv, command->resp, 1, CPU_TRANS);
        }
        else
        {
            command->resp[0] = *cp;
        }
        break;
    case RSP_SPI_R3:
        if (cp == end)
        {
            spidrv_read(priv, command->resp, 4, DMA_TRANS);
        }
        else
        {
            memcpy(command->resp, cp, 4);
        }
        break;
    case RSP_SPI_R1:
        break;
    default:
        break;
    }
__done:
    return value;
}

static int sd_send_command(void *priv, SDIO_CMD *command)
{
    unsigned char *cp = sd_mgr.status;
    memset(cp, 0xff, sizeof(sd_mgr.status));
    cp[1] = 0x40 | command->cmd;
    memcpy(cp + 2, &command->arg1, 4);
    cp[6] = crc7_be(0, cp + 1, 5) | 0x01;
    cp += 7;
    if (command->flags & CMD_READ)
    {
        cp += 2;
    }
    else
    {
        cp += 10;
        if (command->flags & RSP_SPI_S2)
            cp++;
        else if (command->flags & RSP_SPI_B4)
            cp += 4;
        else if (command->flags & RSP_SPI_BUSY)
            cp = sd_mgr.status + sizeof(sd_mgr.status);
        /* else:  R1 (most commands) */
    }
    command->len = cp - sd_mgr.status;
    spidrv_write_read(priv, sd_mgr.status, sd_mgr.status, cp - sd_mgr.status);
    return sd_response_get(priv, command);
}

static int sd_setup_data_message(void *priv, SDIO_CMD *cmd, unsigned char *data, unsigned int len)
{
    int resp=-1;
    unsigned short crc;
    unsigned char temp[64 + 5];
    unsigned char *cp;

    if (cmd->flags & CMD_READ)
    {
        resp = sd_wait_readtoken(priv, 8);
        spidrv_read(priv, data, len, DMA_TRANS);
        spidrv_read(priv, (unsigned char *)&crc, 2, CPU_TRANS);
    }
    else
    {
        cp = temp;
        *cp++ = cmd->data_token;
        memcpy(cp, data, len);
        cp += len;
        memset(cp, 0xff, 4);
        cp += 2;
        spidrv_write_read(priv, temp, temp, len + 5);
        cmd->resp[0] = *cp++;
        if (cmd->resp[0] != 0)
        {
            if (R1_SPI_PARAMETER & cmd->resp[0])
                resp = -14; /* Bad address */
            else if (R1_SPI_ILLEGAL_COMMAND & cmd->resp[0])
                resp = -89; /* Function not implemented */
            else if (R1_SPI_COM_CRC & cmd->resp[0])
                resp = -88; /* Illegal byte sequence */
            else if (R1_SPI_FUNCTION_NUMBER & cmd->resp[0])
                resp = -5; /* I/O error */
            /* else R1_SPI_IDLE, "it's resetting" */
        }
        if (*cp != 0xff)
        {
            resp = sd_wait_unbusy(priv, 2048);
        }
    }

    return resp;
}

static int sd_go_idle_state(void *priv)
{
    int resp;
    SDIO_CMD cmd0 = {0, 0x00, 0x00, 0x00, 0x00};

    cmd0.flags |= RSP_SPI_R1;
    memset(sd_mgr.status, 0xff, sizeof(sd_mgr.status));
    spidrv_write(priv, sd_mgr.status, 10, DMA_TRANS);
    spidrv_cs(priv, CS_ENABLE);
    resp = sd_send_command(priv, &cmd0);
    spidrv_cs(priv, CS_DISABLE);

    return resp;
}

static int sd_io_send_op_cond(void *priv)
{
    int resp;
    SDIO_CMD cmd5 = {5, 0x00, 0x00, 0x00, 0x00};

    cmd5.flags |= RSP_SPI_R4;
    spidrv_cs(priv, CS_ENABLE);
    resp = sd_send_command(priv, &cmd5);
    spidrv_cs(priv, CS_DISABLE);
    if (resp == 0)
    {
        memcpy(&cmd5.arg1, cmd5.resp, 4);
        spidrv_cs(priv, CS_ENABLE);
        resp = sd_send_command(priv, &cmd5);
        spidrv_cs(priv, CS_DISABLE);
    }

    return resp;
}

static int sd_io_rw_direct(void *priv, SDIO_CMD *cmd52, unsigned char *r5)
{
    int resp;

    spidrv_cs(priv, CS_ENABLE);
    resp = sd_send_command(priv, cmd52);
    if (resp == 0)
    {
        *r5 = cmd52->resp[0];
    }
    spidrv_cs(priv, CS_DISABLE);

    return resp;
}

static int sd_io_rw_extended(void *priv, SDIO_CMD *cmd53, unsigned char *data)
{
    int resp;
    unsigned char temp;
    char rw_flag = (cmd53->arg1 & 0x80) ? 1 : 0;    // 1 for write, 0 for read
    char block_flag = (cmd53->arg1 & 0x08) ? 1 : 0; // 1 for block, 0 for byte
    unsigned int len = ((cmd53->arg3 << 8) | cmd53->arg4) & 0x1ff;
    len = (len == 0) ? 512 : len;

    spidrv_cs(priv, CS_ENABLE);
    if (!rw_flag)
        cmd53->flags |= (RSP_SPI_R5 | CMD_READ);
    resp = sd_send_command(priv, cmd53);
    if (resp)
    {
        spidrv_cs(priv, CS_DISABLE);
        return resp;
    }

    if (rw_flag)
    {
        if (block_flag)
        { // ---------------------------------blocks write
            cmd53->data_token = 0xfc;
            while (len--)
            {
                sd_setup_data_message(priv, cmd53, data, SDIO_BLOCK_SIZE);
                data += SDIO_BLOCK_SIZE;
            }
            temp = 0xfd;
            spidrv_write(priv, &temp, 1, CPU_TRANS); // stop byte
            sd_wait_unbusy(priv, 1);
        }
        else
        { // ------------------------------------------bytes write
            cmd53->data_token = 0xfe;
            sd_setup_data_message(priv, cmd53, data, len);
        }
    }
    else
    {
        cmd53->data_token = 0xfe;
        if (block_flag)
        { // ---------------------------------blocks read
            while (len--)
            {
                sd_setup_data_message(priv, cmd53, data, SDIO_BLOCK_SIZE);
                data += SDIO_BLOCK_SIZE; // but reading process still going on
            }
        }
        else
        { // ------------------------------------------bytes read
            sd_setup_data_message(priv, cmd53, data, len);
        }
        temp = 0xff;
        spidrv_write(priv, &temp, 1, CPU_TRANS);
    }
    spidrv_cs(priv, CS_DISABLE);
    return resp;
}

static int sd_write_byte(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char value)
{
    unsigned char r5 = 0;
    SDIO_CMD cmd52 = {52, (0x80 | func << 4 | addr >> 15), addr >> 7, addr << 1, value};
    cmd52.flags |= RSP_SPI_R5;
    return sd_io_rw_direct(priv, &cmd52, &r5);
}

static int sd_read_byte(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char *data)
{
    SDIO_CMD cmd52 = {52, (0x00 | func << 4 | addr >> 15), addr >> 7, addr << 1, 0x00};
    cmd52.flags |= RSP_SPI_R5;
    return sd_io_rw_direct(priv, &cmd52, data);
}

static int sd_write_read_byte(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char value, unsigned char *data)
{
    SDIO_CMD cmd52 = {52, (0x88 | func << 4 | addr >> 15), addr >> 7, addr << 1, value};
    cmd52.flags |= RSP_SPI_R5;
    return sd_io_rw_direct(priv, &cmd52, data);
}

static int sd_write_bytes(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char *data, unsigned int len)
{
    len = (len == 512) ? 0 : len;
    SDIO_CMD cmd53 = {53, (0x84 | func << 4 | addr >> 15), addr >> 7, (addr << 1 | len >> 8), len};
    return sd_io_rw_extended(priv, &cmd53, data);
}

static int sd_read_bytes(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char *data, unsigned int len)
{
    len = (len == 512) ? 0 : len;
    SDIO_CMD cmd53 = {53, (0x04 | func << 4 | addr >> 15), addr >> 7, (addr << 1 | len >> 8), len};
    return sd_io_rw_extended(priv, &cmd53, data);
}

static int sd_write_blocks(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char *data, unsigned int len)
{
    unsigned int block = (len + SDIO_BLOCK_SIZE - 1) / SDIO_BLOCK_SIZE;
    block = (block >= 512) ? 0 : block;
    SDIO_CMD cmd53 = {53, (0x8c | func << 4 | addr >> 15), addr >> 7, (addr << 1 | block >> 8), block};
    return sd_io_rw_extended(priv, &cmd53, data);
}

static int sd_read_blocks(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char *data, unsigned int len)
{
    unsigned int block = (len + SDIO_BLOCK_SIZE - 1) / SDIO_BLOCK_SIZE;
    block = (block >= 512) ? 0 : block;
    SDIO_CMD cmd53 = {53, (0x0c | func << 4 | addr >> 15), addr >> 7, (addr << 1 | block >> 8), block};
    return sd_io_rw_extended(priv, &cmd53, data);
}

static int sd_get_cistpl_info(void *priv, unsigned char code, unsigned char *buff)
{
    int i;
    unsigned int cis_ptr = 0;
    char tpl_state = 0;
    char found = 0;
    unsigned char tpl_code;
    unsigned char tpl_link;
    unsigned char tpl_body;

    for (i = 0; i < 3; ++i)
    {
        unsigned char temp = 0;
        if (sd_read_byte(priv, FUNCTION_0, COM_CIS_POINTER + i, &temp) < 0)
        {
            return RET_ERR;
        }
        cis_ptr |= (temp << (8 * i));
    }

    do
    {
        switch (tpl_state)
        {
        case 0: // tuple code
            if (sd_read_byte(priv, FUNCTION_0, cis_ptr++, &tpl_code) < 0)
            {
                return RET_ERR;
            }
            if (tpl_code == CISTPL_END)
            { //
                return RET_ERR;
            }
            else if (tpl_code == CISTPL_NULL)
            {
                return RET_ERR;
            }
            else
            {
                found = (tpl_code == code) ? 1 : 0;
                tpl_state = 1;
            }
            break;
        case 1: // tuple link
            if (sd_read_byte(priv, FUNCTION_0, cis_ptr++, &tpl_link) < 0)
            {
                return RET_ERR;
            }
            tpl_state = 2;
            break;
        case 2: // tuple body
            for (i = 0; i < tpl_link; ++i)
            {
                if (sd_read_byte(priv, FUNCTION_0, cis_ptr++, &tpl_body) < 0)
                {
                    return RET_ERR;
                }
                if (found == 1)
                {
                    buff[i] = tpl_body;
                    tpl_state = 3; // found it!
                }
                else
                {
                    tpl_state = 0;
                }
            }
            break;
        default:
            return RET_ERR;
        }
    } while (!(found == 1 && tpl_state == 3));

    return RET_OK;
}

static int sd_try_stop(void *priv)
{
    unsigned char temp;
    // retry stop Host to Slave SPI block write
    spidrv_cs(priv, CS_ENABLE);
    temp = 0xfd; // end byte
    spidrv_write(priv, &temp, 1, CPU_TRANS);
    sd_wait_unbusy(priv, 1);
    spidrv_cs(priv, CS_DISABLE);

    // retry stop other command
    return sd_write_byte(priv, FUNCTION_0, IO_ABORT, 0x1);
}

int hgic_sdspi_init(void *priv)
{
    int resp = 0;
    unsigned char data[4] = {0};

    // try stop previous cmd
    sd_try_stop(priv);
    // reset SDIO and sned cmd0
    sd_write_byte(priv, FUNCTION_0, IO_ABORT, 0x08);
    if (sd_go_idle_state(priv) < 0)
    {
        printf("Reset error. Try reset IO\r\n");
        sd_write_byte(priv, FUNCTION_0, IO_ABORT, 0x08);
#ifdef HW_CRC_ON
        if (sd_crc_on_off(priv, &cmd59) == RET_OK)
        {
            __crc_en__ = 1;
            //printf("CRC on\r\n");
        }
#endif
    }

    // test io
    if (sd_io_send_op_cond(priv) < 0)
    {
        printf("Illegal command\r\n");
        return RET_ERR;
    }

    // function_1 enable
    resp = sd_write_read_byte(priv, FUNCTION_0, IO_ENABLE, 0x02, data);
    if (resp == RET_OK && (data[0] & 0x2) == 0x2)
    {
        printf("Function 1 enabled\r\n");
    }
    // interrupt_1 enable
    resp = sd_write_read_byte(priv, FUNCTION_0, INT_ENABLE, 0x03, data);
    if (resp == RET_OK && (data[0] & 0x3) == 0x3)
    {
        printf("Interrupt 1 enabled\r\n");
    }
    // check support and set high-speed
    resp = sd_write_read_byte(priv, FUNCTION_0, HIGH_SPEED, 0x02, data);
    if (resp == RET_OK && (data[0] & 0x3) == 0x3)
    {
        printf("Switch to high speed mode\r\n");
    }
    else
    {
        printf("Not support high speed mode\r\n");
    }
    // check support spi interrupt
    resp = sd_write_read_byte(priv, FUNCTION_0, BUS_IF_CTRL, 0x20, data);
    if (resp == RET_OK && (data[0] & 0x60) == 0x60)
    {
        printf("Continuous SPI interrupt enabled\r\n");
    }
    else
    {
        printf("SPI interrupt only assert when the CS line is asserted\r\n");
    }
    // set function_1 block size
    sd_write_byte(priv, FUNCTION_0, FUNC1_IO_BLOCK_SIZE, SDIO_BLOCK_SIZE);
    sd_write_byte(priv, FUNCTION_0, FUNC1_IO_BLOCK_SIZE + 1, (SDIO_BLOCK_SIZE >> 8));

    return RET_OK;
}

int hgic_sdspi_detect_alive(void *priv)
{
    unsigned char data;
    return sd_read_byte(priv, FUNCTION_0, CCCR_SDIO_VER, &data);
}

int hgic_sdspi_abort(void *priv)
{
    return sd_write_byte(priv, FUNCTION_0, IO_ABORT, 0x1); // send abort cmd
}

int hgic_sdspi_get_devid(void *priv, unsigned short *vendor_id, unsigned short *devid)
{
    unsigned char manfid[4] = {0};
    if (sd_get_cistpl_info(priv, CISTPL_MANFID, manfid) < 0)
    {
        return RET_ERR;
    }
    memcpy(vendor_id, manfid, 2);
    memcpy(devid, manfid + 2, 2);
    return RET_OK;
}

int hgic_sdspi_data_ready(void *priv)
{
    int resp = 0;
    unsigned char data = 0;

    resp = sd_read_byte(priv, FUNCTION_0, INT_PENDING, &data);
    if (resp == RET_OK && (data & 0x02))
    {
        resp = sd_read_byte(priv, FUNCTION_1, SLAVE_INTR_ID, &data);
        if (resp == RET_OK && (data & 0x01))
        {
            return 1;
        }
    }
    return resp;
}

unsigned int hgic_sdspi_get_datalen(void *priv, int flags)
{
    int resp = 0;
    unsigned char data = 0;
    unsigned int data_len = 0;
    unsigned int addr = (flags & FLAGS_INBOOT) ? SLAVE_AHB_TRANS_CNT_BL : SLAVE_AHB_TRANS_CNT;

    resp = sd_read_byte(priv, FUNCTION_1, addr, &data);
    if (resp == RET_OK) {
        data_len = data;
        resp = sd_read_byte(priv, FUNCTION_1, addr+1, &data);
        if (resp == RET_OK) {
            data_len |= data << 8;
        } else {
            data_len = 0xffffffff;
        }
    }
    if ((flags & FLAGS_INBOOT) && data_len == 0)  {
        data_len = 16;
    }
    return data_len;
}

int hgic_sdspi_readdata(void *priv, unsigned char *buf, unsigned int len)
{
    int resp = 0;

    if (len < SDIO_BLOCK_SIZE)
    {
        resp = sd_read_bytes(priv, FUNCTION_1, 0, buf, len);
    }
    else
    {
        resp = sd_read_blocks(priv, FUNCTION_1, 0, buf, len);
    }

    if (resp < 0)
    {
        hgic_sdspi_abort(priv);
        hgic_sdspi_init(priv);
        return RET_ERR;
    }
    else
    {
        return RET_OK;
    }
}

int hgic_sdspi_read(void *priv, unsigned char *buf, unsigned int len, int flags)
{
    int resp = 0;
    unsigned int data_len = 0;

    resp = hgic_sdspi_data_ready(priv);
    if (resp < 0)
    {
        hgic_sdspi_abort(priv);
        hgic_sdspi_init(priv);
        return -1;
    }
    else if (resp > 0)
    {
        data_len = hgic_sdspi_get_datalen(priv, flags);
        if (data_len == 0xffffffff)
        {
            hgic_sdspi_abort(priv);
            hgic_sdspi_init(priv);
            return 0;
        }

        if (data_len > len)
        {
            hgic_sdspi_abort(priv);
            return -1;
        }

        resp = hgic_sdspi_readdata(priv, buf, data_len);
        if (resp < 0)
        {
            hgic_sdspi_abort(priv);
            hgic_sdspi_init(priv);
            return -1;
        }
    }
    return data_len;
}

int hgic_sdspi_write(void *priv, unsigned char *data, unsigned int len)
{
    int resp;
    unsigned int write_len;
    unsigned char temp;

    if (data == NULL || len == 0)
    {
        return RET_ERR;
    }
    if (len > 1664)
    {
        resp = sd_write_blocks(priv, FUNCTION_1, 0, data, 1664);
        write_len = 1664;
    }
    else
    {
        resp = sd_write_blocks(priv, FUNCTION_1, 0, data, len);
        write_len = len;
    }
    if (resp < 0)
    {
        spidrv_cs(priv, CS_ENABLE); //retry stop Host to Slave SPI block write
        temp = 0xfd;                // end byte
        spidrv_write(priv, &temp, 1, CPU_TRANS);
        sd_wait_unbusy(priv, 1);
        spidrv_cs(priv, CS_DISABLE);
        hgic_sdspi_init(priv);
        return RET_ERR;
    }
    else
    {
        return write_len;
    }
}
