#include "sx1276_fsk_packet.h"
#include "stm32f1xx_hal.h"
#include "sx1276-Fsk.h"
#include "sx1276-Hal.h"
#include "sx1276.h"

#define RX_PKT_PAYLOAD_MAX 64
#define SX1276_FSTEP_HZ_NUM 15625
#define SX1276_FSTEP_HZ_DEN 256

/* ================================================================
 * FSK parameters — constants (per POCSAG spec, not configurable)
 * ================================================================ */

/* ================================================================
 * DIO0 interrupt — used by RX (PayloadReady) and TX (PacketSent)
 * ================================================================ */
static volatile uint8_t dio0_irq_fired = 0;
static volatile uint8_t dio1_fifo_level = 0;

static int16_t fifo_level_rssi;
static int16_t payload_ready_rssi;
static int32_t fifo_level_afc_hz;
static int32_t payload_ready_afc_hz;
static int32_t fifo_level_fei_hz;
static int32_t payload_ready_fei_hz;
static bool fifo_level_rssi_valid;

void EXTI1_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_1) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_1);
        dio0_irq_fired = 1;
    }
}

void EXTI3_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_3) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_3);
        dio1_fifo_level = 1;
    }
}

static int32_t SX1276_FSK_ReadSignedFreqHz(uint8_t msb_reg)
{
    uint8_t b[2];
    SX1276ReadBuffer(msb_reg, b, sizeof(b));
    int16_t raw = (int16_t)(((uint16_t)b[0] << 8) | b[1]);
    return ((int32_t)raw * SX1276_FSTEP_HZ_NUM) / SX1276_FSTEP_HZ_DEN;
}

static void SX1276_FSK_SampleFifoLevelMetrics(void)
{
    fifo_level_rssi = SX1276_FSK_ReadRssi();
    fifo_level_afc_hz = SX1276_FSK_ReadSignedFreqHz(REG_AFCMSB);
    fifo_level_fei_hz = SX1276_FSK_ReadSignedFreqHz(REG_FEIMSB);
    fifo_level_rssi_valid = true;
}

static void SX1276_FSK_SamplePayloadReadyMetrics(void)
{
    payload_ready_rssi = SX1276_FSK_ReadRssi();
    payload_ready_afc_hz = SX1276_FSK_ReadSignedFreqHz(REG_AFCMSB);
    payload_ready_fei_hz = SX1276_FSK_ReadSignedFreqHz(REG_FEIMSB);
}

static void SX1276_FSK_CopyPayloadMetricsToFifoMetrics(void)
{
    fifo_level_rssi = payload_ready_rssi;
    fifo_level_afc_hz = payload_ready_afc_hz;
    fifo_level_fei_hz = payload_ready_fei_hz;
    fifo_level_rssi_valid = true;
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
    FskSettings.PayloadLength = RX_PKT_PAYLOAD_MAX;

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
    SX1276Write(REG_RXTIMEOUT2, 4); /* 4×16×Tbit≈53ms */
}

void SX1276_FSK_StartRx(void)
{
    SX1276Write(REG_OPMODE, RF_OPMODE_STANDBY);

    fifo_level_rssi = 0;
    payload_ready_rssi = 0;
    fifo_level_afc_hz = 0;
    payload_ready_afc_hz = 0;
    fifo_level_fei_hz = 0;
    payload_ready_fei_hz = 0;
    fifo_level_rssi_valid = false;
    dio0_irq_fired = 0;
    dio1_fifo_level = 0;

    SX1276Write(REG_PAYLOADLENGTH, RX_PKT_PAYLOAD_MAX);
    SX1276Write(REG_FIFOTHRESH, 0xA0); /* FifoLevel at 32 bytes */

    SX1276_FSK_ClearIrq();

    RXTX(0); /* antenna to RX */

    SX1276Write(REG_OPMODE, RF_OPMODE_RECEIVER);
}

bool SX1276_FSK_ReadPacketIfReady(uint8_t* buf, uint16_t max, uint16_t* len)
{
    bool output_valid = buf && max >= RX_PKT_PAYLOAD_MAX && len;

    if (len) {
        *len = 0;
    }

    if (dio1_fifo_level) {
        dio1_fifo_level = 0;
        if (!fifo_level_rssi_valid) {
            SX1276_FSK_SampleFifoLevelMetrics();
        }
    }

    if (dio0_irq_fired) {
        if (!output_valid) {
            return false;
        }
        dio0_irq_fired = 0;
        SX1276_FSK_SamplePayloadReadyMetrics();
        SX1276ReadFifo(buf, RX_PKT_PAYLOAD_MAX);
        *len = RX_PKT_PAYLOAD_MAX;
        if (!fifo_level_rssi_valid) {
            SX1276_FSK_CopyPayloadMetricsToFifoMetrics();
        }
        SX1276_FSK_ClearIrq();
        fifo_level_rssi_valid = false;
        return true;
    }
    return false;
}

int16_t SX1276_FSK_GetFifoLevelRssi(void)
{
    return fifo_level_rssi;
}

int16_t SX1276_FSK_GetPayloadReadyRssi(void)
{
    return payload_ready_rssi;
}

int32_t SX1276_FSK_GetFifoLevelAfcHz(void)
{
    return fifo_level_afc_hz;
}

int32_t SX1276_FSK_GetPayloadReadyAfcHz(void)
{
    return payload_ready_afc_hz;
}

int32_t SX1276_FSK_GetFifoLevelFeiHz(void)
{
    return fifo_level_fei_hz;
}

int32_t SX1276_FSK_GetPayloadReadyFeiHz(void)
{
    return payload_ready_fei_hz;
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
