#ifndef _HGIC_SPI_SLAVE_H_
#define _HGIC_SPI_SLAVE_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined ( __CC_ARM )
#pragma anon_unions
#endif

#define HGIC_SPI_WIRE_MODE_NORMAL 0x00
#define HGIC_SPI_WIRE_MODE_3_WIRE 0x01
#define HGIC_SPI_WIRE_MODE_DUAL   0x02
#define HGIC_SPI_WIRE_MODE_QUAD   0x03

/**
  * @brief  SPI Driver write function
  * @param  priv: spi driver private data
            data: Data buffer to be Tx
            len : Data length
  * @retval None
  */
extern void spidrv_write(void *priv, unsigned char *data, unsigned int len);

/**
  * @brief  SPI Driver read function
  * @param  priv: spi driver private data
            data: Data buffer to be Rx
            len : Data length
  * @retval None
  */
extern void spidrv_read(void *priv, unsigned char *data, unsigned int len);

/**
  * @brief  SPI Driver CS function
  * @param  priv: spi driver private data
            enable: Not zero as CS line enableed (low),
                    Zero as CS line disableed (high).
  * @retval None
  */
extern void spidrv_cs(void *priv, char enable);

/**
  * @brief  hgic spi slave write data function
  * @param  priv: spi driver private data
            wire_mode: wire mode to switch
  * @retval None
  */
void hgic_spi_slave_wire_mode(void *priv, unsigned char wire_mode);

/**
  * @brief  hgic spi slave write data function
  * @param  priv: spi driver private data
            data: Date buffer to write.
            len : Data length
  * @retval Real write data length
  */
int hgic_spi_slave_write(void *priv, unsigned char *data, unsigned int len);

/**
  * @brief  hgic spi slave read data function.
  * @param  priv: spi driver private data
            buf: Read data buffer
            len: buffer maximum length.
  * @retval Not zero as real read data length. Zero as not data to read.
  */
int hgic_spi_slave_read(void *priv, unsigned char *buf, unsigned int len);


#endif
