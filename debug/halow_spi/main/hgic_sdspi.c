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
typedef struct sdio_cmd_format {
    unsigned char cmd;
    unsigned char arg1;
    unsigned char arg2;
    unsigned char arg3;
    unsigned char arg4;
    unsigned char crc; // if use hardware crc, set 0 default. Not zero data will send the value as crc
    unsigned char exp_resp;
} SDIO_CMD;

/* SDIO Function enum */
typedef enum {
    FUNCTION_0,
    FUNCTION_1,
    FUNCTION_2,
    FUNCTION_3,
    FUNCTION_4,
    FUNCTION_5,
    FUNCTION_6,
    FUNCTION_7,
} SDIO_FUNC;

#define RET_OK                                  (0)
#define RET_ERR                                 (-1)

#define CMD_CRC                                 (0)
#define DATA_CRC                                (1)

#define CPU_TRANS                               (0)
#define DMA_TRANS                               (1)

#define CS_DISABLE                              (0)
#define CS_ENABLE                               (1)

#define FLAGS_INBOOT                            (1 << 0)

#define SDIO_RESP_RETRY_CNT                     (8)
#define SDIO_BUSY_RETRY_CNT                     (2048)
#define SDIO_BLOCK_SIZE                         (64)

/* SDIO CIA registers */
#define CCCR_SDIO_VER                           (0x00)
#define SD_SPEC_VER                             (0x01)
#define IO_ENABLE                               (0x02)
#define IO_READY                                (0x03)
#define INT_ENABLE                              (0x04)
#define INT_PENDING                             (0x05)
#define IO_ABORT                                (0x06)
#define BUS_IF_CTRL                             (0x07)
#define CARD_CAPABILITY                         (0x08)
#define COM_CIS_POINTER                         (0x09)
#define BUS_SUSPEND                             (0x0C)
#define FUNCTION_SELECT                         (0x0D)
#define EXCE_FLAGS                              (0x0E)
#define READY_FLAGS                             (0x0F)
#define FUNC0_IO_BLOCK_SIZE                     (0x10)
#define POWER_CONTROL                           (0x12)
#define HIGH_SPEED                              (0x13)

#define FUNC1_IO_BLOCK_SIZE                     (0x110)

/* SDIO slave function_1 register */
#define SLAVE_DATA_PORT                         (0x00)
#define SLAVE_INTR_ID                           (0x04)
#define SLAVE_CLK_WKUP                          (0x2C)
#define SLAVE_AHB_TRANS_CNT_BL                  (0x1C)
#define SLAVE_AHB_TRANS_CNT                     (0x49)

/* SDIO CIS infomation */
#define CISTPL_NULL                             (0x00)
#define CISTPL_CHECKSUM                         (0x10)
#define CISTPL_VERS_1                           (0x15)
#define CISTPL_ALTSTR                           (0x16)
#define CISTPL_MANFID                           (0x20)
#define CISTPL_FUNCID                           (0x21)
#define CISTPL_FUNCE                            (0x22)
#define CISTPL_SDIO_STD                         (0x91)
#define CISTPL_SDIO_EXT                         (0x92)
#define CISTPL_END                              (0xff)

const SDIO_CMD cmd0  = {0, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x01};
const SDIO_CMD cmd5  = {5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
const SDIO_CMD cmd59 = {59, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01};

static char __crc_en__ = 0;

static void sd_send_cmd(void *priv, const SDIO_CMD *cmd)
{
    int crc;
    unsigned char temp;

    temp = cmd->cmd | 0x40;
    spidrv_write(priv, &temp, 1, CPU_TRANS); // start bit and dir bit
    spidrv_write(priv, (unsigned char *)&cmd->arg1, 4, CPU_TRANS);
    if (cmd->crc) {
        temp = (cmd->crc << 1) | 0x01;
    } else {
        crc = spidrv_hw_crc(priv, (unsigned char *)cmd, 5, CMD_CRC);
        temp = (crc << 1) | 0x01;
    }
    spidrv_write(priv, &temp, 1, CPU_TRANS); // end bit
}

static int sd_wait_r1(void *priv, char exp_resp)
{
    char retry = SDIO_RESP_RETRY_CNT;
    unsigned char r1 = 0xff;
    do {
        spidrv_read(priv, &r1, 1, CPU_TRANS);
    } while (retry-- && r1 != exp_resp);
    return (r1 == exp_resp) ? RET_OK : RET_ERR;
}

static int sd_reset(void *priv, const SDIO_CMD *cmd0)
{
    int resp;
    unsigned char temp = 0xff;
    char i = 0;

    for (i = 0; i < 10; ++i) {
        spidrv_write(priv, &temp, 1, CPU_TRANS);
    }
    spidrv_cs(priv, CS_ENABLE);
    sd_send_cmd(priv, cmd0);
    resp = sd_wait_r1(priv, cmd0->exp_resp);
    spidrv_cs(priv, CS_DISABLE);
    spidrv_write(priv, &temp, 1, CPU_TRANS);
    return resp;
}

static int sd_io_send_op_cond(void *priv, const SDIO_CMD *cmd5, unsigned char *r4)
{
    int resp;
    unsigned char temp = 0xff;

    spidrv_cs(priv, CS_ENABLE);
    sd_send_cmd(priv, cmd5);
    resp = sd_wait_r1(priv, cmd5->exp_resp);
    if (resp == RET_OK) {
        spidrv_read(priv, r4, 4, CPU_TRANS);
    }
    spidrv_cs(priv, CS_DISABLE);
    spidrv_write(priv, &temp, 1, CPU_TRANS);
    return resp;
}

static int sd_crc_on(void *priv, const SDIO_CMD *cmd59)
{
    int resp;
    unsigned char temp = 0xff;

    spidrv_cs(priv, CS_ENABLE);
    sd_send_cmd(priv, cmd59);
    resp = sd_wait_r1(priv, cmd59->exp_resp);
    spidrv_cs(priv, CS_DISABLE);
    spidrv_write(priv, &temp, 1, CPU_TRANS);

    return resp;
}

static int sd_io_rw_direct(void *priv, const SDIO_CMD *cmd52, unsigned char *r5)
{
    int resp;
    unsigned char temp = 0xff;

    *r5 = 0;
    spidrv_cs(priv, CS_ENABLE);
    sd_send_cmd(priv, cmd52);
    resp = sd_wait_r1(priv, cmd52->exp_resp);
    if (resp == RET_OK) {
        spidrv_read(priv, r5, 1, CPU_TRANS); // respone 1 byte in SPI mode
    }
    spidrv_cs(priv, CS_DISABLE);
    spidrv_write(priv, &temp, 1, CPU_TRANS);
    return resp;
}

static int sd_io_rw_extended(void *priv, const SDIO_CMD *cmd53, unsigned char *data)
{
    int resp;
    unsigned int crc;
    unsigned int read_crc;
    int retry;
    int err = 0;
    unsigned char temp;
    unsigned char r1 = 0;
    char rw_flag = (cmd53->arg1 & 0x80) ? 1 : 0; // 1 for write, 0 for read
    char block_flag = (cmd53->arg1 & 0x08) ? 1 : 0; // 1 for block, 0 for byte
    unsigned int len = ((cmd53->arg3 << 8) | cmd53->arg4) & 0x1ff;
    len = (len == 0) ? 512 : len;

    spidrv_cs(priv, CS_ENABLE);
    sd_send_cmd(priv, cmd53);
    resp = sd_wait_r1(priv, cmd53->exp_resp);
    if (resp == RET_OK) {
        spidrv_read(priv, &temp, 1, CPU_TRANS); // r5 read as 0x00, don't care
        if (rw_flag) {
            if (block_flag) { // ---------------------------------blocks write
                while (len--) {
                    temp = 0xff;
                    spidrv_write(priv, &temp, 1, CPU_TRANS); // dummy
                    temp = 0xfc;
                    spidrv_write(priv, &temp, 1, CPU_TRANS); // start byte
                    spidrv_write(priv, data, SDIO_BLOCK_SIZE, DMA_TRANS);
                    crc = spidrv_hw_crc(priv, data, SDIO_BLOCK_SIZE, DATA_CRC);
                    spidrv_write(priv, (unsigned char *)&crc, 2, CPU_TRANS); // crc 16 bit
                    data += SDIO_BLOCK_SIZE;
                    retry = SDIO_RESP_RETRY_CNT;
                    do {
                        spidrv_read(priv, &r1, 1, CPU_TRANS);
                    } while (retry-- && ((r1 & 0x1f) != 0x05)); // crc status
                    err += ((r1 & 0x1f) != 0x05) ? 1 : 0;
                    retry = SDIO_BUSY_RETRY_CNT;
                    do {
                        spidrv_read(priv, &r1, 1, CPU_TRANS);
                    } while (retry-- && r1 != 0xff); // wait busy
                    err += (r1 != 0xff) ? 1 : 0;
                }
                temp = 0xfd;
                spidrv_write(priv, &temp, 1, CPU_TRANS); // stop byte
                retry = SDIO_RESP_RETRY_CNT;
                do {
                    spidrv_read(priv, &r1, 1, CPU_TRANS);
                } while (retry-- && r1 == 0xff); // wait busy
                err += (r1 == 0xff) ? 1 : 0;
                retry = SDIO_RESP_RETRY_CNT;
                do {
                    spidrv_read(priv, &r1, 1, CPU_TRANS);
                } while (retry-- && r1 != 0xff); // wait busy
                err += (r1 != 0xff) ? 1 : 0;
            } else { // ------------------------------------------bytes write
                temp = 0xff;
                spidrv_write(priv, &temp, 1, CPU_TRANS); // dummy
                temp = 0xfe;
                spidrv_write(priv, &temp, 1, CPU_TRANS); // start byte
                spidrv_write(priv, data, len, DMA_TRANS);
                crc = spidrv_hw_crc(priv, data, len, DATA_CRC);
                spidrv_write(priv, (unsigned char *)&crc, 2, CPU_TRANS); // crc 16 bit
                retry = SDIO_RESP_RETRY_CNT;
                do {
                    spidrv_read(priv, &r1, 1, CPU_TRANS);
                } while (retry-- && ((r1 & 0x1f) != 0x05)); // crc status
                err += ((r1 & 0x1f) != 0x05) ? 1 : 0;
                retry = SDIO_BUSY_RETRY_CNT;
                do {
                    spidrv_read(priv, &r1, 1, CPU_TRANS);
                } while (retry-- && r1 != 0xff); // wait busy
                err += (r1 != 0xff) ? 1 : 0;
            }
        } else {
            if (block_flag) { // ---------------------------------blocks read
                while (len--) {
                    retry = SDIO_RESP_RETRY_CNT;
                    do {
                        spidrv_read(priv, &r1, 1, CPU_TRANS);
                    } while (retry-- && r1 != 0xfe);
                    err += (r1 != 0xfe) ? 1 : 0;
                    spidrv_read(priv, data, SDIO_BLOCK_SIZE, DMA_TRANS);
                    spidrv_read(priv, (unsigned char *)&read_crc, 2, CPU_TRANS); // crc 16 bit
                    crc = spidrv_hw_crc(priv, data, SDIO_BLOCK_SIZE, DATA_CRC);
                    if (__crc_en__ && crc != read_crc) {
                        err++; // mark error to notify upper layer to drop this data
                    }
                    data += SDIO_BLOCK_SIZE; // but reading process still going on
                }
            } else { // ------------------------------------------bytes read
                retry = SDIO_RESP_RETRY_CNT;
                do {
                    spidrv_read(priv, &r1, 1, CPU_TRANS);
                } while (retry-- && r1 != 0xfe);
                err += (r1 != 0xfe) ? 1 : 0;
                spidrv_read(priv, data, len, DMA_TRANS);
                spidrv_read(priv, (unsigned char *)&read_crc, 2, CPU_TRANS); // crc 16 bit
                crc = spidrv_hw_crc(priv, data, SDIO_BLOCK_SIZE, DATA_CRC);
                if (__crc_en__ && crc != read_crc) {
                    err++; // mark error to notify upper layer to drop this data
                }
            }
        }
    }
    spidrv_cs(priv, CS_DISABLE);
    temp = 0xff;
    spidrv_write(priv, &temp, 1, CPU_TRANS);
    if (err) { resp = RET_ERR; }
    return resp;
}

static int sd_write_byte(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char value)
{
    unsigned char r5 = 0;
    SDIO_CMD cmd52 = {52, (0x80 | func << 4 | addr >> 15), addr >> 7, addr << 1, value, 0x00, 0x00};
    return sd_io_rw_direct(priv, &cmd52, &r5);
}

static int sd_read_byte(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char *data)
{
    SDIO_CMD cmd52 = {52, (0x00 | func << 4 | addr >> 15), addr >> 7, addr << 1, 0x00, 0x00, 0x00};
    return sd_io_rw_direct(priv, &cmd52, data);
}

static int sd_write_read_byte(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char value, unsigned char *data)
{
    SDIO_CMD cmd52 = {52, (0x88 | func << 4 | addr >> 15), addr >> 7, addr << 1, value, 0x00, 0x00};
    return sd_io_rw_direct(priv, &cmd52, data);
}

static int sd_write_bytes(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char *data, unsigned int len)
{
    len = (len == 512) ? 0 : len;
    SDIO_CMD cmd53 = {53, (0x84 | func << 4 | addr >> 15), addr >> 7, (addr << 1 | len >> 8), len, 0x00, 0x00};
    return sd_io_rw_extended(priv, &cmd53, data);
}

static int sd_read_bytes(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char *data, unsigned int len)
{
    len = (len == 512) ? 0 : len;
    SDIO_CMD cmd53 = {53, (0x04 | func << 4 | addr >> 15), addr >> 7, (addr << 1 | len >> 8), len, 0x00, 0x00};
    return sd_io_rw_extended(priv, &cmd53, data);
}

static int sd_write_blocks(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char *data, unsigned int len)
{
    unsigned int block = (len + SDIO_BLOCK_SIZE - 1) / SDIO_BLOCK_SIZE;
    block = (block >= 512) ? 0 : block;
    SDIO_CMD cmd53 = {53, (0x8c | func << 4 | addr >> 15), addr >> 7, (addr << 1 | block >> 8), block, 0x00, 0x00};
    return sd_io_rw_extended(priv, &cmd53, data);
}

static int sd_read_blocks(void *priv, SDIO_FUNC func, unsigned int addr, unsigned char *data, unsigned int len)
{
    unsigned int block = (len + SDIO_BLOCK_SIZE - 1) / SDIO_BLOCK_SIZE;
    block = (block >= 512) ? 0 : block;
    SDIO_CMD cmd53 = {53, (0x0c | func << 4 | addr >> 15), addr >> 7, (addr << 1 | block >> 8), block, 0x00, 0x00};
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

    for (i = 0; i < 3; ++i) {
        unsigned char temp = 0;
        if (sd_read_byte(priv, FUNCTION_0, COM_CIS_POINTER + i, &temp) == RET_ERR) {
            return RET_ERR;
        }
        cis_ptr |= (temp << (8 * i));
    }

    do {
        switch (tpl_state) {
            case 0: // tuple code
                if (sd_read_byte(priv, FUNCTION_0, cis_ptr++, &tpl_code) == RET_ERR) {
                    return RET_ERR;
                }
                if (tpl_code == CISTPL_END) { //
                    return RET_ERR;
                } else if (tpl_code == CISTPL_NULL) {
                    return RET_ERR;
                } else {
                    found = (tpl_code == code) ? 1 : 0;
                    tpl_state = 1;
                }
                break;
            case 1: // tuple link
                if (sd_read_byte(priv, FUNCTION_0, cis_ptr++, &tpl_link) == RET_ERR) {
                    return RET_ERR;
                }
                tpl_state = 2;
                break;
            case 2: // tuple body
                for (i = 0; i < tpl_link; ++i) {
                    if (sd_read_byte(priv, FUNCTION_0, cis_ptr++, &tpl_body) == RET_ERR) {
                        return RET_ERR;
                    }
                    if (found == 1) {
                        buff[i] = tpl_body;
                        tpl_state = 3; // found it!
                    } else {
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
    int i = 0;
    // retry stop Host to Slave SPI block write
    spidrv_cs(priv, CS_ENABLE);
    temp = 0xfd; // end byte
    spidrv_write(priv, &temp, 1, CPU_TRANS);
    for (i = 0; i < 16; ++i) {
        spidrv_read(priv, &temp, 1, CPU_TRANS); // don't care
    }
    spidrv_cs(priv, CS_DISABLE);
    temp = 0xff;
    spidrv_write(priv, &temp, 1, CPU_TRANS);

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
    if (sd_reset(priv, &cmd0) == RET_ERR) {
        //printf("Reset error. Try reset IO\r\n");
        sd_write_byte(priv, FUNCTION_0, IO_ABORT, 0x08);
#ifdef HW_CRC_ON
        if (sd_crc_on_off(priv, &cmd59) == RET_OK) {
            __crc_en__ = 1;
            //printf("CRC on\r\n");
        }
#endif
    }

    // test io
    if (sd_io_send_op_cond(priv, &cmd5, data) == RET_ERR) {
        //printf("Illegal command\r\n");
        return RET_ERR;
    } else {
        // NF>0 && OCR vaild
        if ((data[0] & 0x70) && (data[1] || data[2] || data[3])) {
            SDIO_CMD cmd5_copy;
            memcpy(&cmd5_copy, &cmd5, sizeof(SDIO_CMD));
            memcpy(&cmd5_copy.arg2, data, 3); // re-write support voltage range into OCR
            cmd5_copy.crc = spidrv_hw_crc(priv, (unsigned char *)&cmd5_copy, 5, CMD_CRC);
            cmd5_copy.exp_resp = 0x00;
            sd_io_send_op_cond(priv, &cmd5_copy, data); // The last command succeeds, and so does the default command
            if (data[0] & 0x80) {
                //printf("IO initialized\r\n");
            } else {
                //printf("IO initialize timeout\r\n");
                return RET_ERR;
            }
        } else {
            //printf("Get IO OCR error\r\n");
            return RET_ERR;
        }
    }

    // function_1 enable
    resp = sd_write_read_byte(priv, FUNCTION_0, IO_ENABLE, 0x02, data);
    if (resp == RET_OK && (data[0] & 0x2) == 0x2) {
        //printf("Function 1 enabled\r\n");
    }
    // interrupt_1 enable
    resp = sd_write_read_byte(priv, FUNCTION_0, INT_ENABLE, 0x03, data);
    if (resp == RET_OK && (data[0] & 0x3) == 0x3) {
        //printf("Interrupt 1 enabled\r\n");
    }
    // check support and set high-speed
    resp = sd_write_read_byte(priv, FUNCTION_0, HIGH_SPEED, 0x02, data);
    if (resp == RET_OK && (data[0] & 0x3) == 0x3) {
        //printf("Switch to high speed mode\r\n");
    } else {
        //printf("Not support high speed mode\r\n");
    }
    // check support spi interrupt
    resp = sd_write_read_byte(priv, FUNCTION_0, BUS_IF_CTRL, 0x20, data);
    if (resp == RET_OK && (data[0] & 0x60) == 0x60) {
        //printf("Continuous SPI interrupt enabled\r\n");
    } else {
        //printf("SPI interrupt only assert when the CS line is asserted\r\n");
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
    if (sd_get_cistpl_info(priv, CISTPL_MANFID, manfid) == RET_ERR) {
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
    if (resp == RET_OK && (data & 0x02)) {
        resp = sd_read_byte(priv, FUNCTION_1, SLAVE_INTR_ID, &data);
        if (resp == RET_OK && (data & 0x01)) {
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

    if (len < SDIO_BLOCK_SIZE) {
        resp = sd_read_bytes(priv, FUNCTION_1, 0, buf, len);
    } else {
        resp = sd_read_blocks(priv, FUNCTION_1, 0, buf, len);
    }

    if (resp == RET_ERR) {
        hgic_sdspi_abort(priv);
        hgic_sdspi_init(priv);
        return RET_ERR;
    } else {
        return RET_OK;
    }
}

int hgic_sdspi_read(void *priv, unsigned char *buf, unsigned int len, int flags)
{
    int resp = 0;
    unsigned int data_len = 0;

    resp = hgic_sdspi_data_ready(priv);
    if (resp == RET_ERR) {
        hgic_sdspi_abort(priv);
        hgic_sdspi_init(priv);
        return -1;
    } else if (resp > 0) {
        data_len = hgic_sdspi_get_datalen(priv, flags);
        if (data_len == 0xffffffff) {
            hgic_sdspi_abort(priv);
            hgic_sdspi_init(priv);
            return 0;
        }

        if (data_len > len) {
            hgic_sdspi_abort(priv);
            return -1;
        }

        resp = hgic_sdspi_readdata(priv, buf, data_len);
        if (resp == RET_ERR) {
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
    int i;

    if (data == NULL || len == 0) {
        return RET_ERR;
    }
    if (len > 1664) {
        resp = sd_write_blocks(priv, FUNCTION_1, 0, data, 1664);
        write_len = 1664;
    } else {
        resp = sd_write_blocks(priv, FUNCTION_1, 0, data, len);
        write_len = len;
    }
    if (resp == RET_ERR) {
        unsigned char temp;
        spidrv_cs(priv, CS_ENABLE); //retry stop Host to Slave SPI block write
        temp = 0xfd; // end byte
        spidrv_write(priv, &temp, 1, CPU_TRANS);
        for (i = 0; i < 16; ++i) {
            spidrv_read(priv, &temp, 1, CPU_TRANS); // don't care
        }
        spidrv_cs(priv, CS_DISABLE);
        temp = 0xff;
        spidrv_write(priv, &temp, 1, CPU_TRANS);
        hgic_sdspi_init(priv);
        return RET_ERR;
    } else {
        return write_len;
    }
}

