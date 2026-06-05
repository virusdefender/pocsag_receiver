/* SX1276 HAL adaptor — SPI/GPIO wrappers for libbch_pocsag FSK driver */
#include "radio.h"
#include "stm32f1xx_hal.h"
#include "sx1276-Hal.h"
#include "sx1276.h"

extern SPI_HandleTypeDef hspi1;

/* ==================== SX1276 HAL interface ==================== */

void SX1276InitIo(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = { 0 };

    /* DIO0: PB1 PayloadReady interrupt */
    gpio.Pin = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_IT_RISING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_NVIC_SetPriority(EXTI1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);

    /* DIO1: PA3 FifoLevel interrupt */
    gpio.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_NVIC_SetPriority(EXTI3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);

    /* SPI_CS: PA4 output */
    gpio.Pin = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

    /* RESET: PB0 output */
    gpio.Pin = GPIO_PIN_0;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);

    /* TXEN: PB12, RXEN: PB13 */
    gpio.Pin = GPIO_PIN_12 | GPIO_PIN_13;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
}

void SX1276SetReset(uint8_t state)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,
        state == RADIO_RESET_ON ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void SX1276Write(uint8_t addr, uint8_t data)
{
    SX1276WriteBuffer(addr, &data, 1);
}

void SX1276Read(uint8_t addr, uint8_t* data)
{
    SX1276ReadBuffer(addr, data, 1);
}

void SX1276WriteBuffer(uint8_t addr, uint8_t* buffer, uint8_t size)
{
    uint8_t cmd = addr | 0x80;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 0xFFFF);
    HAL_SPI_Transmit(&hspi1, buffer, size, 0xFFFF);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

void SX1276ReadBuffer(uint8_t addr, uint8_t* buffer, uint8_t size)
{
    uint8_t cmd = addr & 0x7F;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 0xFFFF);
    HAL_SPI_Receive(&hspi1, buffer, size, 0xFFFF);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

void SX1276WriteFifo(uint8_t* buffer, uint8_t size)
{
    SX1276WriteBuffer(0, buffer, size);
}

void SX1276ReadFifo(uint8_t* buffer, uint8_t size)
{
    SX1276ReadBuffer(0, buffer, size);
}

uint8_t SX1276ReadDio0(void) { return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1); }

uint8_t SX1276ReadDio1(void) { return 0; }
uint8_t SX1276ReadDio2(void) { return 0; }
uint8_t SX1276ReadDio3(void) { return 0; }
uint8_t SX1276ReadDio4(void) { return 0; }
uint8_t SX1276ReadDio5(void) { return 0; }

void SX1276WriteRxTx(uint8_t txEnable)
{
    if (txEnable) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
    }
}

uint32_t SX1276GetTickCounter(void) { return HAL_GetTick(); }
