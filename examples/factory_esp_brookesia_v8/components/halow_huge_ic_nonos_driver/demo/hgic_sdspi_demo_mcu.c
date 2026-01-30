/**
  ******************************************************************************
  * @file    User/LL/user.c
  * @author  HUGE-IC Application Team
  * @version V1.0.0
  * @date    01-07-2019
  * @brief   Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2019 HUGE-IC</center></h2>
  *
  *
  ******************************************************************************
  */ 

/* Includes ------------------------------------------------------------------*/
#include "include.h"
#include "user.h"
#include "hgic_raw.h"
#include "hgic_sdspi.h"

/** @addtogroup Template_Project
  * @{
  */
  
/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Function return result */
#define RET_OK                                  (0)
#define RET_ERR                                 (-1)
/* UART configuration */
#define PRINTF_UART_BAUDRATE                    (115200)
#define PRINTF_UART_SEL                         (UART1)
#define PRINTF_UART_TX_GPIO_PORT                (GPIOE)
#define PRINTF_UART_TX_GPIO_PIN                 (0)
#define PRINTF_UART_TX_GPIO_ALTER_FUNC          (LL_GPIO_PIN_FUNC_SEL_2)
#define PRINTF_DEBUG_UART_SEL                   (DEBUG_UART1)

/* SPI configuration */
#define SPI_BAUDRATE                            (8000000)
/* SPI data buffer length */
#define SPI_DATA_BUF_LEN                        (512+32)

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static uint8_t spi_rx_buf[SPI_DATA_BUF_LEN];
static uint8_t spi_tx_buf[SPI_DATA_BUF_LEN];
volatile uint8_t hgic_sdspi_read_flag = 0;

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Uart initialization function
  * @param  None
  * @retval None
  */
void uart_init(void)
{ 
    /* UART initialization structure variable */
    TYPE_LL_UART_INIT init;
    
    memset(&init, 0x0, sizeof(TYPE_LL_UART_INIT)); 
    
    /* Reset the UART module to enable the clock. */
    ll_uart_init(PRINTF_UART_SEL, &init);
}

/**
  * @brief  Uart Configuration function
  * @param  None
  * @retval None
  */
void uart_config(void)
{
    /* UART configuration structure variable */
    TYPE_LL_UART_CFG uart_cfg;
    
    /* Initialize the relevant variables of the UART */
    memset(&uart_cfg, 0x0, sizeof(uart_cfg));

    /* Set UART GPIO's pin function. */
    ll_gpio_bit_set(PRINTF_UART_TX_GPIO_PORT, BIT(PRINTF_UART_TX_GPIO_PIN));
    ll_gpio_pull(PRINTF_UART_TX_GPIO_PORT, BIT(PRINTF_UART_TX_GPIO_PIN), LL_GPIO_PUSH_PULL);
    ll_gpio_pin_func_set(PRINTF_UART_TX_GPIO_PORT, BIT(PRINTF_UART_TX_GPIO_PIN), PRINTF_UART_TX_GPIO_ALTER_FUNC);
    
    /* Configure information about the UART */
    uart_cfg.baudrate              = (SYS_CLK/PRINTF_UART_BAUDRATE) - 1;
    uart_cfg.bit_width_sel         = LL_UART_WORD_LENGTH_8B;
    uart_cfg.stop_bit_sel          = LL_UART_STOP_1B;
    uart_cfg.parity                = LL_UART_PARITY_NO;
    uart_cfg.mode                  = LL_UART_FULL_DUPLEX;
    uart_cfg.timeout_en            = false;
    uart_cfg.carrier_output_en     = false;
    uart_cfg.tx_signal_invert_flag = false;
    uart_cfg.rx_signal_invert_flag = false;
    
    /* Call the UART driver configuration function */
    ll_uart232_config(PRINTF_UART_SEL, &uart_cfg);
    
    /* Configure the debug interface of the printf function. */
    debug_select_interface(PRINTF_DEBUG_UART_SEL);
}

/**
  * @brief  UART detele init function
  * @param  None
  * @retval None
  */
void uart_deinit(void)
{
    ll_uart_deinit(PRINTF_UART_SEL);
}

/**
  * @brief  SPI initialization function
  * @param  None
  * @retval None
  */
void spi_init(void)
{
    TYPE_LL_SPI_INIT init;
    
    NVIC_SetPriority(SPI1_IIC1_IRQn, 0);
    NVIC_EnableIRQ(SPI1_IIC1_IRQn);
    
    memset(&init, 0x0, sizeof(TYPE_LL_IIC_INIT));
    ll_spi_init(SPI1, &init);
}

/**
  * @brief  SPI Configuration function
  * @param  None
  * @retval None
  */
void spi_config(void)
{
    TYPE_LL_SPI_CFG spi_cfg;
    /* SPI1 use pins:PB5/PB6/PB7/PB8 */
    ll_gpio_pin_func_set(GPIOB, (BIT(5) | BIT(6) | BIT(7) | BIT(8)), LL_GPIO_PIN_FUNC_SEL_1);
    
    memset(&spi_cfg, 0x0, sizeof(TYPE_LL_SPI_CFG));
    
    spi_cfg.frame_size  = LL_SPI_FRAME_SIZE_8_BITS;
    spi_cfg.wire_mode   = LL_SPI_CON0_NORMAL_MODE;
    spi_cfg.spi_mode    = LL_SPI_CON0_MODE_0;
    spi_cfg.work_mode   = LL_SPI_CON1_MASTER_MODE;
    spi_cfg.clk_div_cnt = SYS_CLK/SPI_BAUDRATE/2 - 1;
    spi_cfg.cs_en       = true;
    
    ll_spi_config(SPI1, &spi_cfg);
    
    /* PB9 as interrupt pin */
    ll_gpio_dir(GPIOB, BIT(9), LL_GPIO_PIN_INPUT);
    ll_gpio_irq_config(GPIOB, BIT(9), ENABLE);
    NVIC_SetPriority(GPIOB_IRQn, 0);
    NVIC_EnableIRQ(GPIOB_IRQn);
}

/**
  * @brief  SPI data handle function
  * @param  None
  * @retval None
  */
void user_handle(void)
{
    uint8_t test2_flag = 1;
    uint32_t seed = 0;
    uint32_t test_err = 0;
    uint32_t check_tick = 0;
    uint32_t speed_tick = 0;
    uint32_t restart_tick = 0;
    uint32_t tx_bytes = 0;
    int read_len = 0;
    unsigned char *p_buf;
    unsigned int p_len;
    
    spi_init();
    spi_config();
    system_tick_init();
    
    while (hgic_sdspi_init(0) == RET_ERR) {
        printf("Bus init error\r\n");
        delay_ms(1000);
    }
    
    check_tick = get_system_tick();
    speed_tick = get_system_tick();
    restart_tick = get_system_tick();
    
    while (1) {
        if (hgic_sdspi_read_flag) { // flag set in interrupt
            hgic_sdspi_read_flag = 0;
            read_len = hgic_sdspi_read(0, spi_rx_buf, sizeof(spi_rx_buf));
            if (read_len != RET_ERR && read_len > 0) {
                p_buf = spi_rx_buf;
                p_len = read_len;
                if (hgic_raw_rx(&p_buf, &p_len) == HGIC_RAW_RX_TYPE_DATA) {
                    if (memcmp(spi_tx_buf, p_buf, p_len) != 0) {
                        test_err++;
                    } else {
                        tx_bytes += 1024;
                    }
                    // random data seed
                    srand(seed++);
                    for (uint32_t i = 0; i < 512; ++i) {
                        spi_tx_buf[i] = rand() & 0xff;
                        //spi_tx_buf[i] = 0x55;
                    }
                    test2_flag = 1;
                }
            }
        }
        if (is_systick_expired(check_tick, 100)) {
            check_tick = get_system_tick();
            if (hgic_sdspi_detect_alive(0) == RET_ERR) {
                hgic_sdspi_init(0);
            }
        }
        if (is_systick_expired(speed_tick, 1000)) {
            speed_tick = get_system_tick();
            printf("%d KBytes/s, err:%d\r\n", tx_bytes/1024, test_err);
            tx_bytes = 0;
        }
        if (is_systick_expired(restart_tick, 5000)) {
            restart_tick = get_system_tick();
            printf("Restart speed test\r\n");
            test2_flag = 1;
        }
        
        if (test2_flag == 1) {
            test2_flag = 0;
            hgic_raw_test2(spi_tx_buf, 512); // for test
            restart_tick = get_system_tick();
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////
int raw_send(unsigned char *data, unsigned int len)
{
    return hgic_sdspi_write(0, data, len);
}

void spidrv_write(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
    ll_spi_buf_tx(SPI1, data, len);
}

void spidrv_read(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
    if (dma_flag) {
        TYPE_LL_SPI_DMA_CFG spi_dma_cfg;
        spi_dma_cfg.dma_addr  = (u32)data;
        spi_dma_cfg.dma_len   = len;
        spi_dma_cfg.dir       = LL_SPI_CON1_RX;
        ll_spi_dma_config(SPI1, &spi_dma_cfg);
        ll_spi_dma_start(SPI1);
        while(!LL_SPI_GET_DMA_PENDING(SPI1));
        ll_spi_clear_dma_pending(SPI1);
    } else {
        ll_spi_buf_rx(SPI1, data, len);
    }
}

void spidrv_cs(void *priv, char cs)
{
    if (cs) {
        ll_spi_clear_cs(SPI1);
    } else {
        ll_spi_set_cs(SPI1);
    }
}

int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int len, char flag)
{
    return 0;
}


/**
  * @}
  */

/*************************** (C) COPYRIGHT 2019 HUGE-IC ***** END OF FILE *****/
