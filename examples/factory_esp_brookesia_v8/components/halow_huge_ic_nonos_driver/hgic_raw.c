#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "hgic_raw.h"

struct hgic_raw hgic;

static int hgic_raw_rx_event(struct hgic_ctrl_hdr *evt)
{
    int i = 0;
    unsigned char *data = (unsigned char *)(evt + 1);
    unsigned int event_id = HDR_EVTID(evt);
    switch (event_id) {
        case HGIC_EVENT_CONECT_START:
            printf("start conncectting...\r\n");
            break;
        case HGIC_EVENT_CONECTED:
            hgic.status.conn_state = 1;
            memcpy(hgic.bssid, data, 6);
            hgic_raw_send_cmd(HGIC_CMD_GET_STA_LIST, hgic.cmd_buf, CMD_HDR_LEN);
            printf("STA:"MACSTR" connected\r\n", MAC2STR(data));
            break;
        case HGIC_EVENT_DISCONECTED:
            hgic.status.conn_state = 0;
            memset(hgic.bssid, 0, 6);
            hgic_raw_send_cmd(HGIC_CMD_GET_STA_LIST, hgic.cmd_buf, CMD_HDR_LEN);
            printf("STA:"MACSTR" disconnected\r\n", MAC2STR(data));
            break;
        case HGIC_EVENT_SIGNAL:
            hgic.status.rssi = data[0];
            hgic.status.evm  = data[1];
            break;
        case HGIC_EVENT_TX_BITRATE:
            hgic.status.tx_bitrate = get_unaligned_le32(data);
            break;
        case HGIC_EVENT_PAIR_START:
            printf("start pairing ...\r\n");
            break;
        case HGIC_EVENT_PAIR_SUCCESS:
            printf("pairing success\r\n");
            break;
        case HGIC_EVENT_PAIR_DONE:
            printf("pairing done\r\n");
            hgic.paired_stas_cnt = (evt->hdr.length - sizeof(struct hgic_ctrl_hdr)) / 6;
            if (hgic.paired_stas_cnt > STA_MAX_COUNT) {
                hgic.paired_stas_cnt = STA_MAX_COUNT;
            }
            memcpy(hgic.paired_stas, data, hgic.paired_stas_cnt * 6);

            printf("paired sta list:\r\n");
            for(i=0; i<hgic.paired_stas_cnt; i++){
                printf("%d:"MACSTR"\r\n", i, MAC2STR(hgic.paired_stas+i*6));
            }
            break;
        case HGIC_EVENT_SCANNING:
            printf("start scanning ...\r\n");
            break;
        case HGIC_EVENT_SCAN_DONE:
            hgic_raw_send_cmd(HGIC_CMD_GET_STA_LIST, hgic.cmd_buf, CMD_HDR_LEN);
            printf("scan done\r\n");
            break;
        case HGIC_EVENT_FWDBG_INFO:
            printf("%s", data);
            break;
        case HGIC_EVENT_EXCEPTION_INFO:{
            struct hgic_exception_info *exception_info = (struct hgic_exception_info *)data;
            switch (exception_info->num){
                case HGIC_EXCEPTION_STRONG_BGRSSI:
                    printf("STRONG BGRSSI:%d\r\n", exception_info->info.bgrssi.avg);
                    break;
                case HGIC_EXCEPTION_WRONG_PASSWORD:
                    printf("WRONG PASSWORD\r\n");
                    break;
                case HGIC_EXCEPTION_TEMPERATURE_OVERTOP:
                    printf("TEMPERATURE OVERTOP:%d\r\n", exception_info->info.temperature.temp);
                    break;
                case HGIC_EXCEPTION_CPU_USED_OVERTOP:
                    printf("CPU USED OVERTOP\r\n");
                    break;
                case HGIC_EXCEPTION_TXDELAY_TOOLONG:
                    printf("TXDELAY TOOLONG:%d\r\n", exception_info->info.txdelay.avg);
                    break;
                case HGIC_EXCEPTION_WIFI_BUFFER_USED_OVERTOP:
                    printf("WIFI BUFFER USED OVERTOP:total=%d, used=%d\r\n", 
                            exception_info->info.buffer_usage.total, exception_info->info.buffer_usage.used);
                    break;
            }
        }
            break;
        case HGIC_EVENT_ACS_DONE:
            printf("ACS DONE\r\n");
            break;
        case HGIC_EVENT_CUSTOMER_MGMT:
            printf("CUSTOMER MGMT\r\n");
            break;
        default:
            break;
    }
    *(unsigned short *)evt = event_id; //save event id
    return HGIC_RAW_RX_TYPE_EVENT;
}

static HGIC_RAW_RX_TYPE hgic_raw_rx_cmd_resp(unsigned char **data, unsigned int *len)
{
    struct hgic_ctrl_hdr *hdr = (struct hgic_ctrl_hdr *)(*data);
    unsigned int cmd_id = HDR_CMDID(hdr);
    short cmd_resp = HDR_CMDRESP(hdr);

    printf("CMD:%d, RESP:%s[%d]\r\n", cmd_id, cmd_resp < 0 ? "Fail" : "OK", cmd_resp);
    if (cmd_resp < 0) {
        return HGIC_RAW_RX_TYPE_IGNORE;
    }
    switch (cmd_id) {
        case HGIC_CMD_GET_STA_LIST:
            hgic.sta_cnt = cmd_resp / sizeof(struct hgic_sta_info);
            if (hgic.sta_cnt > STA_MAX_COUNT) hgic.sta_cnt = STA_MAX_COUNT;
            memset(hgic.sta_list, 0, sizeof(hgic.sta_list));
            if (hgic.sta_cnt > 0) {
                memcpy(hgic.sta_list, (char *)(hdr + 1), hgic.sta_cnt * sizeof(struct hgic_sta_info));
            }
            for (int i = 0; i < hgic.sta_cnt; i++) {
                printf("aid:%d, "MACSTR", ps:%d, rssi:%d, evm:%d, tx_snr:%d, rx_snr:%d\r\n", 
                        hgic.sta_list[i].aid, MAC2STR(hgic.sta_list[i].addr), hgic.sta_list[i].ps, 
                        hgic.sta_list[i].rssi, hgic.sta_list[i].evm, hgic.sta_list[i].tx_snr, hgic.sta_list[i].rx_snr);
            }
            break;
        case HGIC_CMD_GET_SCAN_LIST:
            hgic.bss_cnt = cmd_resp / sizeof(struct hgic_bss_info);
            if (hgic.bss_cnt > BSS_MAX_COUNT) hgic.bss_cnt = BSS_MAX_COUNT;
            memset(hgic.bsslist, 0, sizeof(hgic.bsslist));
            if (hgic.bss_cnt > 0) {
                memcpy(hgic.bsslist, (char *)(hdr + 1), hgic.bss_cnt * sizeof(struct hgic_bss_info));
            }
            for (int i = 0; i < hgic.bss_cnt; i++) {
                printf("%s, bssid:"MACSTR", freq:%d, signal:%d, encryption:%d\r\n", 
                    hgic.bsslist[i].ssid, MAC2STR(hgic.bsslist[i].bssid), hgic.bsslist[i].freq, hgic.bsslist[i].signal, hgic.bsslist[i].encrypt);
            }
            break;
        case HGIC_CMD_GET_FW_INFO:
            memcpy(&hgic.fwinfo, (char *)(hdr + 1), sizeof(struct hgic_fw_info));
            memcpy(hgic.addr, hgic.fwinfo.mac, 6);
            printf("hgic fw info:%d.%d.%d.%d, svn version:%d, "MACSTR", ",
                (hgic.fwinfo.version >> 24) & 0xff, (hgic.fwinfo.version >> 16) & 0xff,
                (hgic.fwinfo.version >> 8) & 0xff, (hgic.fwinfo.version & 0xff),
                 hgic.fwinfo.svn_version, MAC2STR(hgic.fwinfo.mac));
            if (hgic.fwinfo.smt_dat == 0) {
                printf("smt dat:Before 221020\r\n");
            } else {
                printf("smt dat:%u\r\n", hgic.fwinfo.smt_dat);
            }
            break;
        case HGIC_CMD_GET_CONN_STATE:
            if (cmd_resp == 9) {
                hgic.status.conn_state = 1;
                printf("Connected!\r\n");
            } else {
                hgic.status.conn_state = 0;
                printf("Disconnected!\r\n");
            }
            break;
        case HGIC_CMD_GET_UART_FIXLEN:
            printf("uart fixlen=%d\r\n", get_unaligned_le16((unsigned char *)(hdr + 1)));
            break;
        case HGIC_CMD_GET_MODE:
            printf("mode:%s\r\n", (char *)(hdr + 1));
            break;
        case HGIC_CMD_GET_WKDATA_BUFF:
            printf("wakeup data:%s\r\n", (char *)(hdr + 1));
            break;
        case HGIC_CMD_GET_TEMPERATURE:
            printf("temperature=%d\r\n", cmd_resp);
            break;
        case HGIC_CMD_GET_SSID:
            printf("ssid=%s\r\n", (char *)(hdr + 1));
            break;
        case HGIC_CMD_GET_BSSID:
            printf("bssid="MACSTR", my aid=%d\r\n", MAC2STR((char *)(hdr + 1)), *((char *)(hdr + 1) + 6));
            break;
        case HGIC_CMD_GET_SIGNAL:
            printf("signal=%d\r\n", cmd_resp - 0x1000);
            break;
        case HGIC_CMD_GET_WPA_PSK:
            printf("wpa_psk=%s\r\n", (char *)(hdr + 1));
            break;
        case HGIC_CMD_GET_KEY_MGMT:
            printf("key_mgmt=%s\r\n", (char *)(hdr + 1));
            break;
        case HGIC_CMD_GET_STA_COUNT:
            printf("sta count=%d\r\n", cmd_resp);
            break;
        case HGIC_CMD_GET_TX_POWER:
            printf("txpower=%d\r\n", cmd_resp);
            break;
        case HGIC_CMD_GET_AGG_CNT:
            printf("aggcnt=%d\r\n", cmd_resp);
            break;
        case HGIC_CMD_GET_FREQ_RANGE:
            printf("freq start:%d, freq end:%d, bss bw:%d\r\n", get_unaligned_le32((unsigned char *)(hdr + 1)),
                get_unaligned_le32((unsigned char *)(hdr + 1)+4), get_unaligned_le32((unsigned char *)(hdr + 1)+8));
            break;
        case HGIC_CMD_GET_CHAN_LIST:
            printf("channel list:");
            memcpy(&hgic.freqinfo.chan_list, (char *)(hdr + 1), sizeof(hgic.freqinfo.chan_list));
            hgic.freqinfo.chan_cnt = cmd_resp / sizeof(unsigned short);
            for (int i = 0; i < hgic.freqinfo.chan_cnt; i++) {
                printf("%d,", hgic.freqinfo.chan_list[i]);
            }
            printf("\r\n");
            break;
        case HGIC_CMD_GET_BSS_BW:
            printf("bss bw=%d\r\n", cmd_resp);
            break;
        case HGIC_CMD_GET_MODULETYPE:
            memcpy(&hgic.module_hwinfo, (char *)(hdr + 1), sizeof(struct hgic_module_hwinfo));
            printf("module type:%d, saw:%d\r\n", hgic.module_hwinfo.type, hgic.module_hwinfo.saw);
            break;
        case HGIC_CMD_GET_DHCPC_RESULT: {
            struct hgic_dhcpc_result *dhcpc = (struct hgic_dhcpc_result *)(hdr + 1);
            printf("ipaddr:%d, netmask:%d, svrip:%d, router:%d, dns1:%d, dns2:%d\r\n",
                dhcpc->ipaddr, dhcpc->netmask, dhcpc->svrip, dhcpc->router, dhcpc->dns1, dhcpc->dns2);
        }
            break;
        case HGIC_CMD_GET_DISASSOC_REASON:
            printf("disassoc reason=%d\r\n", cmd_resp);
            break;
        case HGIC_CMD_GET_RTC:
            printf("rtc_ms=%d\r\n", get_unaligned_le32((unsigned char *)(hdr + 1)));
            break;
        case HGIC_CMD_GET_CENTER_FREQ:
            printf("center freq=%d\r\n", cmd_resp);
            break;
        case HGIC_CMD_GET_WAKEUP_REASON:
            printf("wake reason=%d\r\n", cmd_resp);
            break;
        case HGIC_CMD_GET_ANT_SEL:
            printf("ant sel=%d\r\n", cmd_resp);
            break;
        case HGIC_CMD_GET_BGRSSI:{
            signed char *bgrssi = (signed char *)(hdr + 1);
            printf("bgrssi=max:%d min:%d avg:%d\r\n", bgrssi[0], bgrssi[1], bgrssi[2]);
        }
            break;
        case HGIC_CMD_GET_ACS_RESULT:
            hgic.freqinfo.chan_cnt = cmd_resp / sizeof(struct hgic_acs_result);
            printf("chan_cnt:%d\r\n", hgic.freqinfo.chan_cnt);
            struct hgic_acs_result *chan_info = (struct hgic_acs_result *)(hdr + 1);
            for (int i = 0; i < hgic.freqinfo.chan_cnt; i++) {
                printf("freq:%d, max:%d avg:%d min:%d\r\n", 
                chan_info[i].freq, chan_info[i].max, chan_info[i].avg, chan_info[i].min);
            }
            break;
        default:
            break;
    }

    sprintf((char *)*data, "+RESP:%s[%d]", cmd_resp >= 0 ? "OK" : "Fail", cmd_resp);
    return HGIC_RAW_RX_TYPE_CMD_RESP;
}

HGIC_RAW_RX_TYPE hgic_raw_rx(unsigned char **data, unsigned int *len)
{
    struct eth_hdr *ehdr;
    struct hgic_ctrl_hdr *hdr = (struct hgic_ctrl_hdr *)(*data);
    struct hgic_bootdl_resp_hdr *brhdr;
    struct hgic_ota_hdr *otahdr;

    // if (hdr->hdr.magic != HGIC_HDR_RX_MAGIC) {
    //     printf("invalid magic %x\r\n", hdr->hdr.magic);
    //     return HGIC_RAW_RX_TYPE_IGNORE;
    // }

    switch (hdr->hdr.type) {
            case HGIC_HDR_TYPE_TEST2:
        *data += sizeof(struct hgic_frm_hdr2);
        *len   = hdr->hdr.length - sizeof(struct hgic_frm_hdr2);
        return HGIC_RAW_RX_TYPE_DATA; //data
        case HGIC_HDR_TYPE_FRM2:
            *data += sizeof(struct hgic_frm_hdr2);
            *len   = hdr->hdr.length - sizeof(struct hgic_frm_hdr2);
            ehdr   = (struct eth_hdr *)(*data);
            if (hgic.rxcookie+1 != hdr->hdr.cookie) {
                printf("cookie err: last:%d, new:%d\r\n", hgic.rxcookie, hdr->hdr.cookie);
            }
            hgic.rxcookie = hdr->hdr.cookie;
            return HGIC_RAW_RX_TYPE_DATA; //data
        case HGIC_HDR_TYPE_CMD:
        case HGIC_HDR_TYPE_CMD2:
            return hgic_raw_rx_cmd_resp(data, len);
        case HGIC_HDR_TYPE_EVENT:
        case HGIC_HDR_TYPE_EVENT2:
            hgic_raw_rx_event((struct hgic_ctrl_hdr *)(*data));
            break;
        case HGIC_HDR_TYPE_BOOTDL:
            brhdr = (struct hgic_bootdl_resp_hdr *)(*data);
            **data = brhdr->rsp; //save reponse code
            return HGIC_RAW_RX_TYPE_BOOTDL_RESP;
        case HGIC_HDR_TYPE_OTA:
            otahdr = (struct hgic_ota_hdr *)(*data + sizeof(struct hgic_hdr));
            **(unsigned short **)data = otahdr->err_code; //save reponse code
            return HGIC_RAW_RX_TYPE_OTA_RESP;
        default:
            break;
    }
    return HGIC_RAW_RX_TYPE_IGNORE; //ignore
}

/* send raw data: needn't care the packet format.
    data: raw data
    len : raw data len
*/
int hgic_raw_send(unsigned char *dest, unsigned char *data, unsigned int len)
{
    struct hgic_frm_hdr2 *hdr = (struct hgic_frm_hdr2 *)hgic.tx_buf;
    struct eth_hdr      *ehdr = (struct eth_hdr *)(hdr + 1);

    if ((len > HGIC_RAW_DATA_ROOM) || (len > HGIC_RAW_MAX_PAYLOAD)) {
        printf("**too big data**\r\n");
        return -1;
    }

    hdr->hdr.magic  = HGIC_HDR_TX_MAGIC;
    hdr->hdr.type   = HGIC_HDR_TYPE_FRM2;
    hdr->hdr.length = len + sizeof(struct hgic_frm_hdr2) + sizeof(struct eth_hdr);
    hdr->hdr.ifidx  = 1;
    hdr->hdr.cookie = hgic.txcookie++;
    hdr->hdr.flags  = 0;
    memcpy(ehdr->dst, (dest?dest:hgic.bssid), 6);
    memcpy(ehdr->src, hgic.addr, 6);
    ehdr->type = HTONS(HGIC_RAW_ETHTYPE);
    memcpy((unsigned char *)(ehdr + 1), data, len);
    return raw_send(hgic.tx_buf, len + sizeof(struct hgic_frm_hdr2) + sizeof(struct eth_hdr));
}


/* send ethernet frame
    data: ethernet frame packet
    len : frame len
*/
int hgic_raw_send_ether(unsigned char *data, unsigned int len)
{
    struct hgic_frm_hdr2 *hdr = (struct hgic_frm_hdr2 *)hgic.tx_buf;

    if ((len > HGIC_RAW_DATA_ROOM) || (len > HGIC_RAW_MAX_PAYLOAD)) {
        printf("**too big data**\r\n");
        return -1;
    }

    hdr->hdr.magic  = HGIC_HDR_TX_MAGIC;
    hdr->hdr.type   = HGIC_HDR_TYPE_FRM2;
    hdr->hdr.length = len + sizeof(struct hgic_frm_hdr2);
    hdr->hdr.ifidx  = 1;
    hdr->hdr.cookie = hgic.txcookie++;
    hdr->hdr.flags  = 0;
    memcpy((unsigned char *)(hdr + 1), data, len);
    return raw_send(hgic.tx_buf, len + sizeof(struct hgic_frm_hdr2));
}

