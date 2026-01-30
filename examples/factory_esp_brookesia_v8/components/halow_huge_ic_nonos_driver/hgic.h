#ifndef _HUGE_IC_H_
#define _HUGE_IC_H_
#ifdef __cplusplus
extern "C" {
#endif

#ifdef __RTOS__
#define HGIC_CMD_START 100
#else
#define HGIC_CMD_START 0
#endif

#if defined ( __CC_ARM )
#pragma anon_unions
#endif

typedef void (*hgic_init_cb)(void *args);
typedef void (*hgic_event_cb)(char *ifname, int event, int param1, int param2);

struct hgic_bss_info {
    unsigned char   bssid[6];
    unsigned char   ssid[32];
    unsigned char   encrypt;
    signed char     signal;
    unsigned short  freq;
};

struct hgic_fw_info {
    unsigned int    version;
    unsigned int    svn_version;
    unsigned short  chip_id;
    unsigned short  cpu_id;
    unsigned char   mac[6];
    unsigned char   resv[2]; //align 4 for app_version
    unsigned int    app_version;
    unsigned int    smt_dat;
};

struct hgic_sta_info {
    unsigned char   aid;
    unsigned char   ps:1;
    unsigned char   addr[6];
    signed char     rssi;
    signed char     evm;
    signed char     tx_snr;
    signed char     rx_snr;
};

struct hgic_freqinfo {
    unsigned char  bss_bw, chan_cnt;
    unsigned short freq_start, freq_end;
    unsigned short chan_list[16];
};

struct hgic_module_hwinfo {
    union{
        struct{
            unsigned char type;
            unsigned char saw:1, rev:7;
        };
        unsigned short v;
    };
};

struct hgic_acs_result {
    unsigned int  freq;//KHz
    unsigned char sync_cnt;
    signed char   min;
    signed char   max;
    signed char   avg;
};

struct hgic_exception_info {
    int num;
    union {
        struct  {
            int max, avg, min;
        } txdelay;
        struct  {
            int max, avg, min;
        } bgrssi;
        struct  {
            int total, used;
        } buffer_usage;
        struct  {
            int total, used;
        } heap_usage;
        struct  {
            int temp;
        } temperature;
    } info;
};

struct hgic_dhcpc_result {
    unsigned int ipaddr, netmask, svrip, router, dns1, dns2;
};

struct hgic_tx_info {
    unsigned char   band;
    unsigned char   tx_bw;
    unsigned char   tx_mcs;
    unsigned char   freq_idx: 5, antenna: 3;
    unsigned int    tx_flags;
    unsigned short  tx_flags2;
    unsigned char   priority;
    unsigned char   tx_power;
};

enum HGIC_EXCEPTION_NUM {
    HGIC_EXCEPTION_CPU_USED_OVERTOP         = 1,
    HGIC_EXCEPTION_HEAP_USED_OVERTOP        = 2,
    HGIC_EXCEPTION_WIFI_BUFFER_USED_OVERTOP = 3,
    HGIC_EXCEPTION_TX_BLOCKED               = 4,
    HGIC_EXCEPTION_TXDELAY_TOOLONG          = 5,
    HGIC_EXCEPTION_STRONG_BGRSSI            = 6,
    HGIC_EXCEPTION_TEMPERATURE_OVERTOP      = 7,
    HGIC_EXCEPTION_WRONG_PASSWORD           = 8,
};

enum hgic_cmd {
    HGIC_CMD_DEV_OPEN               = 1,
    HGIC_CMD_DEV_CLOSE              = 2,
    HGIC_CMD_SET_MAC                = 3,
    HGIC_CMD_SET_SSID               = 4,
    HGIC_CMD_SET_BSSID              = 5,
    HGIC_CMD_SET_COUNTERY           = 6,
    HGIC_CMD_SET_CHANNEL            = 7,
    HGIC_CMD_SET_CENTER_FREQ        = 8,
    HGIC_CMD_SET_RTS_THRESHOLD      = 9,
    HGIC_CMD_SET_FRG_THRESHOLD      = 10,
    HGIC_CMD_SET_KEY_MGMT           = 11,
    HGIC_CMD_SET_WPA_PSK            = 12,
    HGIC_CMD_SET_KEY                = 13,
    HGIC_CMD_SCAN                   = 14,
    HGIC_CMD_GET_SCAN_LIST          = 15,
    HGIC_CMD_SET_BSSID_FILTER       = 16,
    HGIC_CMD_DISCONNECT             = 17,
    HGIC_CMD_GET_BSSID              = 18,
    HGIC_CMD_SET_WBNAT              = 19,
    HGIC_CMD_GET_STATUS             = 20,
    HGIC_CMD_SET_LISTEN_INTERVAL    = 21,
    HGIC_CMD_SET_TX_POWER           = 22,
    HGIC_CMD_GET_TX_POWER           = 23,
    HGIC_CMD_SET_TX_LCOUNT          = 24,
    HGIC_CMD_SET_TX_SCOUNT          = 25,
    HGIC_CMD_ADD_STA                = 26,
    HGIC_CMD_REMOVE_STA             = 27,
    HGIC_CMD_SET_TX_BW              = 28,
    HGIC_CMD_SET_TX_MCS             = 29,
    HGIC_CMD_SET_FREQ_RANGE         = 30,
    HGIC_CMD_ACS_ENABLE             = 31,
    HGIC_CMD_SET_PRIMARY_CHAN       = 32,
    HGIC_CMD_SET_BG_RSSI            = 33,
    HGIC_CMD_SET_BSS_BW             = 34,
    HGIC_CMD_TESTMODE_CMD           = 35,
    HGIC_CMD_SET_AID                = 36,
    HGIC_CMD_GET_FW_STATE           = 37,
    HGIC_CMD_SET_TXQ_PARAM          = 38,
    HGIC_CMD_SET_CHAN_LIST          = 39,
    HGIC_CMD_GET_CONN_STATE         = 40,
    HGIC_CMD_SET_WORK_MODE          = 41,
    HGIC_CMD_SET_PAIRED_STATIONS    = 42,
    HGIC_CMD_GET_FW_INFO            = 43,
    HGIC_CMD_PAIRING                = 44,
    HGIC_CMD_GET_TEMPERATURE        = 45,
    HGIC_CMD_ENTER_SLEEP            = 46,
    HGIC_CMD_OTA                    = 47,
    HGIC_CMD_GET_SSID               = 48,
    HGIC_CMD_GET_WPA_PSK            = 49,
    HGIC_CMD_GET_SIGNAL             = 50,
    HGIC_CMD_GET_TX_BITRATE         = 51,
    HGIC_CMD_SET_BEACON_INT         = 52,
    HGIC_CMD_GET_STA_LIST           = 53,
    HGIC_CMD_SAVE_CFG               = 54,
    HGIC_CMD_JOIN_GROUP             = 55,
    HGIC_CMD_SET_ETHER_TYPE         = 56,
    HGIC_CMD_GET_STA_COUNT          = 57,
    HGIC_CMD_SET_HEARTBEAT_INT      = 58,
    HGIC_CMD_SET_MCAST_KEY          = 59,
    HGIC_CMD_SET_AGG_CNT            = 60,
    HGIC_CMD_GET_AGG_CNT            = 61,
    HGIC_CMD_GET_BSS_BW             = 62,
    HGIC_CMD_GET_FREQ_RANGE         = 63,
    HGIC_CMD_GET_CHAN_LIST          = 64,
    HGIC_CMD_RADIO_ONOFF            = 65,
    HGIC_CMD_SET_PS_HEARTBEAT       = 66,
    HGIC_CMD_SET_WAKEUP_STA         = 67,
    HGIC_CMD_SET_PS_HEARTBEAT_RESP  = 68,
    HGIC_CMD_SET_PS_WAKEUP_DATA     = 69,
    HGIC_CMD_SET_PS_CONNECT         = 70,
    HGIC_CMD_SET_BSS_MAX_IDLE       = 71,
    HGIC_CMD_SET_WKIO_MODE          = 72,
    HGIC_CMD_SET_DTIM_PERIOD        = 73,
    HGIC_CMD_SET_PS_MODE            = 74,
    HGIC_CMD_LOAD_DEF               = 75,
    HGIC_CMD_DISASSOC_STA           = 76,
    HGIC_CMD_SET_APLOST_TIME        = 77,
    HGIC_CMD_GET_WAKEUP_REASON      = 78,
    HGIC_CMD_UNPAIR                 = 79,
    HGIC_CMD_SET_AUTO_CHAN_SWITCH   = 80,
    HGIC_CMD_SET_REASSOC_WKHOST     = 81,
    HGIC_CMD_SET_WAKEUP_IO          = 82,
    HGIC_CMD_DBGINFO_OUTPUT         = 83,
    HGIC_CMD_SET_SYSDBG             = 84,
    HGIC_CMD_SET_AUTO_SLEEP_TIME    = 85,
    HGIC_CMD_GET_KEY_MGMT           = 86,
    HGIC_CMD_SET_PAIR_AUTOSTOP      = 87,
    HGIC_CMD_SET_SUPPER_PWR         = 88,
    HGIC_CMD_SET_REPEATER_SSID      = 89,
    HGIC_CMD_SET_REPEATER_PSK       = 90,
    HGIC_CMD_CFG_AUTO_SAVE          = 91,
    HGIC_CMD_SEND_CUST_MGMT         = 92,
    HGIC_CMD_GET_BATTERY_LEVEL      = 93,
    HGIC_CMD_SET_DCDC13             = 94,
    HGIC_CMD_SET_ACKTMO             = 95,
    HGIC_CMD_GET_MODULETYPE         = 96,
    HGIC_CMD_PA_PWRCTRL_DIS         = 97,
    HGIC_CMD_SET_DHCPC              = 98,
    HGIC_CMD_GET_DHCPC_RESULT       = 99,
    HGIC_CMD_SET_WKUPDATA_MASK      = 100,
    HGIC_CMD_GET_WKDATA_BUFF        = 101,
    HGIC_CMD_GET_DISASSOC_REASON    = 102,
    HGIC_CMD_SET_WKUPDATA_SAVEEN    = 103,
    HGIC_CMD_SET_CUST_DRIVER_DATA   = 104,
    HGIC_CMD_SET_MCAST_TXPARAM      = 105,
    HGIC_CMD_SET_STA_FREQINFO       = 106,
    HGIC_CMD_SET_RESET_STA          = 107,
    HGIC_CMD_SET_UART_FIXLEN        = 108,
    HGIC_CMD_GET_UART_FIXLEN        = 109,
    HGIC_CMD_SET_ANT_AUTO           = 110,
    HGIC_CMD_SET_ANT_SEL            = 111,
    HGIC_CMD_GET_ANT_SEL            = 112,
    HGIC_CMD_SET_WKUP_HOST_REASON   = 113,
    HGIC_CMD_SET_MAC_FILTER_EN      = 114,
    HGIC_CMD_SET_ATCMD              = 115,
    HGIC_CMD_SET_ROAMING            = 116,
    HGIC_CMD_SET_AP_HIDE            = 117,
    HGIC_CMD_SET_DUAL_ANT           = 118,
    HGIC_CMD_SET_MAX_TCNT           = 119,
    HGIC_CMD_SET_ASSERT_HOLDUP      = 120,
    HGIC_CMD_SET_AP_PSMODE_EN       = 121,
    HGIC_CMD_SET_DUPFILTER_EN       = 122,
    HGIC_CMD_SET_DIS_1V1_M2U        = 123,
    HGIC_CMD_SET_DIS_PSCONNECT      = 124,
    HGIC_CMD_SET_RTC                = 125,
    HGIC_CMD_GET_RTC                = 126,
    HGIC_CMD_SET_KICK_ASSOC         = 127,
    HGIC_CMD_START_ASSOC            = 128,
    HGIC_CMD_SET_AUTOSLEEP          = 129,
    HGIC_CMD_SEND_BLENC_DATA        = 130,
    HGIC_CMD_SET_BLENC_EN           = 131,
    HGIC_CMD_RESET                  = 132,
    HGIC_CMD_SET_HWSCAN             = 133,
    HGIC_CMD_GET_TXQ_PARAM          = 134,
    HGIC_CMD_SET_PROMISC            = 135,
    HGIC_CMD_SET_USER_EDCA          = 136,
    HGIC_CMD_SET_FIX_TXRATE         = 137,
    HGIC_CMD_SET_NAV_MAX            = 138,
    HGIC_CMD_CLEAR_NAV              = 139,
    HGIC_CMD_SET_CCA_PARAM          = 140,
    HGIC_CMD_SET_TX_MODGAIN         = 141,
    HGIC_CMD_GET_NAV                = 142,
    HGIC_CMD_SET_BEACON_START       = 143,
    HGIC_CMD_SET_BLE_OPEN           = 144,
    HGIC_CMD_GET_MODE               = 145,
    HGIC_CMD_GET_BGRSSI             = 146,
    HGIC_CMD_SEND_BLENC_ADVDATA     = 147,
    HGIC_CMD_SEND_BLENC_SCANRESP    = 148,
    HGIC_CMD_SEND_BLENC_DEVADDR     = 149,
    HGIC_CMD_SEND_BLENC_ADVINTERVAL = 150,
    HGIC_CMD_SEND_BLENC_STARTADV    = 151,
    HGIC_CMD_SET_RTS_DURATION       = 152,
    HGIC_CMD_STANDBY_CFG            = 153,
    HGIC_CMD_SET_CONNECT_PAIRONLY   = 154,
    HGIC_CMD_SET_DIFFCUST_CONN      = 155,
    HGIC_CMD_GET_CENTER_FREQ        = 156,
    HGIC_CMD_SET_WAIT_PSMODE        = 157,
    HGIC_CMD_SET_AP_CHAN_SWITCH     = 158,
    HGIC_CMD_SET_CCA_FOR_CE         = 159,
    HGIC_CMD_SET_DISABLE_PRINT      = 160,
    HGIC_CMD_SET_APEP_PADDING       = 161,
    HGIC_CMD_GET_ACS_RESULT         = 162,
    HGIC_CMD_GET_WIFI_STATUS_CODE   = 163,
    HGIC_CMD_GET_WIFI_REASON_CODE   = 164,
    HGIC_CMD_SET_PS_HBDATA_MASK     = 183,
};

enum hgic_event {
    HGIC_EVENT_STATE_CHG           = 1,
    HGIC_EVENT_CH_SWICH            = 2,
    HGIC_EVENT_DISCONNECT_REASON   = 3,
    HGIC_EVENT_ASSOC_STATUS        = 4,
    HGIC_EVENT_SCANNING            = 5,
    HGIC_EVENT_SCAN_DONE           = 6,
    HGIC_EVENT_TX_BITRATE          = 7,
    HGIC_EVENT_PAIR_START          = 8,
    HGIC_EVENT_PAIR_SUCCESS        = 9,
    HGIC_EVENT_PAIR_DONE           = 10,
    HGIC_EVENT_CONECT_START        = 11,
    HGIC_EVENT_CONECTED            = 12,
    HGIC_EVENT_DISCONECTED         = 13,
    HGIC_EVENT_SIGNAL              = 14,
    HGIC_EVENT_DISCONNET_LOG       = 15,
    HGIC_EVENT_REQUEST_PARAM       = 16,
    HGIC_EVENT_TESTMODE_STATE      = 17,
    HGIC_EVENT_FWDBG_INFO          = 18,
    HGIC_EVENT_CUSTOMER_MGMT       = 19,
    HGIC_EVENT_SLEEP_FAIL          = 20,
    HGIC_EVENT_DHCPC_DONE          = 21,
    HGIC_EVENT_CONNECT_FAIL        = 22,
    HGIC_EVENT_CUST_DRIVER_DATA    = 23,
    HGIC_EVENT_UNPAIR_STA          = 24,
    HGIC_EVENT_BLENC_DATA          = 25,
    HGIC_EVENT_HWSCAN_RESULT       = 26,
    HGIC_EVENT_EXCEPTION_INFO      = 27,
    HGIC_EVENT_DSLEEP_WAKEUP       = 28,
    HGIC_EVENT_STA_MIC_ERROR       = 29,
    HGIC_EVENT_ACS_DONE            = 30,
};

extern int  hgic_sdio_init(void);
extern void hgic_sdio_exit(void);
extern int  hgic_usb_init(void);
extern void hgic_usb_exit(void);

#ifdef __RTOS__
struct firmware {
    unsigned char *data;
    unsigned int size;
};
int request_firmware(const struct firmware **fw, const char *name, void *dev);
void release_firmware(struct firmware *fw);

extern int hgicf_init(void);
extern int hgicf_cmd(char *ifname, unsigned int cmd, unsigned int param1, unsigned int param2);
extern int  hgics_init(void);
extern void hgics_exit(void);
extern int wpas_init(void);
extern int wpas_start(char *ifname);
extern int wpas_stop(char *ifname);
extern int wpas_cli(char *ifname, char *cmd, char *reply_buff, int reply_len);
extern int wpas_passphrase(char *ssid, char *passphrase, char psk[32]);
extern int hapd_init(void);
extern int hapd_start(char *ifname);
extern int hapd_stop(char *ifname);
extern int hapd_cli(char *ifname, char *cmd, char *reply_buff, int reply_len);
extern void hgic_param_iftest(int iftest);
extern const char *hgic_param_ifname(const char *name);
extern char *hgic_param_fwfile(const char *fw);
extern int hgic_param_ifcount(int count);
extern void hgic_param_initcb(hgic_init_cb cb);
extern void hgic_param_eventcb(hgic_event_cb cb);
extern int hgic_ota_start(char *ifname, char *fw_name);

void hgic_raw_init(void);
int hgic_raw_send(char *dest, char *data, int len);
int hgic_raw_rcev(char *buf, int size, char *src);

#ifdef HGIC_SMAC
#include "umac_config.h"
#endif
#endif

#ifdef __cplusplus
}
#endif
#endif
