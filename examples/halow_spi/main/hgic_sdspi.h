#ifndef _HGIC_SDSPI_H_
#define _HGIC_SDSPI_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined ( __CC_ARM )
#pragma anon_unions
#endif

/**
  * @brief  SPI Driver write and read function
  * @param  priv: spi driver private data
            wdata: Data buffer to be Tx
            rdata: Data buffer to be Rx
            len : Data length
            dma_flag: data trnasfer state can use dma
  * @retval None
  */
extern void spidrv_write_read(void *priv, unsigned char *wdata, unsigned char *rdata, unsigned int len);

/**
  * @brief  SPI Driver write function
  * @param  priv: spi driver private data
            data: Data buffer to be Tx
            len : Data length
            dma_flag: data trnasfer state can use dma
  * @retval None
  */
extern void spidrv_write(void *priv, unsigned char *data, unsigned int len, char dma_flag);

/**
  * @brief  SPI Driver read function
  * @param  priv: spi driver private data
            data: Data buffer to be Rx
            len : Data length
            dma_flag: data trnasfer state can use dma
  * @retval None
  */
extern void spidrv_read(void *priv, unsigned char *data, unsigned int len, char dma_flag);

/**
  * @brief  SPI Driver CS function
  * @param  priv: spi driver private data
            enable: Not zero as CS line enableed (low),
                    Zero as CS line disableed (high).
  * @retval None
  */
extern void spidrv_cs(void *priv, char enable);

/**
  * @brief  hardware CRC function. just return 0 if not support.
  * @param  priv: spi driver private data
            data: Data buffer need to CRC
            len : Data length
            flag: Not zero as data 16-bit CRC,
                  Zero as command 7-bit CRC.
  * @retval CRC result
  */
extern int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int len, char flag);

//#define HW_CRC_ON


/**
  * @brief  hgic sdspi Init function.
  * @param  priv: spi driver private data
  * @retval 
  *         0: init success.
  *        -1: init fail.
  */
int hgic_sdspi_init(void *priv);

/**
  * @brief  hgic sdspi detect device alive.
  * @param  priv: spi driver private data
  * @retval Response result
  */
int hgic_sdspi_detect_alive(void *priv);

/**
  * @brief  hgic sdspi write data function
  * @param  priv: spi driver private data
            data: Date buffer to write. PS: malloc size need align sdio block size
            len : Data length
  * @retval Real write data length
  */
int hgic_sdspi_write(void *priv, unsigned char *data, unsigned int len);

/**
  * @brief  hgic sdspi read data function.
  * @param  priv: spi driver private data
            buf: Read data buffer
            len: buffer maximum length.
  * @retval Not zero as real read data length. Zero as not data to read.
  */
int hgic_sdspi_read(void *priv, unsigned char *buf, unsigned int len, unsigned int flags);


#endif
