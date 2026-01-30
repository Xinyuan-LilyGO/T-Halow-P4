hgic_sdspi是sdio spi模式的驱动实现
|
|-> hgic_sdspi_v2.c ---- 如果mcu的dma支持同时读写数据的实现，在v2版本上协议交互会更效率【推荐】
|-> hgic_sdspi.c    ---- 如果mcu的dma只支持单个方向传输数据只能使用v1版本

==============================================================================================
nos-os没有实现软件流控，低数据量通信可以不使用，如果瞬时发送数据量过大，最好能够实现软件流控
// txwnd定义为模组端可以用于接收主控数据用于发送的包数
// 在还没发送数据前，先初始化txwnd=2，然后每次发送数据或cmd时检查txwnd<2时，需要重新查询txwnd
// 模组返回的txwnd可能更新大于或等于2的值，即表示有空间接收包，如果小于2，则可做延迟再查询
// 成功发送后将txwnd数量减少1
int hgic_raw_request_txwnd(void)
{
    unsigned short txwnd = 0;
    struct hgic_frm_hdr2 *hdr = (struct hgic_frm_hdr2 *)hgic.tx_buf;
    hdr->hdr.magic  = HGIC_HDR_TX_MAGIC;
    hdr->hdr.type   = HGIC_HDR_TYPE_SOFTFC;
    hdr->hdr.length = sizeof(struct hgic_frm_hdr2);
    hdr->hdr.ifidx  = 1;
    hdr->hdr.cookie = 0;
    hdr->hdr.flags  = 0;
    spi_raw_send(hgic.tx_buf, sizeof(struct hgic_frm_hdr2));
    // 等待同步

    /* 需要实现同步等待接收到HGIC_HDR_TYPE_SOFTFC类型响应更新txwnd
     *  case HGIC_HDR_TYPE_SOFTFC:
     *      txwnd = hdr->hdr.cookie;
     *      // 触发同步
     *      break;
     */
    return txwnd;
}

在HGIC_RAW_RX_TYPE hgic_raw_rx(unsigned char **data, unsigned int *len)函数中追加
    case HGIC_HDR_TYPE_SOFTFC:
        hgic.txwnd = hdr->hdr.cookie;
        break;

==============================================================================================
demo中接口测速测试函数，用于检测接口有无问题
// 单向接口测速，数据从主控发送到模组，模组不做检查
int hgic_raw_test(unsigned char *data, unsigned int len)
{
    struct hgic_frm_hdr2 *hdr = (struct hgic_frm_hdr2 *)hgic.tx_buf;

    if ((len > HGIC_RAW_DATA_ROOM) || (len > HGIC_RAW_MAX_PAYLOAD)) {
        printf("**too big data**\r\n");
        return -1;
    }

    hdr->hdr.magic  = HGIC_HDR_TX_MAGIC;
    hdr->hdr.type   = HGIC_HDR_TYPE_TEST;
    hdr->hdr.length = len;
    hdr->hdr.ifidx  = 1;
    hdr->hdr.cookie = 0;
    memcpy((unsigned char *)(hdr + 1), data, len);
    return spi_raw_send(hgic.tx_buf, len + sizeof(struct hgic_frm_hdr2));
}

// 双向接口测速，数据从主控发送到模组，模组将数据原样发回
int hgic_raw_test2(unsigned char *data, unsigned int len)
{
    struct hgic_frm_hdr2 *hdr = (struct hgic_frm_hdr2 *)hgic.tx_buf;

    if ((len > HGIC_RAW_DATA_ROOM) || (len > HGIC_RAW_MAX_PAYLOAD)) {
        printf("**too big data**\r\n");
        return -1;
    }

    hdr->hdr.magic  = HGIC_HDR_TX_MAGIC;
    hdr->hdr.type   = HGIC_HDR_TYPE_TEST2;
    hdr->hdr.length = len + sizeof(struct hgic_frm_hdr2);
    hdr->hdr.ifidx  = 1;
    hdr->hdr.cookie = 0;
    memcpy((unsigned char *)(hdr + 1), data, len);
    return spi_raw_send(hgic.tx_buf, len + sizeof(struct hgic_frm_hdr2));
}

在HGIC_RAW_RX_TYPE hgic_raw_rx(unsigned char **data, unsigned int *len)函数中追加
    case HGIC_HDR_TYPE_TEST2:
        *data += sizeof(struct hgic_frm_hdr2);
        *len   = hdr->hdr.length - sizeof(struct hgic_frm_hdr2);
        return HGIC_RAW_RX_TYPE_DATA; //data