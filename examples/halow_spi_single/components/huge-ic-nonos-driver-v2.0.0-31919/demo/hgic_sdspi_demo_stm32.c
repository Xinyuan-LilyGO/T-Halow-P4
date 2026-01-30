/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "hgic_raw.h"
#include "hgic_sdspi.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#ifdef __GNUC__
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

#define RET_OK                                  (0)
#define RET_ERR                                 (-1)

/* SPI data buffer length */
#define SPI_TX_TEST_LEN                         (1024)
#define SPI_DATA_BUF_LEN                        (1536+32)

#define SPI_TEST_MODE_TX                        (0)
#define SPI_TEST_MODE_TRX                       (1)
#define SPI_TEST_MODE_PEER                      (2)
#define SPI_TEST_MODE                           (SPI_TEST_MODE_TRX)    // (0:TX, 1:TRX, 2:PEER)

#define SPI_READ_MODE_POLLING                   (0)
#define SPI_READ_MODE_IRQ                       (1)
#define SPI_RX_MODE                             (SPI_READ_MODE_IRQ)    // (0:POLLING, 1:IRQ)

#define FLASH_BOOT                              (0)
#define SRAM_BOOT                               (1)
#define WIFI_BOOT_MODE                          (SRAM_BOOT)           // (0:FLASH_BOOT, 1:SRAM_BOOT)

#define TEST_FW_FRAG_SIZE                       (1024)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;
DMA_HandleTypeDef hdma_spi1_tx;
DMA_HandleTypeDef hdma_spi1_rx;
DMA_HandleTypeDef hdma_spi2_tx;
DMA_HandleTypeDef hdma_spi2_rx;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static uint8_t spi_rx_buf[SPI_DATA_BUF_LEN];
static uint8_t spi_tx_buf[SPI_DATA_BUF_LEN];
static uint8_t temp_buf[SPI_DATA_BUF_LEN];
static uint8_t bootdl_buf[TEST_FW_FRAG_SIZE];
static uint8_t ota_buf[TEST_FW_FRAG_SIZE + 28]; // reserve 28 bytes for hdr 
volatile uint8_t sdio_read_flag = 0;
volatile uint8_t dma1_done = 0;
volatile uint8_t dma2_done = 0;
uint8_t dest[] = {0xfa, 0xde, 0x09, 0x8c, 0x6a, 0x28};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
PUTCHAR_PROTOTYPE
{
  /* Place your implementation of fputc here */
  /* e.g. write a character to the EVAL_COM1 and Loop until the end of transmission */
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}

void spidrv_write_read(void *priv, unsigned char *wdata, unsigned char *rdata, unsigned int len)
{
    HAL_SPI_TransmitReceive_DMA(&hspi2, (uint8_t *)wdata, (uint8_t *)rdata, len);
    while (!dma2_done);
    dma2_done = 0;
}

void spidrv_write(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
  if (dma_flag) {
    HAL_SPI_TransmitReceive_DMA(&hspi2, (uint8_t *)data, (uint8_t *)temp_buf, len);
    while (!dma2_done);
    dma2_done = 0;
  } else {
    HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)data, (uint8_t *)temp_buf, len, 0xffff);
  }
}

void spidrv_read(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
  if (dma_flag) {
    memset(temp_buf, 0xff, len);
    HAL_SPI_TransmitReceive_DMA(&hspi2, (uint8_t *)temp_buf, (uint8_t *)data, len);
    while (!dma2_done);
    dma2_done = 0;
  } else {
    HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)temp_buf, (uint8_t *)data, len, 0xffff);
  }
}

void spidrv_cs(void *priv, char enable)
{
  if (enable) {
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET);
  } else {
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);
  }
}

int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int len, char flag)
{
  return 0;
}

/**
  * @brief  Raw send function implementation. API define in hgic_raw.c
  * @param  data: Date buffer to write. PS: malloc size need align sdio block size
            len : Real data length
  * @retval Read handle result
  */
int raw_send(unsigned char *data, unsigned int len)
{
  return hgic_sdspi_write(0, data, len);
}

static void random_swap(uint8_t *buff, uint32_t len)
{
    uint32_t a = rand() % len;
    uint32_t b = rand() % len;
    uint8_t temp = buff[a];
    buff[a] = buff[b];
    buff[b] = temp;
}

static unsigned short spi_flash_read_id(void)
{
  unsigned char temp[] = {0x90, 0x00, 0x00, 0x00, 0xff, 0xff};
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
  HAL_SPI_TransmitReceive_DMA(&hspi1, temp, temp, sizeof(temp));
  while (!dma1_done);
  dma1_done = 0;
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
  return (temp[4] << 8) | temp[5];
}

static unsigned short spi_flash_read(unsigned char *data, unsigned int addr, unsigned int len)
{
  unsigned char temp[] = {0x03, addr >> 16, addr >> 8, addr};
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
  HAL_SPI_TransmitReceive_DMA(&hspi1, temp, temp, sizeof(temp));
  while (!dma1_done);
  dma1_done = 0;
  HAL_SPI_TransmitReceive_DMA(&hspi1, bootdl_buf, data, len);
  while (!dma1_done);
  dma1_done = 0;
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
  return (temp[4] << 8) | temp[5];
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  uint8_t test2_flag = 1;
  uint8_t boot_flag = 0;
  uint8_t boot_resp = 0;
  uint8_t ota_resp = 0;
  uint32_t test_err = 0;
  uint32_t check_tick;
  uint32_t speed_tick;
  uint32_t restart_tick;
#if (SPI_RX_MODE == SPI_READ_MODE_POLLING)
  uint32_t read_tick;
#endif
#if (WIFI_BOOT_MODE == SRAM_BOOT)
  uint32_t boot_tick;
#endif
  uint32_t ota_tick;
  uint32_t tx_bytes = 0;
  int read_len = 0;
  unsigned char *p_buf;
  unsigned int p_len;
  HGIC_RAW_RX_TYPE resp;
  unsigned int boot_state;
  unsigned int fw_state;
  struct hgic_bootdl bootdl;
  unsigned int ota_state;
  struct hgic_ota ota;
  uint16_t id;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_SPI2_Init();
  /* USER CODE BEGIN 2 */
  check_tick = HAL_GetTick();
  speed_tick = HAL_GetTick();
  restart_tick = HAL_GetTick();
#if (SPI_RX_MODE == SPI_READ_MODE_POLLING)
  read_tick = HAL_GetTick();
#endif
#if (WIFI_BOOT_MODE == SRAM_BOOT)
  boot_tick = HAL_GetTick();
#endif
  ota_tick = HAL_GetTick();
  
  id = spi_flash_read_id();
  printf("flash id: %x\r\n", id);
  
  while (hgic_sdspi_init(0) == RET_ERR) {
    printf("Bus init error\r\n");
    HAL_Delay(1000);
  }
  //hgic_raw_get_fwinfo();

#if (WIFI_BOOT_MODE == SRAM_BOOT)
__boot_retry:
  boot_flag = 1;
  boot_state = 0;
  /* Setup fragmemt size */
  bootdl.frag_size = TEST_FW_FRAG_SIZE;
  /* Obtain firmware header and parse firmware info */
  spi_flash_read(bootdl_buf, 0, sizeof(struct hgic_fw_info_hdr));
  if (hgic_bootdl_parse_fw(bootdl_buf, &bootdl) != 0) {
    printf("Bootdl fail\r\n");
    while(1);
  }
  /* Enter boot download command and wait response later */
  fw_state = 0xAA;
  hgic_bootdl_request(HGIC_BOOTDL_CMD_ENTER, &bootdl);
  while (1) {
#if (SPI_RX_MODE == SPI_READ_MODE_POLLING)
    if (HAL_GetTick() - read_tick >= 5) {
      read_tick = HAL_GetTick();
#else
    if (sdio_read_flag) { // flag set in interrupt
      sdio_read_flag = 0;
#endif
      read_len = hgic_sdspi_read(0, spi_rx_buf, sizeof(spi_rx_buf), boot_flag);
      if (read_len > 0) {
        p_buf = spi_rx_buf;
        p_len = read_len;
        resp = hgic_raw_rx(&p_buf, &p_len);
        /* Got some bootdl response */
        if (resp == HGIC_RAW_RX_TYPE_BOOTDL_RESP) {
          if (fw_state == 0xAA) {
            fw_state = p_buf[0]; /*0x0:boot state, 0xff:firmware state*/
          }

          if (p_buf[0] == 0xff) {
            printf("In firmware state\r\n");
            goto __boot_done;
          }
          if (p_buf[0] == 0) {
            boot_resp = 1;
            boot_tick = HAL_GetTick();
          }
        }
      }
    }

    if (fw_state == 0 && boot_resp) {
      boot_resp = 0;
      switch (boot_state) {
        case 0:
          /* Write memory addr and size command, and then wait response */
          if (bootdl.offset + bootdl.frag_size > bootdl.fw_info.fw_size) {
            /* remain size less than fragment size */
            bootdl.frag_size = bootdl.fw_info.fw_size - bootdl.offset;
          }
          hgic_bootdl_request(HGIC_BOOTDL_CMD_WRITE_MEM, &bootdl);
          boot_state = 1;
          break;
        case 1:
          /* After write mem cmd responsed, send firmware data in fragmemt */
          spi_flash_read(bootdl_buf, bootdl.fw_info.hdr_len + bootdl.offset, bootdl.frag_size);
          bootdl.offset += bootdl.frag_size;
          hgic_bootdl_send(bootdl_buf, bootdl.frag_size);
          /* Repate [write mem cmd <-> data] until full firmware transmit */
          if (bootdl.offset < bootdl.fw_info.fw_size) {
            boot_state = 0;
          } else {
            boot_state = 2;
          }
          break;
        case 2:
          /* Run boot cmd and wait response */
          hgic_bootdl_request(HGIC_BOOTDL_CMD_RUN, &bootdl);
          boot_state = 3;
          break;
        case 3:
          /* Need sometime bootup */
          HAL_Delay(500);
          goto __boot_done;
        default:
          break;
      }
    }
    if (HAL_GetTick() - check_tick >= 100) {
      check_tick = HAL_GetTick();
      if (hgic_sdspi_detect_alive(0) == RET_ERR) {
        hgic_sdspi_init(0);
      }
    }
    if (HAL_GetTick() - boot_tick >= 500) {
      boot_tick = HAL_GetTick();
      printf("Boot timeout retry it!\r\n");
      goto __boot_retry;
    }
  }
__boot_done:
  boot_flag = 0;
#endif
  
#if 0 // OTA Demo
__ota_retry:
  ota_state = 0;
  ota_resp = 0xff; // first time no wait resp
  /* Setup fragmemt size */
  ota.frag_size = TEST_FW_FRAG_SIZE;
  /* Obtain firmware header and parse firmware info */
  spi_flash_read(ota_buf, 0, sizeof(struct hgic_fw_info_hdr));
  if (hgic_ota_parse_fw(ota_buf, &ota) != 0) {
    printf("OTA fail\r\n");
    while(1);
  }
  while (1) {
#if (SPI_RX_MODE == SPI_READ_MODE_POLLING)
    if (HAL_GetTick() - read_tick >= 5) {
      read_tick = HAL_GetTick();
#else
    if (sdio_read_flag) { // flag set in interrupt
      sdio_read_flag = 0;
#endif
      read_len = hgic_sdspi_read(0, spi_rx_buf, sizeof(spi_rx_buf), boot_flag);
      if (read_len > 0) {
        p_buf = spi_rx_buf;
        p_len = read_len;
        resp = hgic_raw_rx(&p_buf, &p_len);
        /* Got some ota response */
        if (resp == HGIC_RAW_RX_TYPE_OTA_RESP && *(short *)p_buf/*resp code*/ == 0) {
          ota_resp = 1;
          ota_tick = HAL_GetTick();
        }
      }
    }
    if (ota_resp) {
      ota_resp = 0;
      switch (ota_state) {
        case 0:
          if (ota.offset + ota.frag_size > ota.fw_info.hdr_len + ota.fw_info.fw_size) {
            /* remain size less than fragment size */
            ota.frag_size = ota.fw_info.hdr_len + ota.fw_info.fw_size - ota.offset;
          }
          /* Send firmware data in fragmemt */
          spi_flash_read(ota_buf+28, ota.offset, ota.frag_size); // reserve 28 bytes for hdr
          hgic_ota_send_packet(&ota, ota_buf, ota.frag_size);
          ota.offset += ota.frag_size;
          if (ota.offset >= ota.fw_info.hdr_len + ota.fw_info.fw_size) { // hdr + fw
            ota_state = 1;
          }
          break;
        case 1:
          /* Need sometime bootup */
          HAL_Delay(500);
          goto __ota_done;
        default:
          break;
      }
    }
    if (HAL_GetTick() - check_tick >= 100) {
      check_tick = HAL_GetTick();
      if (hgic_sdspi_detect_alive(0) == RET_ERR) {
        hgic_sdspi_init(0);
      }
    }
    if (ota.offset > ota.frag_size && HAL_GetTick() - ota_tick >= 500) {// first packet need erase flash, maybe need longer time
      ota_tick = HAL_GetTick();
      printf("OTA timeout retry it!\r\n");
      goto __ota_retry;
    }
    if (ota.offset == ota.frag_size && HAL_GetTick() - ota_tick >= 5000) {// first packet need erase flash, maybe need longer time
      ota_tick = HAL_GetTick();
      printf("OTA erase flash timeout retry it!\r\n");
      goto __ota_retry;
    }
  }
__ota_done:
#endif
  
  //hgic_raw_set_mac_filter_en(1);
  //HAL_Delay(1);
  //hgic_raw_send(dest, "123456789ABCDEFghijklm", 22);
  
  // random data seed
  srand(0);
  for (uint32_t i = 0; i < SPI_TX_TEST_LEN; ++i) {
    spi_tx_buf[i] = rand() & 0xff;
    //spi_tx_buf[i] = 0x55;
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if (SPI_RX_MODE == SPI_READ_MODE_POLLING)
    if (HAL_GetTick() - read_tick >= 5) {
        read_tick = HAL_GetTick();
#else
    if (sdio_read_flag) { // flag set in interrupt
        sdio_read_flag = 0;
#endif
        read_len = hgic_sdspi_read(0, spi_rx_buf, sizeof(spi_rx_buf), boot_flag);
        if (read_len > 0) {
          p_buf = spi_rx_buf;
          p_len = read_len;
          resp = hgic_raw_rx(&p_buf, &p_len);
          if (resp == HGIC_RAW_RX_TYPE_DATA) {
#if (SPI_TEST_MODE == SPI_TEST_MODE_TRX)
            if (memcmp(spi_tx_buf, p_buf, p_len) != 0) {
              test_err++;
              HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_0);
            } else {
              tx_bytes += 2*SPI_TX_TEST_LEN;
            }
            //random_swap(spi_tx_buf, SPI_TX_TEST_LEN);
            test2_flag = 1;
#elif (SPI_TEST_MODE == SPI_TEST_MODE_PEER)
            p_buf += 14;
            p_len -= 14;
            if (memcmp(spi_tx_buf, p_buf, p_len) != 0) {
              HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_0);
              test_err++;
            } else {
              tx_bytes += 2*SPI_TX_TEST_LEN;
            }
            //random_swap(spi_tx_buf, SPI_TX_TEST_LEN);
            test2_flag = 1;
#endif
          }
        }
    }
    if (HAL_GetTick() - check_tick >= 100) {
      check_tick = HAL_GetTick();
      if (hgic_sdspi_detect_alive(0) == RET_ERR) {
        hgic_sdspi_init(0);
      }
    }
    if (HAL_GetTick() - speed_tick >= 1000) {
      speed_tick = HAL_GetTick();
      printf("%d KBytes/s, err:%d\r\n", tx_bytes/1024, test_err);
      hgic_raw_get_connect_state();
      tx_bytes = 0;
    }
    if (HAL_GetTick() - restart_tick >= 5000) {
      restart_tick = HAL_GetTick();
      printf("Restart speed test\r\n");
      test2_flag = 1;
    }

    if (test2_flag == 1) {
      test2_flag = 0;
#if (SPI_TEST_MODE == SPI_TEST_MODE_TRX)
      hgic_raw_test2(spi_tx_buf, SPI_TX_TEST_LEN); // for test
#elif (SPI_TEST_MODE == SPI_TEST_MODE_TX)
      hgic_raw_test((char *)spi_tx_buf, SPI_TX_TEST_LEN); // for test
      tx_bytes += SPI_TX_TEST_LEN;
#else
      hgic_raw_send("\xff\xff\xff\xff\xff\xff", (char *)spi_tx_buf, SPI_TX_TEST_LEN); // for test
#endif
      restart_tick = HAL_GetTick();
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.Prediv1Source = RCC_PREDIV1_SOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  RCC_OscInitStruct.PLL2.PLL2State = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure the Systick interrupt time 
  */
  __HAL_RCC_PLLI2S_ENABLE();
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/** 
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void) 
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
  /* DMA1_Channel3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
  /* DMA1_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
  /* DMA1_Channel5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : FLASH_CS_Pin */
  GPIO_InitStruct.Pin = FLASH_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(FLASH_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI_CS_Pin */
  GPIO_InitStruct.Pin = SPI_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI_INT_Pin */
  GPIO_InitStruct.Pin = SPI_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(SPI_INT_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == SPI_INT_Pin) {
    sdio_read_flag = 1;
  }
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi == &hspi1) {
    dma1_done = 1;
  } else if (hspi == &hspi2) {
    dma2_done = 1;
  }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi == &hspi1) {
    dma1_done = 1;
  } else if (hspi == &hspi2) {
    dma2_done = 1;
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */

  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
