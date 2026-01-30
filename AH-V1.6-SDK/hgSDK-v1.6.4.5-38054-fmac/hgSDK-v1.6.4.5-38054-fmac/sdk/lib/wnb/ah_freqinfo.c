/**
  ******************************************************************************
  * @file    ah_freqinfo.c
  * @author  HUGE-IC Application Team
  * @version V1.0.1
  * @date    2023-11-7
  * @brief   IEEE802.11 AH Frequency defines
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2021 HUGE-IC</center></h2>
  *
  ******************************************************************************
  */ 
#include "typesdef.h"
#include "errno.h"
#include "list.h"
#include "dev.h"
#include "devid.h"
#include "list.h"
#include "osal/string.h"
#include "osal/mutex.h"
#include "osal/semaphore.h"
#include "osal/mutex.h"
#include "osal/task.h"
#include "osal/timer.h"
#include "hal/gpio.h"
#include "lib/sysvar.h"
#include "lib/skb/skb.h"
#include "lib/mac_bus.h"
#include "lib/skb/skb_list.h"
#include "lib/lmac/hgic.h"
#include "lib/lmac/lmac.h"
#include "lib/lmac/lmac_ah/ieee802_11_defs.h"
#include "lib/lmac/lmac_ah/ieee802_11_ah_defs.h"
#include "lib/wnb/libwnb.h"

#define ARRAYSIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct ieee80211_ah_freqinfo {
    unsigned char s1g_opclass, type, max_txpower, freq_count;
    uint16 freqlist[16];
};

const struct ieee80211_ah_freqinfo ah_freqs[] = {//fixed type to 1
    {1, 1, 22, 6, { 9055, 9095, 9135, 9175, 9215, 9255}}, /*U.S., 1M, type1*/
    //{1, 2, 22, 16, { 9020+5*5, 9020+5*7, 9020+5*9, 9020+5*11, 9020+5*13, 9020+5*15,
    //                9020+5*17, 9020+5*19, 9020+5*21, 9020+5*23, 9020+5*25, 9020+5*27,
    //                9020+5*29, 9020+5*31, 9020+5*33, 9020+5*35}}, /*U.S., 1M, type2*/
    {2, 1, 22, 6, { 9050, 9090, 9130, 9170, 9210, 9250}}, /*U.S., 2M, type1*/
    //{2, 2, 22, 8, { 9020+5*6, 9020+5*10, 9020+5*14, 9020+5*18, 9020+5*22, 9020+5*26, 9020+5*30, 9020+5*34}}, /*U.S., 2M, type2*/
    {3, 1, 22, 5, { 9060, 9100, 9140, 9180, 9220}}, /*U.S., 4M, type1, 926M deleted*/
    //{3, 2, 22, 4, { 9020+5*8, 9020+5*16, 9020+5*24, 9020+5*32}}, /*U.S., 4M, type2*/
    {4, 1, 22, 3, { 9080, 9160, 9240}}, /*U.S., 8M, type1*/
    //{4, 2, 22, 2, { 9020+5*12, 9020+5*28}}, /*U.S., 8M, type2*/
    //{5, 2, 22, 1, { 9020+5*20}}, /*U.S., 16M, type2*/

    {6, 1, 6, 5, { 8635, 8645, 8655, 8665, 8675}}, /*Europe, 1M, type1*/
    //{7, 2, 6, 2, { 8630+5*2, 8630+5*6}}, /*Europe, 2M, type1*/
    {8, 1, 6, 1, { 8660}}, /*Europe, 2M, for audio&video*/

		//invalid in China
    /*{9, 1, 10, 16, { 7550+5*1, 7550+5*3, 7550+5*5, 7550+5*7, 7550+5*9,
                    7550+5*11, 7550+5*13, 7550+5*15, 7550+5*17, 7550+5*19,
                    7550+5*21, 7550+5*23, 7550+5*25, 7550+5*27,
                    7550+5*29, 7550+5*31}},*/ /*China, 1M, type1*/
    /*{10, 1, 10, 8, { 7790+5*1, 7790+5*3, 7790+5*5, 7790+5*7, 7790+5*9,
                       7790+5*11, 7790+5*13, 7790+5*15}},*/ /*China, 1M, type1*/
    //{11, 2, 10, 4, { 7790+5*2, 7790+5*6, 7790+5*10, 7790+5*14}}, /*China, 2M, type2*/
    //{12, 2, 10, 2, { 7790+5*4, 7790+5*12}}, /*China, 4M, type2*/
    //{13, 2, 10, 1, { 7790+5*8}}, /*China, 8M, type2*/

    {14, 1, 10, 6, { 9180, 9190, 9200, 9210, 9220, 9230}}, /*Korea, 1M, type1*/
    {15, 1, 10, 3, { 9185, 9205, 9225}}, /*Korea, 2M, type1*/
    {16, 1, 10, 1, { 9215}}, /*Korea, 4M, type1*/

    {17, 1, 20, 5, { 9205, 9215, 9225, 9235, 9245}}, /*Singapore, 1M, type1*/
    {19, 1, 20, 2, { 9215, 9235}}, /*Singapore, 2M, type1*/
    {21, 1, 20, 1, { 9225}}, /*Singapore, 4M, type1*/

    {22, 1, 22, 6, { 9165, 9185, 9205, 9225, 9245, 9265}}, /*Australia, 1M, type1*/
    //{22, 2, 30, 8, { 9020+5*37, 9020+5*39, 9020+5*41, 9020+5*43, 9020+5*45, 9020+5*47, 9020+5*49, 9020+5*51}}, /*Australia, 1M, type2*/
    {23, 1, 22, 6, { 9160, 9180, 9210, 9230, 9250, 9270}}, /*Australia, 2M, type1*/
    //{23, 2, 30, 4, { 9020+5*38, 9020+5*42, 9020+5*46, 9020+5*50}}, /*Australia, 2M, type2*/
    {24, 1, 22, 3, { 9170, 9220, 9260}}, /*Australia, 4M, type1*/
    //{24, 2, 30, 2, { 9020+5*40, 9020+5*48}}, /*Australia, 4M, type2*/
    {25, 1, 22, 1, { 9240}}, /*Australia, 8M, type2*/

    //{26, 1, 36, 9, { 9020+5*27, 9020+5*29, 9020+5*31, 9020+5*33, 9020+5*35, 9020+5*37, 9020+5*39, 9020+5*41, 9020+5*43}}, /*New Zealand, 1M, type1*/
    {26, 1, 28, 4, { 9245, 9255, 9265, 9275}}, /*New Zealand, 1M, type2*/
    //{27, 1, 36, 4, { 9020+5*28, 9020+5*32, 9020+5*36, 9020+5*40}}, /*New Zealand, 2M, type1*/
    {27, 1, 28, 2, { 9250, 9270}}, /*New Zealand, 2M, type2*/
    //{28, 1, 36, 2, { 9020+5*30, 9020+5*38}}, /*New Zealand, 4M, type1*/
    {28, 1, 28, 1, { 9260}}, /*New Zealand, 4M, type2*/
    {29, 1, 5, 1, { 9190}}, /*New Zealand, 8M, type1*/
    
    {30, 1, 20, 2, { 9210, 9220}}, /*Indonesia, 1M, type1*/
    {31, 1, 20, 1, { 9215}}, /*Indonesia, 2M, type1*/
    
    {32, 1, 8, 5, { 9180, 9200, 9220, 9240, 9260}}, /*Japan, 1M, type1*/
    {33, 1, 8, 3, { 9220, 9240, 9260}}, /*Japan, 2M, type1*/
    {34, 1, 8, 1, { 9240}}, /*Japan, 4M, type1*/
    
    {35, 1, 20, 5, { 9195, 9205, 9215, 9225, 9235}}, /*Malaysia, 1M, type1*/
    {36, 1, 20, 2, { 9205, 9225}}, /*Malaysia, 2M, type1*/
    {37, 1, 20, 1, { 9215}}, /*Malaysia, 4M, type1*/
    
    {38, 1, 10, 5, { 9205, 9215, 9225, 9235, 9245}}, /*Thailand, 1M, type1*/
    {39, 1, 10, 2, { 9215, 9235}}, /*Thailand, 2M, type1*/
    {40, 1, 10, 1, { 9225}}, /*Thailand, 4M, type1*/
};

const struct ieee80211_ah_freqinfo *ah_get_freqinfo(char *country_region, uint8 bss_bw, uint8 type)
{
    int i = 0;
    uint8 s1g_opclass = 0;

    if(country_region == NULL)
        return NULL;
    
    if (os_strcasecmp(country_region, "US") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 1; break;
            case 2: s1g_opclass = 2; break;
            case 4: s1g_opclass = 3; break;
            case 8: s1g_opclass = 4; break;
            default: break;
        };
    } else if (os_strcasecmp(country_region, "EU") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 6; break;
            case 2: s1g_opclass = 8; break;
            default: break;
        };
/*    } else if (strcmp(country_region, "CN") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 9; break;
            case 2: s1g_opclass = 11; break;
            case 4: s1g_opclass = 12; break;
            case 8: s1g_opclass = 13; break;
            default: break;
        };*/
    } else if (os_strcasecmp(country_region, "KR") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 14; break;
            case 2: s1g_opclass = 15; break;
            case 4: s1g_opclass = 16; break;
            default: break;
        };
    } else if (os_strcasecmp(country_region, "SG") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 17; break;
            case 2: s1g_opclass = 19; break;
            case 4: s1g_opclass = 21; break;
            default: break;
        };
    } else if (os_strcasecmp(country_region, "AU") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 22; break;
            case 2: s1g_opclass = 23; break;
            case 4: s1g_opclass = 24; break;
            case 8: s1g_opclass = 25; break;
            default: break;
        };
    } else if (os_strcasecmp(country_region, "NZ") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 26; break;
            case 2: s1g_opclass = 27; break;
            case 4: s1g_opclass = 28; break;
            case 8: s1g_opclass = 29; break;
            default: break;
        };
    } else if (os_strcasecmp(country_region, "ID") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 30; break;
            case 2: s1g_opclass = 31; break;
            default: break;
        };
    } else if (os_strcasecmp(country_region, "JP") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 32; break;
            case 2: s1g_opclass = 33; break;
            case 4: s1g_opclass = 34; break;
            default: break;
        };
    } else if (os_strcasecmp(country_region, "MY") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 35; break;
            case 2: s1g_opclass = 36; break;
            case 4: s1g_opclass = 37; break;
            default: break;
        };    
    } else if (os_strcasecmp(country_region, "TH") == 0) {
        switch (bss_bw) {
            case 1: s1g_opclass = 38; break;
            case 2: s1g_opclass = 39; break;
            case 4: s1g_opclass = 40; break;
            default: break;
        };
    }

    for (i = 0; s1g_opclass && i < ARRAYSIZE(ah_freqs); i++) {
        if (ah_freqs[i].s1g_opclass == s1g_opclass && ah_freqs[i].type == type){ 
            return &ah_freqs[i];
        }
    }
    return NULL;
}

static int ah_set_chan_list(struct wireless_nb *wnb, uint16 *freqlist, uint8 freq_count)
{
    int ret = RET_ERR;
    freq_count = (freq_count > WNB_CHAN_COUNT ? WNB_CHAN_COUNT : freq_count);

    if (wnb_freq_type_match(module_efuse_info.module_type, get_unaligned_le16(freqlist)) == RET_ERR) {
        os_printf("invalid freq param\r\n");
        return ret;
    }

    wnb->cfg->freq_end = 0;
    wnb->cfg->freq_start = 0;
    wnb->cfg->chan_cnt = freq_count;
    os_memcpy(wnb->cfg->chan_list, freqlist, freq_count * sizeof(uint16));
    wnb_setup_chan_list(wnb);
    wnb->chan_idx  = 0;

    /*
    os_printf("wnb->chan_cnt = %d\r\n", wnb->chan_cnt);
    for (int i = 0; i < wnb->cfg->chan_cnt; i++) {
        if (i + 1 < wnb->cfg->chan_cnt) {
            _os_printf("%d,", wnb->chan_list[i]);
        } else {
            _os_printf("%d", wnb->chan_list[i]);
        }
    }*/
    
    if (wnb->ops->set_chan_list(wnb->ops, wnb->chan_list, wnb->chan_cnt)) {
        os_printf("set chan_list error!\r\n");
        return ret;
    }

    wnb->curr_freq = wnb->chan_list[0];
    if (wnb->ops->set_freq(wnb->ops, wnb->curr_freq)) {
        os_printf("set freq error!\r\n");
        return ret;
    }
    os_printf("set chan_list, count:%d\r\n", wnb->chan_cnt);
    ret = RET_OK;
    return ret;
}

static int ah_set_bss_bw(struct wireless_nb *wnb, uint8 bss_bw)
{
    int ret = RET_OK;
    if (wnb->cfg->bss_bw != bss_bw) {
        wnb->cfg->bss_bw = bss_bw;
        if (!lmac_set_bss_bw(wnb->ops, wnb->cfg->bss_bw)) {
            os_printf("set bss BW:%dM\r\n", wnb->cfg->bss_bw);
        }
    }
    return ret;
}

static int ah_set_tx_power(struct wireless_nb *wnb, uint8 tx_power)
{
    int ret = RET_ERR;
    tx_power = (tx_power > 20 ? 20 : tx_power);
    if (!lmac_set_txpower(wnb->ops, tx_power)) {
        wnb->cfg->tx_power = lmac_get_txpower(wnb->ops);
        os_printf("set tx_power:%ddBm\r\n", wnb->cfg->tx_power);
        ret = RET_OK;
    }
    return ret;
}

int ah_set_country_region(char *country_region, uint8 bss_bw, uint8 type) // type fixed to 1
{
    struct wireless_nb *wnb = sysvar(SYSVAR_ID_WIRELESS_NB);
    int ret = RET_OK;
    struct ieee80211_ah_freqinfo *freqinfo = (struct ieee80211_ah_freqinfo *)ah_get_freqinfo(country_region, bss_bw, type);

    if (freqinfo == NULL) {
        os_printf("invalid country region: %s, bw:%d, type:%d\r\n", country_region, bss_bw, type);
        return RET_ERR;
    }
    /*set freq list*/
    ret |= ah_set_chan_list(wnb, freqinfo->freqlist, freqinfo->freq_count);
	//os_printf("ret = %d\r\n", ret);
    /*set bw*/
    ret |= ah_set_bss_bw(wnb, bss_bw);
	//os_printf("ret = %d\r\n", ret);
    /*set tx power*/
    ret |= ah_set_tx_power(wnb, freqinfo->max_txpower);
	//os_printf("ret = %d\r\n", ret);

    if (!ret) {
        if (os_strcasecmp(wnb->cfg->country_region, country_region)) {
            os_memset(wnb->cfg->country_region, 0, COUNTRY_REGION_LEN);
            os_memcpy(wnb->cfg->country_region, country_region, 2);
        }
        wnb_save_cfg(wnb->cfg, 1);
		os_printf("save \'%s\' freqinfo!\r\n", wnb->cfg->country_region);
    }
    return ret;
}

