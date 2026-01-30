#include "typesdef.h"
#include "list.h"
#include "dev.h"
#include "osal/string.h"
#include "osal/sleep.h"
#include "hal/gpio.h"
#include "lib/ticker_api.h"

#if (MCU_FUNCTION == 1)
#define I2C_GPIO_SCL                    PB_6
#define I2C_GPIO_SDA                    PB_5
#define I2C_DELAY                       (3)

#define CW2015_SLAVE_ADDRESS            0x62

#define CW2015_REG_VERSION              0x00
#define CW2015_REG_VCELL                0x02
#define CW2015_REG_SOC                  0x04
#define CW2015_REG_RTT_ALRT             0x06
#define CW2015_REG_CONFIG               0x08
#define CW2015_REG_MODE                 0x0A
#define CW2015_REG_VTEMPL               0x0C
#define CW2015_REG_VTEMPH               0x0D
#define CW2015_REG_BATINFO              0x10

#define CW2015_MODE_SLEEP_MASK          (0x3 << 6)
#define CW2015_MODE_SLEEP               (0x3 << 6)
#define CW2015_MODE_NORMAL              (0x0 << 6)
#define CW2015_MODE_QUICK_START         (0x3 << 4)
#define CW2015_MODE_RESTART             (0xf << 0)

#define CW2015_CONFIG_UPDATE_FLG        (0x1 << 1)
#define CW2015_ATHD(n)                  ((n) << 3) // ATHD = n%

#define BATTERY_UP_MAX_CHANGE           (420*1000) // The time for add capacity when charging
#define BATTERY_DOWN_MAX_CHANGE         (120*1000)
#define BATTERY_JUMP_TO_ZERO            (30*1000)
#define BATTERY_CAPACITY_ERROR          (40*1000)
#define BATTERY_CHARGING_ZERO           (1800*1000)

#define CHARGING_ON 1
#define NO_CHARGING 0

#define SIZE_BATINFO 64
static uint8 config_info[SIZE_BATINFO] = {
    0x15,0x7E,0x00,0x86,0x50,0x34,0x28,0x1A,
    0x15,0x19,0x22,0x25,0x25,0x24,0x2E,0x43,
    0x48,0x43,0x37,0x2F,0x39,0x48,0x52,0x52,
    0x40,0x6E,0x0C,0xCD,0x8C,0xC4,0xC2,0xD4,
    0xDB,0xCB,0xC9,0xD0,0x42,0x1B,0x79,0x4D,
    0x0A,0x42,0x3D,0x77,0x8C,0x91,0x92,0x3B,
    0x59,0x83,0x96,0xA2,0x80,0xFF,0xFF,0xCB,
    0x2F,0x00,0x64,0xA5,0xB5,0x1F,0xE0,0x09,
};

static uint8 temp_info[SIZE_BATINFO];

static inline void i2c_gpio_scl_set_val(int32 value)
{
    gpio_set_val(I2C_GPIO_SCL, value);
}

static inline void i2c_gpio_sda_set_val(int32 value)
{
    gpio_set_val(I2C_GPIO_SDA, value);
}

static inline int32 i2c_gpio_sda_get_val(void)
{
    return gpio_get_val(I2C_GPIO_SDA);
}

static inline void i2c_gpio_scl_set_dir(int32 dir)
{
    gpio_set_dir(I2C_GPIO_SCL, dir);
}

static inline void i2c_gpio_sda_set_dir(int32 dir)
{
    gpio_set_dir(I2C_GPIO_SDA, dir);
}

static inline void i2c_gpio_delay(void)
{
    delay_us(I2C_DELAY);
}

static void i2c_gpio_start(void)
{
    i2c_gpio_scl_set_dir(GPIO_DIR_OUTPUT);
    i2c_gpio_sda_set_dir(GPIO_DIR_OUTPUT);

    i2c_gpio_sda_set_val(1);
    i2c_gpio_scl_set_val(1);

    i2c_gpio_delay();
    i2c_gpio_sda_set_val(0); // pull low sda when scl in high enter start
    i2c_gpio_delay();
    i2c_gpio_scl_set_val(0);
}

static void i2c_gpio_stop(void)
{
    i2c_gpio_scl_set_dir(GPIO_DIR_OUTPUT);
    i2c_gpio_sda_set_dir(GPIO_DIR_OUTPUT);

    i2c_gpio_sda_set_val(0);
    i2c_gpio_scl_set_val(1);

    i2c_gpio_delay();
    i2c_gpio_sda_set_val(1); // pull high sda when scl in high enter stop
    i2c_gpio_delay();
}

static int32 i2c_gpio_wait_ack(uint32 timeout)
{
    i2c_gpio_scl_set_dir(GPIO_DIR_OUTPUT);
    i2c_gpio_sda_set_dir(GPIO_DIR_INPUT);

    //i2c_gpio_scl_set_val(0);
    //i2c_gpio_delay();
    i2c_gpio_scl_set_val(1);
    i2c_gpio_delay();

    while (i2c_gpio_sda_get_val()) {
        if (timeout-- > 0) {
            i2c_gpio_delay();
        } else {
            i2c_gpio_scl_set_val(0);
            i2c_gpio_delay();
            i2c_gpio_stop();
            return RET_ERR;
        }
    }
    i2c_gpio_scl_set_val(0);
    i2c_gpio_delay();
    return RET_OK;
}

static void i2c_gpio_do_ack(void)
{
    i2c_gpio_scl_set_dir(GPIO_DIR_OUTPUT);
    i2c_gpio_sda_set_dir(GPIO_DIR_OUTPUT);

    i2c_gpio_sda_set_val(0);
    i2c_gpio_delay();
    i2c_gpio_scl_set_val(1);
    i2c_gpio_delay();
    i2c_gpio_scl_set_val(0);
    i2c_gpio_delay();
    i2c_gpio_sda_set_val(1);
}

static void i2c_gpio_do_noack(void)
{
    i2c_gpio_scl_set_dir(GPIO_DIR_OUTPUT);
    i2c_gpio_sda_set_dir(GPIO_DIR_OUTPUT);

    i2c_gpio_sda_set_val(1);
    i2c_gpio_delay();
    i2c_gpio_scl_set_val(1);
    i2c_gpio_delay();
    i2c_gpio_scl_set_val(0);
    i2c_gpio_delay();
    //i2c_gpio_sda_set_val(1);
}

static void i2c_gpio_write_byte(uint8 byte)
{
    i2c_gpio_scl_set_dir(GPIO_DIR_OUTPUT);
    i2c_gpio_sda_set_dir(GPIO_DIR_OUTPUT);

    for (uint8 i = 0; i < 8; ++i) {
        i2c_gpio_sda_set_val((byte & 0x80) ? 1 : 0); // MSB
        byte <<= 1;
        i2c_gpio_scl_set_val(1);
        i2c_gpio_delay();
        i2c_gpio_scl_set_val(0);
        i2c_gpio_delay();
    }
}

static uint8 i2c_gpio_read_byte(void)
{
    uint8 byte = 0;
    
    i2c_gpio_scl_set_dir(GPIO_DIR_OUTPUT);
    i2c_gpio_sda_set_dir(GPIO_DIR_INPUT);

    for (uint8 i = 0; i < 8; ++i) {
        byte <<= 1;
        i2c_gpio_scl_set_val(1);
        i2c_gpio_delay();
        byte |= i2c_gpio_sda_get_val();
        i2c_gpio_scl_set_val(0);
        i2c_gpio_delay();
    }
    return byte;
}

static int32 i2c_gpio_write_data(uint8 slave_addr, uint8 *data, uint32 size)
{
    i2c_gpio_start();
    i2c_gpio_write_byte((slave_addr << 1) & 0xfe);
    if (i2c_gpio_wait_ack(3) == RET_ERR) {
        return RET_ERR;
    }
    while (size--) {
        i2c_gpio_write_byte(*data++);
        if (i2c_gpio_wait_ack(3) == RET_ERR) {
            return RET_ERR;
        }
    }
    i2c_gpio_stop();
    
    return RET_OK;
}

static int32 i2c_gpio_read_data(uint8 slave_addr, uint8 *data, uint32 size)
{
    i2c_gpio_start();
    i2c_gpio_write_byte((slave_addr << 1) | 0x01);
    if (i2c_gpio_wait_ack(3) == RET_ERR) {
        return RET_ERR;
    }
    while (size-- > 1) {
        *data++ = i2c_gpio_read_byte();
        i2c_gpio_do_ack();
    }
    // last byte
    *data = i2c_gpio_read_byte();
    i2c_gpio_do_noack();
    i2c_gpio_stop();
    
    return RET_OK;
}

static int32 i2c_gpio_write_reg(uint8 slave_addr, uint8 reg, uint8 *data, uint32 size)
{
    i2c_gpio_start();
    i2c_gpio_write_byte((slave_addr << 1) & 0xfe);
    if (i2c_gpio_wait_ack(3) == RET_ERR) {
        return RET_ERR;
    }
    i2c_gpio_write_byte(reg);
    if (i2c_gpio_wait_ack(3) == RET_ERR) {
        return RET_ERR;
    }
    while (size--) {
        i2c_gpio_write_byte(*data++);
        if (i2c_gpio_wait_ack(3) == RET_ERR) {
            return RET_ERR;
        }
    }
    i2c_gpio_stop();
    
    return RET_OK;
}

static int32 i2c_gpio_read_reg(uint8 slave_addr, uint8 reg, uint8 *data, uint32 size)
{
    i2c_gpio_start();
    i2c_gpio_write_byte((slave_addr << 1) & 0xfe);
    if (i2c_gpio_wait_ack(3) == RET_ERR) {
        return RET_ERR;
    }
    i2c_gpio_write_byte(reg);
    if (i2c_gpio_wait_ack(3) == RET_ERR) {
        return RET_ERR;
    }
    //restart
    i2c_gpio_start();
    i2c_gpio_write_byte((slave_addr << 1) | 0x01);
    if (i2c_gpio_wait_ack(3) == RET_ERR) {
        return RET_ERR;
    }
    while (size-- > 1) {
        *data++ = i2c_gpio_read_byte();
        i2c_gpio_do_ack();
    }
    // last byte
    *data = i2c_gpio_read_byte();
    i2c_gpio_do_noack();
    i2c_gpio_stop();
    
    return RET_OK;
}

static int32 i2c_gpio_open(void)
{
    gpio_set_mode(I2C_GPIO_SCL, GPIO_OPENDRAIN_PULL_UP, GPIO_PULL_LEVEL_10K);
    gpio_set_mode(I2C_GPIO_SDA, GPIO_OPENDRAIN_PULL_UP, GPIO_PULL_LEVEL_10K);
    
    i2c_gpio_scl_set_dir(GPIO_DIR_OUTPUT);
    i2c_gpio_sda_set_dir(GPIO_DIR_OUTPUT);
    
    i2c_gpio_scl_set_val(1);
    i2c_gpio_sda_set_val(1);
    
    return RET_OK;
}

static int32 cw2015_update_config_info(uint8 alert_thd)
{
    uint8 reg_val, reset_val;
    
    // make sure no in sleep mode
    if (i2c_gpio_read_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_MODE, &reg_val, 1) == RET_ERR) {
        return RET_ERR;
    }
    reset_val = reg_val;
    if((reg_val & CW2015_MODE_SLEEP_MASK) == CW2015_MODE_SLEEP) {
        return RET_ERR;
    }
    
    // update new battery info
    if (i2c_gpio_write_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_BATINFO, config_info, SIZE_BATINFO) == RET_ERR) {
        return RET_ERR;
    }
    reg_val |= CW2015_CONFIG_UPDATE_FLG;    // set UPDATE_FLAG
    reg_val &= 0x07;                        // clear ATHD
    reg_val |= CW2015_ATHD(alert_thd);                 // set ATHD
    if (i2c_gpio_write_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_CONFIG, &reg_val, 1) == RET_ERR) {
        return RET_ERR;
    }
    // read back and check
    if (i2c_gpio_read_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_CONFIG, &reg_val, 1) == RET_ERR) {
        return RET_ERR;
    }
    
    if (!(reg_val & CW2015_CONFIG_UPDATE_FLG)) {
        os_printf("Error: The new config set fail\r\n");
        //return RET_ERR;
    }

    if ((reg_val & 0xf8) != CW2015_ATHD(alert_thd)) {
        os_printf("Error: The new ATHD set fail\r\n");
        //return RET_ERR;
    }
    
    // reset
    reset_val &= ~(CW2015_MODE_RESTART);
    reg_val = reset_val | CW2015_MODE_RESTART;
    if (i2c_gpio_write_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_MODE, &reg_val, 1) == RET_ERR) {
        return RET_ERR;
    }
    
    os_sleep_ms(20);
    
    if (i2c_gpio_write_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_MODE, &reset_val, 1) == RET_ERR) {
        return RET_ERR;
    }
    
    return RET_OK;
}

int32 cw2015_init(uint8 alert_thd)
{
    uint8 reg_val = CW2015_MODE_NORMAL;
    i2c_gpio_open();
    if (i2c_gpio_write_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_MODE, &reg_val, 1) == RET_ERR) { // wake up cw2015
        return RET_ERR;
    }
    if (i2c_gpio_read_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_CONFIG, &reg_val, 1) == RET_ERR) {
        return RET_ERR;
    }
    if ((reg_val & 0xf8) != CW2015_ATHD(alert_thd)) {
        reg_val &= 0x07;    /* clear ATHD */
        reg_val |= CW2015_ATHD(alert_thd);    /* set ATHD */
        if (i2c_gpio_write_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_CONFIG, &reg_val, 1) == RET_ERR) {
            return RET_ERR;
        }
    }
    if (i2c_gpio_read_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_CONFIG, &reg_val, 1) == RET_ERR) {
        return RET_ERR;
    }
    if (!(reg_val & CW2015_CONFIG_UPDATE_FLG)) {
        if (cw2015_update_config_info(alert_thd) == RET_ERR) {
            os_printf("update config fail\r\n");
            return RET_ERR;
        }
    } else {
        if (i2c_gpio_read_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_BATINFO, temp_info, SIZE_BATINFO) == RET_ERR) {
            return RET_ERR;
        }
        if (os_memcmp(config_info, temp_info, SIZE_BATINFO) != 0) {
            os_printf("config didn't match, need update config\r\n");
            if (cw2015_update_config_info(alert_thd) == RET_ERR) {
                os_printf("update config fail\r\n");
                return RET_ERR;
            }
        }
    }
    
    return RET_OK;
}

int32 cw2015_get_voltage(uint16 *vcell, uint16 *rtt, uint16 *soc)
{
    //uint8 reg_val;
    uint8 battery_vol[2] = {0};
    uint8 battery_soc[2] = {0};
    uint8 battery_rtt[2] = {0};
//    uint8 i;
    i2c_gpio_open();
#if 1
    if (i2c_gpio_read_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_VCELL, battery_vol, 2) == RET_ERR) {
        return RET_ERR;
    }
    *vcell = ((battery_vol[0] << 8) | battery_vol[1]) * 305 / 1000;
    //os_printf("bat vol:%dmV\r\n", ((battery_vol[0] << 8) | battery_vol[1]) * 305 / 1000);
    if (i2c_gpio_read_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_RTT_ALRT, battery_rtt, 2) == RET_ERR) {
        return RET_ERR;
    }
    *rtt = (battery_rtt[0] << 8) | battery_rtt[1];
    //os_printf("bat rtt:%dmin\r\n", (battery_rtt[0] << 8) | battery_rtt[1]);
    if (i2c_gpio_read_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_SOC, battery_soc, 2) == RET_ERR) {
        return RET_ERR;
    }
    *soc = battery_soc[0];
//    if (i >= 3) {
//        reg_val = CW2015_MODE_SLEEP;
//        if (i2c_gpio_write_reg(CW2015_SLAVE_ADDRESS, CW2015_REG_MODE, &reg_val, 1) == RET_ERR) { // sleep cw2015
//            return RET_ERR;
//        }
//        os_printf("power err! enter sleep mode.\r\n");
//    } else {
//        os_printf("bat soc:%d%%\r\n", battery_soc[0]);
//    }
#endif
    return RET_OK;
}
#endif
