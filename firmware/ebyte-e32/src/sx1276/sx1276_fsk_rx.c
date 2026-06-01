#define RX_PKT_PAYLOAD_MAX 128
#define RX_FIFO_CHUNK 32
#define RX_BUF_SIZE (RX_PKT_PAYLOAD_MAX * 2)

#include "stm32f1xx_hal.h"
#include "sx1276-Fsk.h"
#include "sx1276-Hal.h"
#include "sx1276_fsk_packet.h"
#include <string.h>

/* ---- shared with sx1276_fsk_packet.c ---- */
extern volatile uint8_t dio0_irq_fired;
extern volatile uint8_t dio1_fifo_level;

/* ---- RX buffer for accumulating chunks during large packets ---- */
static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_total;
static uint16_t rx_payload_len;

/* ---- DIO1 (FifoLevel) flag, set by EXTI or polled ---- */
volatile uint8_t dio1_fifo_level = 0;

void EXTI3_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_3) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_3);
        dio1_fifo_level = 1;
    }
}

/* ---- DIO1/FifoLevel: bulk read, FIFO guaranteed >= 32 bytes ---- */
static void read_fifo_chunk(void)
{
    uint16_t remaining = rx_payload_len - rx_total;
    if (remaining == 0) {
        dio1_fifo_level = 0;
        return;
    }
    uint8_t n = remaining < RX_FIFO_CHUNK ? (uint8_t)remaining : RX_FIFO_CHUNK;

    SX1276ReadFifo(rx_buf + rx_total, n);
    rx_total += n;
    dio1_fifo_level = 0;
}

/* ---- DIO0/PayloadReady: drain FIFO byte-by-byte until empty ---- */
static void read_fifo_remaining(void)
{
    uint8_t flags, byte;
    while (rx_total < rx_payload_len) {
        SX1276Read(REG_IRQFLAGS2, &flags);
        if (flags & 0x40) /* FifoEmpty */
            break;
        SX1276ReadFifo(&byte, 1);
        rx_buf[rx_total++] = byte;
    }
}

void SX1276_FSK_StartRx(void)
{
    SX1276Write(REG_OPMODE, RF_OPMODE_STANDBY);

    rx_total = 0;
    memset(rx_buf, 0, sizeof(rx_buf));
    rx_payload_len = RX_PKT_PAYLOAD_MAX;

    SX1276Write(REG_PAYLOADLENGTH, RX_PKT_PAYLOAD_MAX);
    SX1276Write(REG_FIFOTHRESH, 0xA0); /* FifoLevel at 32 bytes */

    SX1276_FSK_ClearIrq();

    RXTX(0); /* antenna to RX */

    SX1276Write(REG_OPMODE, RF_OPMODE_RECEIVER);
}

bool SX1276_FSK_CheckPayloadReady(void)
{
    if (dio1_fifo_level) {
        read_fifo_chunk();
    }

    if (dio0_irq_fired) {
        dio0_irq_fired = 0;
        read_fifo_remaining(); /* precise byte-by-byte drain */
        return true;
    }
    return false;
}

uint16_t SX1276_FSK_GetAndClearRxData(uint8_t* buf, uint16_t max)
{
    uint16_t n = rx_total < max ? rx_total : max;
    memcpy(buf, rx_buf, n);
    rx_total = 0;
    return n;
}
