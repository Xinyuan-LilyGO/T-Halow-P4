#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "hgic_raw.h"

#define HGIC_FWINFO_BOOT_HDR 0x5A69

static unsigned short hgic_ota_check_sum(unsigned char *addr, int count)
{
    int sum = 0;
    while (count > 1) {
        sum = sum + *(unsigned short *)addr;
        addr  += 2;
        count -= 2;
    }
    if (count > 0) {
        sum += *addr;
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (unsigned short)~sum;
}

int hgic_ota_parse_fw(unsigned char *fw_data, struct hgic_ota *ota)
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
    ota->fw_info.hdr_len = fw_hdr->boot.boot_code_offset_addr;
    ota->fw_info.fw_size = fw_hdr->boot.boot_from_flash_len;
    ota->fw_info.chip_id = fw_hdr->fw_infor.chip_id;
    ota->fw_info.sdk_version = fw_hdr->fw_infor.sdk_version;
    ota->offset = 0;
    printf("firmware version  : v%d-%d\r\n", fw_hdr->fw_infor.svn_version, (ota->fw_info.sdk_version & 0xff));
    printf("firmware chip id  : %x\r\n", ota->fw_info.chip_id);
    printf("firmware hdr len  : %d\r\n", ota->fw_info.hdr_len);
    printf("firmware size     : %d\r\n", ota->fw_info.fw_size);
    printf("firmware aes_en:%d,crc_en:%d\r\n", fw_hdr->boot.mode.aes_en, fw_hdr->boot.mode.crc_en);
    return 0;
}

int hgic_ota_send_packet(struct hgic_ota *ota, unsigned char *data, unsigned int len)
{
    struct hgic_hdr *hdr = (struct hgic_hdr *)data;
    struct hgic_ota_hdr *ota_hdr = (struct hgic_ota_hdr *)(hdr + 1);
    unsigned char *fw_data = (unsigned char *)(ota_hdr + 1);

    hdr->magic  = HGIC_HDR_TX_MAGIC;
    hdr->type   = HGIC_HDR_TYPE_OTA;
    hdr->length = len;
    hdr->ifidx  = 1;
    hdr->cookie = 0;
    memset(ota_hdr, 0, sizeof(struct hgic_ota_hdr));
    ota_hdr->version = ota->fw_info.sdk_version;
    ota_hdr->off = ota->offset;
    ota_hdr->tot_len = ota->fw_info.hdr_len + ota->fw_info.fw_size;
    ota_hdr->len = len;
    ota_hdr->checksum = hgic_ota_check_sum(fw_data, ota_hdr->len);
    ota_hdr->chipid = ota->fw_info.chip_id;
    return raw_send(data, len + sizeof(struct hgic_hdr) + sizeof(struct hgic_ota_hdr));
}

