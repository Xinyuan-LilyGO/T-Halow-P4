#ifndef _HGIC_FWINFO_H_
#define _HGIC_FWINFO_H_

#define HGIC_BOOTDL_CMD_ENTER                   (0x00)
#define HGIC_BOOTDL_CMD_WRITE_MEM               (0x02)
#define HGIC_BOOTDL_CMD_RUN                     (0x04)

#define HGIC_BOOT_CMD_RUN_PREACT_AES_DEC        (1 << 31)
#define HGIC_BOOT_CMD_RUN_PREACT_CRC_CHK        (1 << 30)
#define HGIC_BOOT_CMD_RUN_PREACT_SP_CFG         (1 << 29)

struct hgic_spiflash_header_boot {
    unsigned short    boot_flag;              /* 0  :  0x5a69, header boot flag  */
    unsigned char     version;                /* 2  :  version                  */
    unsigned char     size;                   /* 3  :  Link to Next Header      */
    unsigned int      boot_to_sram_addr;       /* 4  :  spi data load to sram addr   */
    unsigned int      run_sram_addr;           /* 8  :  code execute start addr  */
    unsigned int      boot_code_offset_addr;     /* 12 :  HeaderLen+ParamLen=4096+512        */
    unsigned int      boot_from_flash_len;       /* 16 :         */
    unsigned short    boot_data_crc;           /* 20 :  boot data crc check      */
    unsigned short    flash_blk_size;       /* 22 :  flash size in 64KB(version > 1),   512B(version== 0) */
    unsigned short    boot_baud_mhz : 14,   /* 24 :  spi clk freq in mhz(version > 1),  khz(version== 0) */
                      driver_strength : 2;      /*       io driver strength */

    struct {
        unsigned short pll_src : 8,   /*       pll src in Mhz */
                       pll_en : 1,   /*       PLL enable */
                       debug : 1,   /*       debug info uart output enable */
                       aes_en : 1,   /*       AES enable */
                       crc_en : 1,   /*       CRC check enable */
                       reserved : 4;
    } __attribute__((packed)) mode;                     /* 26 :  boot option */
    unsigned short reserved;               /* 28 :  */
    unsigned short head_crc16;             /*       (size+4) byte CRC16 check value */
} __attribute__((packed));

struct hgic_spiflash_read_cfg {
    unsigned char  read_cmd;                       /* read_cmd */
    unsigned char  cmd_dummy_cycles : 4,            /* read dummy cycles */
                   clock_mode : 2,            /* clock polarity & phase */
                   spec_sequnce_en : 1,            /* spec sequnce to execute, maybe same with quad_wire_en */
                   quad_wire_en : 1;                   /* spi D2/D3 enable */

    unsigned char  wire_mode_cmd : 2,
                   wire_mode_addr : 2,
                   wire_mode_data : 2,
                   quad_wire_select : 2;               /* spi D2/D3 group select */

    unsigned char  reserved3;

    unsigned short sample_delay;                   /* RX sample dalay time: 0 ~ clk_divor */
} __attribute__((packed));

struct hgic_spiflash_header_spi_info {
    unsigned char  func_code;                       /* 0 : header function(0x1)  */
    unsigned char  size;                            /* 1:  Link to Next Header   */

    struct         hgic_spiflash_read_cfg read_cfg;
    unsigned char  hgic_spiflash_spec_sequnce[64];

    unsigned short header_crc16;                   /*     (size+2) byte CRC16 check value */
} __attribute__((packed));

/*  hgic_ah_fw_v1.0.1.1_2020.2.20.bin  ?*/

struct hgic_spiflash_header_firmware_info {
    unsigned char func_code;                       /* 0 : header function(0x2)  */
    unsigned char size;                            /* 1:  Link to Next Header   */
    unsigned int sdk_version;                         /* version   */
    unsigned int svn_version;
    unsigned int date;                           /* date   */
    unsigned short chip_id;                        /* chip_id   */
    unsigned char cpu_id;                         /* cpu id, fix 0  */
    unsigned int code_crc32;                    /* code CRC32 */
    unsigned short param_crc16;        /* param CRC16 */
    unsigned short crc16;                   /*     (size+2) byte CRC16 check value */
} __attribute__((packed));

struct hgic_fw_info_hdr {
    struct hgic_spiflash_header_boot        boot;
    struct hgic_spiflash_header_spi_info        spi_infor;      /* func1*/
    struct hgic_spiflash_header_firmware_info   fw_infor ;  /* func2*/
} __attribute__((packed));

struct hgic_bootdl_fw_info {
    unsigned int hdr_len;
    unsigned int boot_addr;
    unsigned int fw_size;
    unsigned int aes_en : 1,
                 crc_en : 1;
};

struct hgic_bootdl {
    struct hgic_bootdl_fw_info fw_info;
    unsigned int offset;
    unsigned int frag_size;
};

struct hgic_ota_fw_info {
    unsigned int hdr_len;
    unsigned int fw_size;
    unsigned short chip_id;
    unsigned int sdk_version;
};

struct hgic_ota {
    struct hgic_ota_fw_info fw_info;
    unsigned int offset;
    unsigned int frag_size;
};

#endif
