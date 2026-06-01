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
void SX1276_FSK_ClearDio0Flag(void);

/* RX */
void SX1276_FSK_StartRx(void);
bool SX1276_FSK_CheckPayloadReady(void);
uint16_t SX1276_FSK_GetAndClearRxData(uint8_t* buf, uint16_t max);

#endif
