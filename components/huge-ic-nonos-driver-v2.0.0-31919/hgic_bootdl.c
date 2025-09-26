#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "hgic_raw.h"

#define HGIC_FWINFO_BOOT_HDR 0x5A69
#define BOOT_CMD_KEY         "@huge-ic"
#define BOOT_CMD_KEY_SIZE    8

static int hgic_bootdl_send_cmd(unsigned char cmd, unsigned int write_addr, unsigned int data_len)
{
    struct hgic_bootdl_cmd_hdr *hdr = (struct hgic_bootdl_cmd_hdr *)hgic.tx_buf;

    hdr->hdr.magic  = HGIC_HDR_TX_MAGIC;
    hdr->hdr.type   = HGIC_HDR_TYPE_BOOTDL;
    hdr->hdr.length = sizeof(struct hgic_bootdl_cmd_hdr);
    hdr->hdr.cookie = 0;

    hdr->cmd = cmd;
    hdr->cmd_len = 12;
    hdr->cmd_flag = 0xff; // off
    if (cmd == HGIC_BOOTDL_CMD_ENTER) {
        // strncpy((char *)hdr->addr, BOOT_CMD_KEY, BOOT_CMD_KEY_SIZE);
        printf("error hdr->addr\n");
    } else {
        put_unaligned_le32(write_addr, hdr->addr);
        put_unaligned_le32(data_len, hdr->len);
    }
    hdr->check = 0;

    return raw_send(hgic.tx_buf, sizeof(struct hgic_bootdl_cmd_hdr));
}

int hgic_bootdl_parse_fw(unsigned char *fw_data, struct hgic_bootdl *bootdl)
{
    struct hgic_fw_info_hdr *fw_hdr = (struct hgic_fw_info_hdr *)fw_data;

    if (fw_data == NULL) {
        printf("FW data null!\r\n");
        return -1;
    }
    if (fw_hdr->boot.boot_flag != HGIC_FWINFO_BOOT_HDR) {
        printf("Can not find boot header!\r\n");
        return -1;
    }
    bootdl->fw_info.boot_addr = fw_hdr->boot.boot_to_sram_addr;
    bootdl->fw_info.hdr_len = fw_hdr->boot.boot_code_offset_addr;
    bootdl->fw_info.fw_size = fw_hdr->boot.boot_from_flash_len;
    bootdl->fw_info.aes_en = fw_hdr->boot.mode.aes_en;
    bootdl->fw_info.crc_en = fw_hdr->boot.mode.crc_en;
    bootdl->offset = 0;
    printf("firmware version  : v%d\r\n", fw_hdr->fw_infor.svn_version);
    printf("firmware hdr len  : %d\r\n", bootdl->fw_info.hdr_len);
    printf("firmware run addr : 0x%x\r\n", bootdl->fw_info.boot_addr);
    printf("firmware size     : %d\r\n", bootdl->fw_info.fw_size);
    printf("firmware aes_en:%d,crc_en:%d\r\n", bootdl->fw_info.aes_en, bootdl->fw_info.crc_en);
    return 0;
}

int hgic_bootdl_request(unsigned char cmd, struct hgic_bootdl *bootdl)
{
    int ret;
    unsigned int temp;
    switch (cmd) {
        case HGIC_BOOTDL_CMD_ENTER:
            ret = hgic_bootdl_send_cmd(cmd, 0, 0);
            printf("enter bootdl\r\n");
            break;
        case HGIC_BOOTDL_CMD_WRITE_MEM:
            ret = hgic_bootdl_send_cmd(cmd, bootdl->fw_info.boot_addr + bootdl->offset, bootdl->frag_size);
            break;
        case HGIC_BOOTDL_CMD_RUN:
            temp = bootdl->fw_info.fw_size | HGIC_BOOT_CMD_RUN_PREACT_SP_CFG;
            if (bootdl->fw_info.aes_en) {temp |= HGIC_BOOT_CMD_RUN_PREACT_AES_DEC;}
            if (bootdl->fw_info.crc_en) {temp |= HGIC_BOOT_CMD_RUN_PREACT_CRC_CHK;}
            ret = hgic_bootdl_send_cmd(cmd, bootdl->fw_info.boot_addr, bootdl->fw_info.fw_size);
            printf("run\r\n");
            break;
        default:
            printf("No support cmd\r\n");
            ret = -1;
            break;
    }
    return ret;
}

int hgic_bootdl_send(unsigned char *data, unsigned int len)
{
    return raw_send(data, len);
}
