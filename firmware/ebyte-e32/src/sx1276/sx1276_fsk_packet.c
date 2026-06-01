#include "sx1276_fsk_packet.h"
#include "stm32f1xx_hal.h"
#include "sx1276-Fsk.h"
#include "sx1276-Hal.h"
#include "sx1276.h"

/* ================================================================
 * FSK parameters — constants (per POCSAG spec, not configurable)
 * ================================================================ */

/* ================================================================
 * DIO0 interrupt — used by RX (PayloadReady) and TX (PacketSent)
 * ================================================================ */
volatile uint8_t dio0_irq_fired = 0;

void EXTI1_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_1) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_1);
        dio0_irq_fired = 1;
    }
}

/* ================================================================
 * Init — delegate to SDK, then POCSAG overrides
 * ================================================================ */
void SX1276_FSK_Init(uint32_t freq, uint32_t bitrate, uint32_t fdev,
    uint32_t rx_bw)
{
    extern uint8_t SX1276Regs[];
    extern tFskSettings FskSettings;
    SX1276 = (tSX1276*)SX1276Regs;

    SX1276InitIo();

    /* SX1276InitIo() sets PB1/PA3 as plain INPUT — re-init with EXTI */
    GPIO_InitTypeDef g = { 0 };
    g.Mode = GPIO_MODE_IT_RISING;
    g.Pull = GPIO_PULLUP;
    g.Pin = GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_NVIC_SetPriority(EXTI1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);

    g.Mode = GPIO_MODE_IT_RISING;
    g.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_NVIC_SetPriority(EXTI3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);
    SX1276Reset();
    /* after reset, SX1276 defaults to FSK mode — SetLoRaOn(false) is redundant
     * and its SPI burst breaks I2C; skip it */
    // SX1276SetLoRaOn(false);

    FskSettings.RFFrequency = freq;
    FskSettings.Bitrate = bitrate;
    FskSettings.Fdev = fdev;
    FskSettings.RxBw = rx_bw;
    FskSettings.RxBwAfc = rx_bw;
    FskSettings.CrcOn = false;
    FskSettings.AfcOn = true;
    FskSettings.PayloadLength = 255;

    SX1276FskInit();

    /* POCSAG overrides (SDK defaults differ or are missing) */
    SX1276Write(REG_PACKETCONFIG1, 0x00);
    // 同步字取反，解决FSK极性问题
    SX1276Write(REG_SYNCVALUE1, 0x83);  // 0x7C 取反
    SX1276Write(REG_SYNCVALUE2, 0x2D);  // 0xD2 取反
    SX1276Write(REG_SYNCVALUE3, 0xEA);  // 0x15 取反
    SX1276Write(REG_SYNCVALUE4, 0x27);  // 0xD8 取反
    SX1276Write(REG_PREAMBLELSB, 0x20);
    SX1276Write(REG_DIOMAPPING1, 0x00);
    SX1276Write(REG_RXCONFIG, 0x1E);
    SX1276Write(REG_RXTIMEOUT2, 4); /* 4×16×Tbit≈53ms */
}

/* ================================================================
 * Common helpers
 * ================================================================ */

int16_t SX1276_FSK_ReadRssi(void)
{
    uint8_t val;
    SX1276Read(REG_RSSIVALUE, &val);
    return -(val / 2);
}

void SX1276_FSK_ClearIrq(void)
{
    uint8_t dummy;
    SX1276Read(REG_IRQFLAGS1, &dummy);
    SX1276Read(REG_IRQFLAGS2, &dummy);
}

void SX1276_FSK_ClearDio0Flag(void) { dio0_irq_fired = 0; }

void SX1276_FSK_DumpRegs(uint8_t* ver, uint8_t* pa, uint8_t* op, uint8_t* frf_h,
    uint8_t* frf_m, uint8_t* frf_l, uint8_t* padac)
{
    SX1276Read(0x42, ver);
    SX1276Read(0x09, pa);
    SX1276Read(0x01, op);
    SX1276Read(0x06, frf_h);
    SX1276Read(0x07, frf_m);
    SX1276Read(0x08, frf_l);
    SX1276Read(0x4D, padac);
}
