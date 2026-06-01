#include "u8g2_hal.h"
#include "stm32f1xx_hal.h"

extern I2C_HandleTypeDef hi2c2;

#define OLED_ADDR 0x78

uint8_t u8x8_byte_hw_i2c(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int,
    void* arg_ptr)
{
    uint8_t* data = (uint8_t*)arg_ptr;
    switch (msg) {
    case U8X8_MSG_BYTE_SEND:
        while (arg_int-- > 0) {
            I2C2->DR = *data++;
            while (__HAL_I2C_GET_FLAG(&hi2c2, I2C_FLAG_TXE) == RESET)
                ;
        }
        break;
    case U8X8_MSG_BYTE_INIT:
    case U8X8_MSG_BYTE_SET_DC:
        break;
    case U8X8_MSG_BYTE_START_TRANSFER:
        while (__HAL_I2C_GET_FLAG(&hi2c2, I2C_FLAG_BUSY) == SET)
            ;
        CLEAR_BIT(I2C2->CR1, I2C_CR1_POS);
        SET_BIT(I2C2->CR1, I2C_CR1_START);
        while (__HAL_I2C_GET_FLAG(&hi2c2, I2C_FLAG_SB) == RESET)
            ;
        I2C2->DR = OLED_ADDR;
        while (__HAL_I2C_GET_FLAG(&hi2c2, I2C_FLAG_ADDR) == RESET)
            ;
        __HAL_I2C_CLEAR_ADDRFLAG(&hi2c2);
        while (__HAL_I2C_GET_FLAG(&hi2c2, I2C_FLAG_TXE) == RESET)
            ;
        break;
    case U8X8_MSG_BYTE_END_TRANSFER:
        SET_BIT(I2C2->CR1, I2C_CR1_STOP);
        break;
    default:
        return 0;
    }
    return 1;
}

uint8_t u8x8_gpio_and_delay_hw(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int,
    void* arg_ptr)
{
    switch (msg) {
    case U8X8_MSG_DELAY_MILLI:
        HAL_Delay(arg_int);
        break;
    default:
        u8x8_SetGPIOResult(u8x8, 1);
        break;
    }
    return 1;
}

void u8g2Init(u8g2_t* u8g2)
{
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2, U8G2_R0, u8x8_byte_hw_i2c,
        u8x8_gpio_and_delay_hw);
    u8g2_InitDisplay(u8g2);
    u8g2_SetPowerSave(u8g2, 0);
    u8g2_ClearBuffer(u8g2);
}
