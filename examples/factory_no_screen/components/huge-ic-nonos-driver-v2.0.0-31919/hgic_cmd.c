#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "hgic_raw.h"

#if defined ( __CC_ARM )
#pragma anon_unions
#endif


int hgic_raw_send_cmd(int cmd, unsigned char *data, unsigned int len)
{
    struct hgic_ctrl_hdr *hdr = (struct hgic_ctrl_hdr *)data;
    memset(hdr->info, 0, sizeof(hdr->info));
    hdr->hdr.magic  = HGIC_HDR_TX_MAGIC;
    hdr->hdr.length = len;
    hdr->hdr.ifidx  = 1;
    hdr->hdr.cookie = 0;
    HDR_CMDID_SET(hdr, cmd);
    return spi_raw_send(data, len);
}

static int hgic_raw_set_str(int cmd, char *str)
{
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    strcpy((char *)hgic.cmd_buf + CMD_HDR_LEN, str);
    return hgic_raw_send_cmd(cmd, hgic.cmd_buf, CMD_HDR_LEN + strlen((const char *)str));
}
static int hgic_raw_set_bytes(int cmd, char *vals, int len)
{
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    memcpy(hgic.cmd_buf + CMD_HDR_LEN, vals, len);
    return hgic_raw_send_cmd(cmd, hgic.cmd_buf, CMD_HDR_LEN + len);
}
static int hgic_raw_set_byte(int cmd, char val)
{
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    hgic.cmd_buf[CMD_HDR_LEN] = val;
    return hgic_raw_send_cmd(cmd, hgic.cmd_buf, CMD_HDR_LEN + 1);
}
static int hgic_raw_set_int(int cmd, int val)
{
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    put_unaligned_le32((unsigned int)val, (unsigned char *)hgic.cmd_buf + CMD_HDR_LEN);
    return hgic_raw_send_cmd(cmd, hgic.cmd_buf, CMD_HDR_LEN + 4);
}
static int hgic_raw_set_intarray(int cmd, int *values, int cnt)
{
    int i = 0;
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    for (i = 0; i < cnt; i++) {
        put_unaligned_le32(values[i], (unsigned char *)hgic.cmd_buf + CMD_HDR_LEN + i * 4);
    }
    return hgic_raw_send_cmd(cmd, hgic.cmd_buf, CMD_HDR_LEN + i * 4);
}
static int hgic_raw_set_varint(int cmd, int cnt, ...)
{
    int i = 0;
    int val = 0;
    va_list argptr;

    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    va_start(argptr, cnt);
    for (i = 0; i < cnt; i++) {
        val = va_arg(argptr, int);
        put_unaligned_le32(val, (unsigned char *)hgic.cmd_buf + CMD_HDR_LEN + i * 4);
    }
    va_end(argptr);
    return hgic_raw_send_cmd(cmd, hgic.cmd_buf, CMD_HDR_LEN + i * 4);
}
static int hgic_raw_set_varbyte(int cmd, int cnt, ...)
{
    int i = 0;
    char val = 0;
    va_list argptr;

    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    va_start(argptr, cnt);
    for (i = 0; i < cnt; i++) {
        val = (char)va_arg(argptr, int);
        hgic.cmd_buf[CMD_HDR_LEN + i] = val;
    }
    va_end(argptr);
    return hgic_raw_send_cmd(cmd, hgic.cmd_buf, CMD_HDR_LEN + i);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
int hgic_raw_set_ssid(char *ssid)
{
    return hgic_raw_set_str(HGIC_CMD_SET_SSID, ssid);
}
int hgic_raw_set_bssid(char *bssid)
{
    return hgic_raw_set_bytes(HGIC_CMD_SET_BSSID, bssid, 6);
}
int hgic_raw_set_channel(int channel)
{
    return hgic_raw_set_int(HGIC_CMD_SET_CHANNEL, channel);
}
int hgic_raw_set_key_mgmt(char *key_mgmt)
{
    return hgic_raw_set_str(HGIC_CMD_SET_KEY_MGMT, key_mgmt);
}
int hgic_raw_set_wpa_psk(char *psk)
{
    return hgic_raw_set_bytes(HGIC_CMD_SET_WPA_PSK, psk, 64);
}
int hgic_raw_set_freq_range(int freq_start, int freq_end, int bss_bw)
{
    return hgic_raw_set_varint(HGIC_CMD_SET_FREQ_RANGE, 3, freq_start, freq_end, bss_bw);
}
int hgic_raw_set_bss_bw(char bss_bw)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_BSS_BW, bss_bw);
}
int hgic_raw_set_tx_mcs(char tx_mcs)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_TX_MCS, tx_mcs);
}
int hgic_raw_set_acs(char acs, char acs_tmo)
{
    return hgic_raw_set_varbyte(HGIC_CMD_ACS_ENABLE, 2, acs, acs_tmo);
}
int hgic_raw_set_mac(char *mac)
{
    memcpy(hgic.addr, mac, 6);
    return hgic_raw_set_bytes(HGIC_CMD_SET_MAC, mac, 6);
}
int hgic_raw_set_chan_list(unsigned short *chan_list, int count)
{
    int i = 0;
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));;
    for (i = 0; i < count && i < CHAN_MAX_COUNT; i++) {
        put_unaligned_le16(chan_list[i], (unsigned char *)hgic.cmd_buf + CMD_HDR_LEN + 2 + i * 2);
    }
    put_unaligned_le16((unsigned short)i, (unsigned char *)hgic.cmd_buf + CMD_HDR_LEN);
    return hgic_raw_send_cmd(HGIC_CMD_SET_CHAN_LIST, hgic.cmd_buf, CMD_HDR_LEN + 2 + i * 2);
}
int hgic_raw_set_mode(char *mode)
{
    return hgic_raw_set_str(HGIC_CMD_SET_WORK_MODE, mode);
}
int hgic_raw_set_paired_stas(char *stas, int len)
{
    if (len > 6 * STA_MAX_COUNT) { len = 6 * STA_MAX_COUNT; }
    return hgic_raw_set_bytes(HGIC_CMD_SET_PAIRED_STATIONS, stas, len);
}
int hgic_raw_set_pairing(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_PAIRING, enable);
}
int hgic_raw_set_txpower(int power)
{
    return hgic_raw_set_int(HGIC_CMD_SET_TX_POWER, power);
}
int hgic_raw_set_ps_connect(char period, char roundup)
{
    return hgic_raw_set_varbyte(HGIC_CMD_SET_PS_CONNECT, 2, period, roundup);
}
int hgic_raw_set_bss_max_idle(int bss_max_idle)
{
    return hgic_raw_set_int(HGIC_CMD_SET_BSS_MAX_IDLE, bss_max_idle);
}
int hgic_raw_set_wkio_mode(char wkio_mode)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_WKIO_MODE, wkio_mode);
}
int hgic_raw_set_dtim_period(int dtim_period)
{
    return hgic_raw_set_int(HGIC_CMD_SET_DTIM_PERIOD, dtim_period);
}
int hgic_raw_set_ps_mode(char ps_mode)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_PS_MODE, ps_mode);
}
int hgic_raw_set_aplost_time(int aplost_time)
{
    return hgic_raw_set_int(HGIC_CMD_SET_APLOST_TIME, aplost_time);
}
int hgic_raw_unpair(char *mac)
{
    return hgic_raw_set_bytes(HGIC_CMD_UNPAIR, mac, 6);
}
int hgic_raw_scan(void)
{
    return hgic_raw_set_byte(HGIC_CMD_SCAN, 1);
}
int hgic_raw_save(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_SAVE_CFG, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_join_group(char *group_addr, char aid)
{
    char data[7];
    memcpy(data, group_addr, 6);
    data[6] = aid;
    return hgic_raw_set_bytes(HGIC_CMD_JOIN_GROUP, data, 7);
}
int hgic_raw_set_auto_chswitch(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_AUTO_CHAN_SWITCH, enable);
}
int hgic_raw_set_mcast_key(char *mcast_key)
{
    return hgic_raw_set_bytes(HGIC_CMD_SET_MCAST_KEY, mcast_key, 64);
}
int hgic_raw_set_reassoc_wkhost(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_REASSOC_WKHOST, enable);
}
int hgic_raw_set_wakeup_io(char wakeup_io, char edge)
{
    return hgic_raw_set_varbyte(HGIC_CMD_SET_WAKEUP_IO, 2, wakeup_io, edge);
}
int hgic_raw_set_dbginfo(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_DBGINFO_OUTPUT, enable);
}
int hgic_raw_set_sysdbg(char *sysdbg)
{
    return hgic_raw_set_str(HGIC_CMD_SET_SYSDBG, sysdbg);
}
int hgic_raw_set_primary_chan(char primary_chan)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_PRIMARY_CHAN, primary_chan);
}
int hgic_raw_set_autosleep_time(char autosleep_time)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_AUTO_SLEEP_TIME, autosleep_time);
}
int hgic_raw_set_supper_pwr(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_SUPPER_PWR, enable);
}
int hgic_raw_set_radio_onoff(char on)
{
    return hgic_raw_set_byte(HGIC_CMD_RADIO_ONOFF, on);
}
int hgic_raw_set_r_ssid(char *r_ssid)
{
    return hgic_raw_set_str(HGIC_CMD_SET_REPEATER_SSID, r_ssid);
}
int hgic_raw_set_r_psk(char *r_psk)
{
    return hgic_raw_set_bytes(HGIC_CMD_SET_REPEATER_PSK, r_psk, 64);
}
int hgic_raw_set_auto_save(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_CFG_AUTO_SAVE, enable);
}
int hgic_raw_set_pair_autostop(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_PAIR_AUTOSTOP, enable);
}
int hgic_raw_set_dcdc13(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_DCDC13, enable);
}
int hgic_raw_set_acktmo(int acktmo)
{
    return hgic_raw_set_int(HGIC_CMD_SET_ACKTMO, acktmo);
}
int hgic_raw_open(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_DEV_OPEN, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_close(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_DEV_CLOSE, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_sleep(char sleep)
{
    return hgic_raw_set_byte(HGIC_CMD_ENTER_SLEEP, sleep);
}
int hgic_raw_get_fwinfo(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_FW_INFO, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_connect_state(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_CONN_STATE, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_set_uart_fixlen(unsigned short fixlen)
{
    return hgic_raw_set_bytes(HGIC_CMD_SET_UART_FIXLEN, (char *)&fixlen, 2);
}
int hgic_raw_get_uart_fixlen(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_UART_FIXLEN, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_set_mcast_txparam(char dupcnt, char txbw, char txmcs, char clearch)
{
    char vals[4] = {dupcnt, txbw, txmcs, clearch};
    return hgic_raw_set_bytes(HGIC_CMD_SET_MCAST_TXPARAM, vals, 4);
}
int hgic_raw_get_mode(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_MODE, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_set_mac_filter_en(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_MAC_FILTER_EN, enable);
}
int hgic_raw_set_ap_psmode_en(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_AP_PSMODE_EN, enable);
}
int hgic_raw_set_pa_pwrctl_dis(char disable)
{
    return hgic_raw_set_byte(HGIC_CMD_PA_PWRCTRL_DIS, disable);
}
int hgic_raw_set_dis_psconnect(char disable)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_DIS_PSCONNECT, disable);
}
int hgic_raw_set_wkdata_save_en(char enable)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_WKUPDATA_SAVEEN, enable);
}
int hgic_raw_set_wkhost_reasons(char *reasons, int count)
{
    return hgic_raw_set_bytes(HGIC_CMD_SET_WKUP_HOST_REASON, reasons, count);
}
int hgic_raw_wakeup_sta(char *sta_mac)
{
    return hgic_raw_set_bytes(HGIC_CMD_SET_WAKEUP_STA, sta_mac, 6);
}
int hgic_raw_get_wkdata_buff(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_WKDATA_BUFF, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_sta_list(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_STA_LIST, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_set_beacon_int(int beacon_int)
{
    return hgic_raw_set_int(HGIC_CMD_SET_BEACON_INT, beacon_int);
}
int hgic_raw_set_heartbeat_int(int heartbeat_int)
{
    return hgic_raw_set_int(HGIC_CMD_SET_HEARTBEAT_INT, heartbeat_int);
}
int hgic_raw_set_rtc(int rtc)
{
    return hgic_raw_set_int(HGIC_CMD_SET_RTC, rtc);
}
int hgic_raw_set_standby(char channel, int period_ms)
{
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    hgic.cmd_buf[CMD_HDR_LEN] = channel;
    put_unaligned_le32((unsigned int)period_ms, (unsigned char *)hgic.cmd_buf + CMD_HDR_LEN + 1);
    return hgic_raw_send_cmd(HGIC_CMD_STANDBY_CFG, hgic.cmd_buf, CMD_HDR_LEN + 5);
}
int hgic_raw_set_ether_type(unsigned short ether_type)
{
    return hgic_raw_set_int(HGIC_CMD_SET_ETHER_TYPE, ether_type);
}
int hgic_raw_set_roaming(char enable, char same_freq)
{
    return hgic_raw_set_varbyte(HGIC_CMD_SET_ROAMING, 2, enable, same_freq);
}
int hgic_raw_set_ap_chan_switch(char chan, char counter)
{
    return hgic_raw_set_varbyte(HGIC_CMD_SET_AP_CHAN_SWITCH, 2, chan, counter);
}
int hgic_raw_set_agg_cnt(char agg_cnt)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_AGG_CNT, agg_cnt);
}
int hgic_raw_set_load_def(char reset_en)
{
    return hgic_raw_set_byte(HGIC_CMD_LOAD_DEF, reset_en);
}
int hgic_raw_set_dhcpc_en(char en)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_DHCPC, en);
}
int hgic_raw_set_ant_auto(char en)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_ANT_AUTO, en);
}
int hgic_raw_set_ant_sel(char sel)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_ANT_SEL, sel);
}
int hgic_raw_set_ap_hide(char en)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_AP_HIDE, en);
}
int hgic_raw_set_dual_ant(char en)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_DUAL_ANT, en);
}
int hgic_raw_set_max_txcnt(char txcnt)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_MAX_TCNT, txcnt);
}
int hgic_raw_set_assert_holdup(char assert_holdup)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_ASSERT_HOLDUP, assert_holdup);
}
int hgic_raw_set_dup_filter_en(char en)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_DUPFILTER_EN, en);
}
int hgic_raw_set_dis_1v1_m2u(char dis)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_DIS_1V1_M2U, dis);
}
int hgic_raw_set_kick_assoc(char en)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_KICK_ASSOC, en);
}
int hgic_raw_set_connect_paironly(char en)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_CONNECT_PAIRONLY, en);
}
int hgic_raw_set_cust_isolation(char en)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_DIFFCUST_CONN, en);
}
int hgic_raw_set_wait_psmode(char mode)
{
    return hgic_raw_set_byte(HGIC_CMD_SET_WAIT_PSMODE, mode);
}
int hgic_raw_get_bgrssi(char chan_index)
{
    return hgic_raw_set_byte(HGIC_CMD_GET_BGRSSI, chan_index);
}
int hgic_raw_set_ps_heartbeat(unsigned int ipaddr, unsigned int dport, unsigned int period, unsigned int hb_tmo)
{
    unsigned char val[16];
    put_unaligned_le32(ipaddr, val);
    put_unaligned_le32(dport,  val + 4);
    put_unaligned_le32(period, val + 8);
    put_unaligned_le32(hb_tmo, val + 12);
    return hgic_raw_set_bytes(HGIC_CMD_SET_PS_HEARTBEAT, (char *)val, 16);
}
int hgic_raw_set_heartbeat_resp(char *heartbeat_resp, int len)
{
    return hgic_raw_set_bytes(HGIC_CMD_SET_PS_HEARTBEAT_RESP, heartbeat_resp, len);
}
int hgic_raw_set_ps_wkdata(char *ps_wkdata, int len)
{
    return hgic_raw_set_bytes(HGIC_CMD_SET_PS_WAKEUP_DATA, ps_wkdata, len);
}
int hgic_raw_disassoc_sta(char *addr)
{
    return hgic_raw_set_bytes(HGIC_CMD_DISASSOC_STA, addr, 6);
}
int hgic_raw_set_sta_freqinfo(char *addr, struct hgic_freqinfo *freqinfo)
{
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    memcpy(hgic.cmd_buf + CMD_HDR_LEN, addr, 6);
    memcpy(hgic.cmd_buf + CMD_HDR_LEN + 6, freqinfo, sizeof(struct hgic_freqinfo));
    return hgic_raw_send_cmd(HGIC_CMD_SET_STA_FREQINFO, hgic.cmd_buf, CMD_HDR_LEN + 6 + sizeof(struct hgic_freqinfo));
}
int hgic_raw_set_wkdata_mask(unsigned short offset/*start from ether hdr*/, unsigned char *mask, int mask_len)
{
    if(mask_len > 16) mask_len = 16;
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    put_unaligned_le16(offset, hgic.cmd_buf + CMD_HDR_LEN);
    memcpy(hgic.cmd_buf + CMD_HDR_LEN + 2, mask, mask_len);
    return hgic_raw_send_cmd(HGIC_CMD_SET_WKUPDATA_MASK, hgic.cmd_buf, CMD_HDR_LEN + 2 + mask_len);
}
int hgic_raw_set_hbdata_mask(unsigned short offset/*start from payload*/, unsigned char *mask, int mask_len)
{
    if(mask_len > 64) mask_len = 64;
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    put_unaligned_le16(offset, hgic.cmd_buf + CMD_HDR_LEN);
    put_unaligned_le16(mask_len, hgic.cmd_buf + CMD_HDR_LEN + 2);
    memcpy(hgic.cmd_buf + CMD_HDR_LEN + 4, mask, mask_len);
    return hgic_raw_send_cmd(HGIC_CMD_SET_PS_HBDATA_MASK, hgic.cmd_buf, CMD_HDR_LEN + 4 + mask_len);
}
int hgic_raw_send_customer_mgmt(char *dest, struct hgic_tx_info *info, char *data, int len)
{
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    if (dest) {
        memcpy(hgic.cmd_buf + CMD_HDR_LEN, dest, 6);
    } else {
        memset(hgic.cmd_buf + CMD_HDR_LEN, 0xff, 6);
    }
    if (info) {
        memcpy(hgic.cmd_buf + CMD_HDR_LEN + 6, info, sizeof(struct hgic_tx_info));
    }
    memcpy(hgic.cmd_buf + CMD_HDR_LEN + 6 + sizeof(struct hgic_tx_info), data, len);
    return hgic_raw_send_cmd(HGIC_CMD_SEND_CUST_MGMT, hgic.cmd_buf, CMD_HDR_LEN + 6 + sizeof(struct hgic_tx_info) + len);
}
int hgic_raw_mcu_reset(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_RESET, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_set_atcmd(char *str)
{
    return hgic_raw_set_str(HGIC_CMD_SET_ATCMD, str);
}
int hgic_raw_set_reset_sta(char *addr)
{
    return hgic_raw_set_bytes(HGIC_CMD_SET_RESET_STA, addr, 6);
}
int hgic_raw_start_assoc(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_START_ASSOC, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_scan_list(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_SCAN_LIST, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_temperature(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_TEMPERATURE, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_ssid(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_SSID, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_bssid(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_BSSID, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_signal(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_SIGNAL, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_wpa_psk(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_WPA_PSK, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_sta_count(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_STA_COUNT, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_txpower(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_TX_POWER, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_aggcnt(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_AGG_CNT, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_freq_range(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_FREQ_RANGE, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_chan_list(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_CHAN_LIST, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_bss_bw(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_BSS_BW, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_key_mgmt(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_KEY_MGMT, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_module_type(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_MODULETYPE, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_dhcpc_result(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_DHCPC_RESULT, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_disassoc_reason(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_DISASSOC_REASON, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_rtc(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_RTC, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_center_freq(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_CENTER_FREQ, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_wakeup_reason(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_WAKEUP_REASON, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_ant_sel(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_ANT_SEL, hgic.cmd_buf, CMD_HDR_LEN);
}
int hgic_raw_get_acs_result(void)
{
    return hgic_raw_send_cmd(HGIC_CMD_GET_ACS_RESULT, hgic.cmd_buf, CMD_HDR_LEN);
}
