#include "typesdef.h"
#include "list.h"
#include "dev.h"
#include "osal/string.h"
#include "osal/semaphore.h"
#include "osal/task.h"
#include "osal/sleep.h"
#include "hal/gpio.h"
#include "lib/ticker_api.h"
#if (MCU_FUNCTION == 1)
//#define P824M_ADC

#define P824M_OUTPUT_IO             PB_7
#define P824M_SETUP_IO              PB_4

// Configuration Register
#define P824M_SENSITIVITY_SHIFT     (17)
#define P824M_SENSITIVITY_MASK      (0xff << 17)

#define P824M_BLIND_TIME_SHIFT      (13)
#define P824M_BLIND_TIME_MASK       (0xf << 13)

#define P824M_PULSE_CNT_SHIFT       (11)
#define P824M_PULSE_CNT_MASK        (0x3 << 11)

#define P824M_WINDOW_TIME_SHIFT     (9)
#define P824M_WINDOW_TIME_MASK      (0x3 << 9)

#define P824M_MOTION_DET_SHIFT      (8)
#define P824M_MOTION_DET_MASK       (0x1 << 8)
#define P824M_MOTION_DET_ENABLE     (1)
#define P824M_MOTION_DET_DISABLE    (0)

#define P824M_INT_SRC_SHIFT         (7)
#define P824M_INT_SRC_MASK          (0x1 << 7)
#define P824M_INT_SRC_ADC           (1)
#define P824M_INT_SRC_MOTION        (0)

#define P824M_ADC_SRC_SHIFT         (5)
#define P824M_ADC_SRC_MASK          (0x3 << 5)
#define P824M_ADC_SRC_TEMPERATURE   (3)
#define P824M_ADC_SRC_VOLTAGE       (2)
#define P824M_ADC_SRC_PIR_LPF       (1)
#define P824M_ADC_SRC_PIR_BPF       (0)

#define P824M_PIR_POWER_SHIFT       (4)
#define P824M_PIR_POWER_MASK        (0x1 << 4)
#define P824M_PIR_POWER_DISABLE     (1)
#define P824M_PIR_POWER_ENABLE      (0)


static struct os_task pir_task;
static struct os_semaphore pir_sema;

static int32 p824m_serial_data_output(uint32 *p_data, uint32 *p_reg, uint8 bits)
{
    uint8  data_bits, reg_bits;
    uint32 data = 0, reg = 0;
    // Fclk = 32kHz
    // tFR = 2/Fclk ~= 7us
//    gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_OUTPUT);
//    gpio_set_val(P824M_OUTPUT_IO, 0);
//    delay_us(1);
    gpio_set_val(P824M_OUTPUT_IO, 1);
    delay_us(100);
    // read data
    data_bits = 15;
    reg_bits = (bits > 15) ? bits - 15 : 0;
    while (data_bits--) {
        data <<= 1;
        gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_OUTPUT);
        gpio_set_val(P824M_OUTPUT_IO, 0);
        delay_us(1);
        gpio_set_val(P824M_OUTPUT_IO, 1);
        delay_us(1);
        gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_INPUT);
        gpio_set_val(P824M_OUTPUT_IO, 0);
        delay_us(1);
        data |= gpio_get_val(P824M_OUTPUT_IO);
        //delay_us(1);
    }
    while (reg_bits--) {
        reg <<= 1;
        gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_OUTPUT);
        gpio_set_val(P824M_OUTPUT_IO, 0);
        delay_us(1);
        gpio_set_val(P824M_OUTPUT_IO, 1);
        delay_us(1);
        gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_INPUT);
        gpio_set_val(P824M_OUTPUT_IO, 0);
        delay_us(1);
        reg |= gpio_get_val(P824M_OUTPUT_IO);
        //delay_us(1);
    }
    gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_OUTPUT);
    gpio_set_val(P824M_OUTPUT_IO, 0);
    // 200ns <= tL = tH < tRA = 130us
    delay_us(130);
    data &= ~BIT(14); // clear overflow bit
    if (p_data != NULL) {*p_data = data;}
    if (p_reg != NULL) {*p_reg = reg;}
    
    return RET_OK;
}

static int32 p824m_serial_data_setup(uint32 *p_data, uint8 bits)
{
    uint32 mask = BIT(bits-1);
    uint32 data = *p_data;
    // Fclk = 32kHz
    gpio_set_dir(P824M_SETUP_IO, GPIO_DIR_OUTPUT);
    gpio_set_val(P824M_SETUP_IO, 0);
    delay_us(500);
    // write data
    while (bits--) {
        // 100us ~= 3/Fclk <= tBW <= 4/Fclk = 130us
        gpio_set_val(P824M_SETUP_IO, 0);
        delay_us(1);
        gpio_set_val(P824M_SETUP_IO, 1);
        delay_us(1);
        gpio_set_val(P824M_SETUP_IO, (data & mask) ? 1 : 0);
        data <<= 1;
        delay_us(110);
    }
    // tWL >= 16/Fclk = 500us
    gpio_set_val(P824M_SETUP_IO, 0);
    delay_us(500);
    return RET_OK;
}

static void p824m_interrup_handle(int32 id, enum gpio_irq_event event)
{
    switch (event) {
        case GPIO_IRQ_EVENT_RISE:
            os_sema_up(&pir_sema);
            break;
        case GPIO_IRQ_EVENT_FALL:
            break;
        default:
            break;
    }
}

static void p824m_read_task(void *args)
{
    uint32 pir_output = 0;
    uint32 pir_reg = 0;
#ifdef P824M_ADC
    uint32 pir_reg_setup = (((P824M_PIR_POWER_ENABLE << P824M_PIR_POWER_SHIFT) & P824M_PIR_POWER_MASK) |
                            ((P824M_ADC_SRC_PIR_BPF << P824M_ADC_SRC_SHIFT) & P824M_ADC_SRC_MASK) |
                            ((P824M_INT_SRC_ADC << P824M_INT_SRC_SHIFT) & P824M_INT_SRC_MASK) |
                            ((P824M_MOTION_DET_DISABLE << P824M_MOTION_DET_SHIFT) & P824M_MOTION_DET_MASK) |
                            ((1 << P824M_WINDOW_TIME_SHIFT) & P824M_WINDOW_TIME_MASK) |
                            ((1 << P824M_PULSE_CNT_SHIFT) & P824M_PULSE_CNT_MASK) |
                            ((3 << P824M_BLIND_TIME_SHIFT) & P824M_BLIND_TIME_MASK) |
                            ((128 << P824M_SENSITIVITY_SHIFT) & P824M_SENSITIVITY_MASK));
#else
    uint32 pir_reg_setup = (((P824M_PIR_POWER_ENABLE << P824M_PIR_POWER_SHIFT) & P824M_PIR_POWER_MASK) |
                            ((P824M_ADC_SRC_PIR_BPF << P824M_ADC_SRC_SHIFT) & P824M_ADC_SRC_MASK) |
                            ((P824M_INT_SRC_MOTION << P824M_INT_SRC_SHIFT) & P824M_INT_SRC_MASK) |
                            ((P824M_MOTION_DET_ENABLE << P824M_MOTION_DET_SHIFT) & P824M_MOTION_DET_MASK) |
                            ((1 << P824M_WINDOW_TIME_SHIFT) & P824M_WINDOW_TIME_MASK) |
                            ((0 << P824M_PULSE_CNT_SHIFT) & P824M_PULSE_CNT_MASK) |
                            ((7 << P824M_BLIND_TIME_SHIFT) & P824M_BLIND_TIME_MASK) |
                            ((64 << P824M_SENSITIVITY_SHIFT) & P824M_SENSITIVITY_MASK));
#endif
    os_sema_init(&pir_sema, 0);
    gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_INPUT);
    if (gpio_get_val(P824M_OUTPUT_IO)) {
        gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_OUTPUT);
        gpio_set_val(P824M_OUTPUT_IO, 0);
        os_sleep_ms(20); // above 20ms
    }
    p824m_serial_data_setup(&pir_reg_setup, 25);
#ifdef P824M_ADC
    p824m_serial_data_output(&pir_output, &pir_reg, 40);
    os_printf("1)pir reg=%x-%x\r\n", pir_output, pir_reg);
    p824m_serial_data_output(&pir_output, &pir_reg, 40);
    os_printf("2)pir reg=%x-%x\r\n", pir_output, pir_reg);
    p824m_serial_data_output(&pir_output, &pir_reg, 40);
    os_printf("3)pir reg=%x-%x\r\n", pir_output, pir_reg);
    while (pir_reg != pir_reg_setup) {
        os_sleep_ms(20);
        mask = disable_irq();
        p824m_serial_data_setup(&pir_reg_setup, 25);
        p824m_serial_data_output(&pir_output, &pir_reg, 40);
        p824m_serial_data_output(&pir_output, &pir_reg, 40);
        p824m_serial_data_output(&pir_output, &pir_reg, 40);
        enable_irq(mask);
        os_printf("pir reg=%x-%x\r\n", pir_output, pir_reg);
    }
#else
    gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_INPUT);
    gpio_enable_irq(P824M_OUTPUT_IO, 1);
    gpio_request_pin_irq(P824M_OUTPUT_IO, p824m_interrup_handle, 0, GPIO_IRQ_EVENT_RISE);
#endif
    while (1) {
        os_sema_down(&pir_sema, osWaitForever);
        gpio_enable_irq(P824M_OUTPUT_IO, 0);
#ifdef P824M_ADC
        p824m_serial_data_output(&pir_output, &pir_reg, 40);
#else
        gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_OUTPUT);
        gpio_set_val(P824M_OUTPUT_IO, 0);
        os_sleep_ms(20); // above 20ms
        p824m_serial_data_output(&pir_output, NULL, 15);
#endif
        gpio_set_dir(P824M_OUTPUT_IO, GPIO_DIR_INPUT);
        gpio_enable_irq(P824M_OUTPUT_IO, 1);
        //os_printf("pir reg=%x-%x, vol=%dmV\r\n", pir_output, pir_reg, (((pir_output & 0x3fff) - 8000) * 213) >> 15);
        os_printf("pir reg=%x-%x, vol=%dmV\r\n", pir_output, pir_reg, ((pir_output & 0x3fff) * 213) >> 15);
    }
}

int32 p824m_init(void)
{
    OS_TASK_INIT("p824m", &pir_task, p824m_read_task, 0, OS_TASK_PRIORITY_NORMAL, 512);
    return RET_OK;
}
#endif