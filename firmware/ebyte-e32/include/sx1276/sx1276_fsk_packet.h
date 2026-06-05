#ifndef __SX1276_FSK_PACKET_H
#define __SX1276_FSK_PACKET_H

#include <stdbool.h>
#include <stdint.h>

/* Init & common */
void SX1276_FSK_Init(uint32_t freq, uint32_t bitrate, uint32_t fdev,
    uint32_t rx_bw);
void SX1276_FSK_DumpRegs(uint8_t* ver, uint8_t* pa, uint8_t* op,
    uint8_t* frf_h, uint8_t* frf_m, uint8_t* frf_l,
    uint8_t* padac);
int16_t SX1276_FSK_ReadRssi(void);
void SX1276_FSK_ClearIrq(void);

/* RX */
void SX1276_FSK_StartRx(void);
bool SX1276_FSK_ReadPacketIfReady(uint8_t* buf, uint16_t max, uint16_t* len);
int16_t SX1276_FSK_GetFifoLevelRssi(void);
int16_t SX1276_FSK_GetPayloadReadyRssi(void);
int32_t SX1276_FSK_GetFifoLevelAfcHz(void);
int32_t SX1276_FSK_GetPayloadReadyAfcHz(void);
int32_t SX1276_FSK_GetFifoLevelFeiHz(void);
int32_t SX1276_FSK_GetPayloadReadyFeiHz(void);

#endif
