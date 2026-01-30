#ifndef _HGIC_RAW_H_
#define _HGIC_RAW_H_
#include "hgic.h"
#include "hgic_bootdl.h"

#define HGIC_VERSION "v2.0.0"

#ifdef __cplusplus
extern "C" {
#endif

#define STA_MAX_COUNT    (8)
#define CHAN_MAX_COUNT   (16)
#define BSS_MAX_COUNT    (12)

#define HGIC_RAW_ETHTYPE  0x66AA
#define HGIC_HDR_TX_MAGIC 0x1A2B
#define HGIC_HDR_RX_MAGIC 0x2B1A

struct hgic_raw {
    struct {
        int rssi;
        int evm;
        int tx_bitrate;
        int conn_state;
    } status;
    unsigned char tx_buf[2048];
    unsigned char cmd_buf[512];
    unsigned char bssid[6];        //for sta mode
    unsigned char addr[6];        //local addr
    unsigned char sta_cnt;
    unsigned char bss_cnt;
    struct hgic_sta_info sta_list[STA_MAX_COUNT]; //for ap  mode: support 8 stations
    unsigned char paired_stas[6 * STA_MAX_COUNT]; //support max 8 stations.
    unsigned int  paired_stas_cnt;
    struct hgic_bss_info bsslist[BSS_MAX_COUNT];
    struct hgic_fw_info  fwinfo;
    struct hgic_freqinfo freqinfo;
    struct hgic_module_hwinfo module_hwinfo;
    unsigned short txcookie, rxcookie;
};

enum hgic_hdr_type {
    HGIC_HDR_TYPE_ACK = 0x1,
    HGIC_HDR_TYPE_FRM,
    HGIC_HDR_TYPE_CMD,
    HGIC_HDR_TYPE_EVENT,
    HGIC_HDR_TYPE_FIRMWARE,
    HGIC_HDR_TYPE_NLMSG,
    HGIC_HDR_TYPE_BOOTDL,
    HGIC_HDR_TYPE_TEST,
    HGIC_HDR_TYPE_FRM2,
    HGIC_HDR_TYPE_TEST2,
    HGIC_HDR_TYPE_SOFTFC,
    HGIC_HDR_TYPE_OTA,
    HGIC_HDR_TYPE_CMD2,
    HGIC_HDR_TYPE_EVENT2,

    HGIC_HDR_TYPE_MAX,
};

typedef enum {
    HGIC_RAW_RX_TYPE_IGNORE       = 0,  //invalid data, just ignore it.
    HGIC_RAW_RX_TYPE_DATA         = 1,  //recieved data.
    HGIC_RAW_RX_TYPE_CMD_RESP     = 2,  //recieved cmd response.
    HGIC_RAW_RX_TYPE_BOOTDL_RESP  = 3,  //bootdl response.
    HGIC_RAW_RX_TYPE_OTA_RESP     = 4,  //ota response.
    HGIC_RAW_RX_TYPE_EVENT        = 5,  //firmware event.
} HGIC_RAW_RX_TYPE;

struct eth_hdr {
    unsigned char dst[6];
    unsigned char src[6];
    unsigned short type;
};

struct hgic_hdr {
    unsigned short magic;
    unsigned char  type;
    unsigned char  ifidx: 4, flags: 4;
    unsigned short length;
    unsigned short cookie;
} __attribute__((packed));

struct hgic_frm_hdr2 {
    struct hgic_hdr hdr;
};

struct hgic_ctrl_hdr {
    struct hgic_hdr hdr;
    union {
        struct {
            unsigned char cmd_id;
            short status;
        } cmd;
        struct {
            unsigned short  cmd_id;
            short status;
        } cmd2;
        struct {
            unsigned char event_id;
            short value;
        } event;
        struct {
            unsigned short event_id;
            short value;
        } event2;
        unsigned char info[4];
    };
};

struct hgic_bootdl_resp_hdr {
    struct hgic_hdr hdr;
    unsigned char   cmd;
    unsigned char   rsp;
    unsigned char   rsp_data[4];
    unsigned char   reserved;
    unsigned char   check;
} __attribute__((packed));

struct hgic_bootdl_cmd_hdr {
    struct hgic_hdr hdr;
    unsigned char   cmd;
    unsigned char   cmd_len;
    unsigned char   cmd_flag;
    unsigned char   addr[4];
    unsigned char   len[4];
    unsigned char   check;
} __attribute__((packed));

struct hgic_ota_hdr {
    unsigned int   version;
    unsigned int   off;
    unsigned int   tot_len;
    unsigned short len;
    unsigned short checksum;
    unsigned short chipid;
    unsigned short err_code;
};

#define CMD_HDR_LEN          (sizeof(struct hgic_ctrl_hdr))
#define HGIC_RAW_MAX_PAYLOAD (1800 - sizeof(struct hgic_frm_hdr2) - sizeof(struct eth_hdr))
#define HGIC_RAW_DATA_ROOM   (sizeof(hgic.tx_buf) - sizeof(struct hgic_frm_hdr2) - sizeof(struct eth_hdr))
#define MAC2STR(a) (a)[0]&0xff, (a)[1]&0xff, (a)[2]&0xff, (a)[3]&0xff, (a)[4]&0xff, (a)[5]&0xff
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

#define HTONS(x) ((unsigned short)((((x) & (unsigned short)0x00ffU) << 8) | (((x) & (unsigned short)0xff00U) >> 8)))
#define NTOHS(x) HTONS(x)
#define HTONL(x) ((((x) & (unsigned int)0x000000ffUL) << 24) | \
                  (((x) & (unsigned int)0x0000ff00UL) <<  8) | \
                  (((x) & (unsigned int)0x00ff0000UL) >>  8) | \
                  (((x) & (unsigned int)0xff000000UL) >> 24))
#define NTOHL(x) HTONL(x)

#define HDR_CMDID(ctl) ((ctl)->hdr.type==HGIC_HDR_TYPE_CMD2?(ctl)->cmd2.cmd_id:(ctl)->cmd.cmd_id)
#define HDR_CMDRESP(ctl) ((ctl)->hdr.type==HGIC_HDR_TYPE_CMD2?(ctl)->cmd2.status:(ctl)->cmd.status)
#define HDR_EVTID(ctl) ((ctl)->hdr.type==HGIC_HDR_TYPE_EVENT2?(ctl)->event2.event_id:(ctl)->event.event_id)
#define HDR_CMDID_SET(ctl, id) if(id>255){\
        (ctl)->hdr.type =HGIC_HDR_TYPE_CMD2;\
        (ctl)->cmd2.cmd_id = id;\
    }else{\
        (ctl)->hdr.type =HGIC_HDR_TYPE_CMD;\
        (ctl)->cmd.cmd_id = id;\
    }
#define HDR_EVTID_SET(ctl, id) if(id>255){\
        (ctl)->hdr.type =HGIC_HDR_TYPE_EVENT2;\
        (ctl)->event2.event_id = id;\
    }else{\
        (ctl)->hdr.type =HGIC_HDR_TYPE_EVENT;\
        (ctl)->event.event_id = id;\
    }

static inline void put_unaligned_le16(unsigned short val, unsigned char *p)
{
    *p++ = val;
    *p++ = val >> 8;
}
static inline void put_unaligned_le32(unsigned int val, unsigned char *p)
{
    put_unaligned_le16(val >> 16, p + 2);
    put_unaligned_le16(val, p);
}
static inline unsigned short get_unaligned_le16(const unsigned char *p)
{
    return p[0] | p[1] << 8;
}
static inline unsigned int get_unaligned_le32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

int hgic_raw_set_ssid(char *ssid);
int hgic_raw_set_bssid(char *bssid);
int hgic_raw_set_channel(int channel);
int hgic_raw_set_key_mgmt(char *key_mgmt);
int hgic_raw_set_wpa_psk(char *psk);
int hgic_raw_set_freq_range(int freq_start, int freq_end, int bss_bw);
int hgic_raw_set_bss_bw(char bss_bw);
int hgic_raw_set_tx_mcs(char tx_mcs);
int hgic_raw_set_acs(char acs, char acs_tmo);
int hgic_raw_set_mac(char *mac);
int hgic_raw_set_chan_list(unsigned short *chan_list, int count);
int hgic_raw_set_mode(char *mode);
int hgic_raw_set_paired_stas(char *stas, int len);
int hgic_raw_set_pairing(char enable);
int hgic_raw_set_txpower(int power);
int hgic_raw_set_ps_connect(char period, char roundup);
int hgic_raw_set_bss_max_idle(int bss_max_idle);
int hgic_raw_set_wkio_mode(char wkio_mode);
int hgic_raw_set_dtim_period(int dtim_period);
int hgic_raw_set_ps_mode(char ps_mode);
int hgic_raw_set_aplost_time(int aplost_time);
int hgic_raw_unpair(char *mac);
int hgic_raw_scan(void);
int hgic_raw_save(void);
int hgic_raw_join_group(char *group_addr, char aid);
int hgic_raw_set_auto_chswitch(char enable);
int hgic_raw_set_mcast_key(char *mcast_key);
int hgic_raw_set_reassoc_wkhost(char enable);
int hgic_raw_set_wakeup_io(char wakeup_io, char edge);
int hgic_raw_set_dbginfo(char enable);
int hgic_raw_set_sysdbg(char *sysdbg);
int hgic_raw_set_primary_chan(char primary_chan);
int hgic_raw_set_autosleep_time(char autosleep_time);
int hgic_raw_set_supper_pwr(char enable);
int hgic_raw_set_radio_onoff(char on);
int hgic_raw_set_r_ssid(char *r_ssid);
int hgic_raw_set_r_psk(char *r_psk);
int hgic_raw_set_auto_save(char enable);
int hgic_raw_set_pair_autostop(char enable);
int hgic_raw_set_dcdc13(char enable);
int hgic_raw_set_acktmo(int acktmo);
int hgic_raw_open(void);
int hgic_raw_close(void);
int hgic_raw_sleep(char sleep);
int hgic_raw_get_fwinfo(void);
int hgic_raw_get_connect_state(void);
int hgic_raw_set_uart_fixlen(unsigned short fixlen);
int hgic_raw_get_uart_fixlen(void);
int hgic_raw_set_mcast_txparam(char dupcnt, char txbw, char txmcs, char clearch);
int hgic_raw_get_mode(void);
int hgic_raw_set_mac_filter_en(char enable);
int hgic_raw_set_ap_psmode_en(char enable);
int hgic_raw_set_pa_pwrctl_dis(char disable);
int hgic_raw_set_dis_psconnect(char disable);
int hgic_raw_set_wkdata_save_en(char enable);
int hgic_raw_set_wkhost_reasons(char *reasons, int count);
int hgic_raw_wakeup_sta(char *mac);
int hgic_raw_get_wkdata_buff(void);
int hgic_raw_get_sta_list(void);
int hgic_raw_set_beacon_int(int beacon_int);
int hgic_raw_set_heartbeat_int(int heartbeat_int);
int hgic_raw_set_rtc(int rtc);
int hgic_raw_set_standby(char channel, int period_ms);
int hgic_raw_set_ether_type(unsigned short ether_type);;
int hgic_raw_set_roaming(char enable, char same_freq);
int hgic_raw_set_ap_chan_switch(char chan, char counter);
int hgic_raw_set_agg_cnt(char agg_cnt);
int hgic_raw_set_load_def(char reset_en);
int hgic_raw_set_dhcpc_en(char en);
int hgic_raw_set_ant_auto(char en);
int hgic_raw_set_ant_sel(char sel);
int hgic_raw_set_ap_hide(char en);
int hgic_raw_set_dual_ant(char en);
int hgic_raw_set_max_txcnt(char txcnt);
int hgic_raw_set_assert_holdup(char assert_holdup);
int hgic_raw_set_dup_filter_en(char en);
int hgic_raw_set_dis_1v1_m2u(char dis);
int hgic_raw_set_kick_assoc(char en);
int hgic_raw_set_connect_paironly(char en);
int hgic_raw_set_cust_isolation(char en);
int hgic_raw_set_wait_psmode(char mode);
int hgic_raw_get_bgrssi(char chan_index);
int hgic_raw_set_ps_heartbeat(unsigned int ipaddr, unsigned int dport, unsigned int period, unsigned int hb_tmo);
int hgic_raw_set_heartbeat_resp(char *heartbeat_resp, int len);
int hgic_raw_set_ps_wkdata(char *ps_wkdata, int len);
int hgic_raw_send_customer_mgmt(char *dest, struct hgic_tx_info *info, char *data, int len);
int hgic_raw_disassoc_sta(char *addr);
int hgic_raw_mcu_reset(void);
int hgic_raw_set_atcmd(char * str);
int hgic_raw_set_reset_sta(char *addr);
int hgic_raw_start_assoc(void);
int hgic_raw_get_scan_list(void);
int hgic_raw_get_temperature(void);
int hgic_raw_get_ssid(void);
int hgic_raw_get_bssid(void);
int hgic_raw_get_wpa_psk(void);
int hgic_raw_get_sta_count(void);
int hgic_raw_get_txpower(void);
int hgic_raw_get_aggcnt(void);
int hgic_raw_get_freq_range(void);
int hgic_raw_get_chan_list(void);
int hgic_raw_get_bss_bw(void);
int hgic_raw_get_key_mgmt(void);
int hgic_raw_get_module_type(void);
int hgic_raw_get_dhcpc_result(void);
int hgic_raw_get_disassoc_reason(void);
int hgic_raw_get_rtc(void);
int hgic_raw_get_center_freq(void);
int hgic_raw_get_wakeup_reason(void);
int hgic_raw_get_ant_sel(void);
int hgic_raw_get_acs_result(void);

/*
return value:
    1: has recieve data, **data point to the valid payload data, *len is payload data length.
    2: has recieve cmd response, **data will be filled with "+RESP:OK" or "+RESP:Fail"
    0: invalid data, just ignore it.
*/
HGIC_RAW_RX_TYPE hgic_raw_rx(unsigned char **data, unsigned int *len);

int hgic_raw_send(unsigned char *dest, unsigned char *data, unsigned int len);
int hgic_raw_send_ether(unsigned char *data, unsigned int len);
int hgic_bootdl_parse_fw(unsigned char *fw_data, struct hgic_bootdl *bootdl);
int hgic_bootdl_request(unsigned char cmd, struct hgic_bootdl *bootdl);
int hgic_bootdl_send(unsigned char *data, unsigned int len);
int hgic_ota_parse_fw(unsigned char *fw_data, struct hgic_ota *ota);
int hgic_ota_send_packet(struct hgic_ota *ota, unsigned char *data, unsigned int len);

extern int raw_send(unsigned char *data, unsigned int len);
extern int hgic_raw_send_cmd(int cmd, unsigned char *data, unsigned int len);

extern struct hgic_raw hgic;
#ifdef __cplusplus
}
#endif
#endif
