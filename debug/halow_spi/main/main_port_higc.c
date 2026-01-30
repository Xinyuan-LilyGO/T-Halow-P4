
// #include "hgic_sdspi.h"

void spidrv_write_read(void *priv, unsigned char *wdata, unsigned char *rdata, unsigned int len)
{
}

void spidrv_write(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
}

void spidrv_read(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
}

void spidrv_cs(void *priv, char enable)
{
}

int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int len, char flag)
{
    return 0;
}

// int hgic_sdspi_init(void *priv);

// int hgic_sdspi_detect_alive(void *priv);

// int hgic_sdspi_write(void *priv, unsigned char *data, unsigned int len);

// int hgic_sdspi_read(void *priv, unsigned char *buf, unsigned int len, unsigned int flags);
