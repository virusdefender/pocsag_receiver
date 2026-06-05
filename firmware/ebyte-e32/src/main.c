#include "stm32f1xx_hal.h"
#include "sx1276_fsk_packet.h"
#include "u8g2_hal.h"
#include <stdio.h>
#include <string.h>

static void SystemClock_Config(void);
static void MX_SPI1_Init(void);
static void MX_I2C2_Init(void);
static void MX_USART1_UART_Init(void);
static void LED_Init(void);
void Error_Handler(void);

I2C_HandleTypeDef hi2c2;
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart1;
u8g2_t u8g2;

#define LED_RX_PORT GPIOB
#define LED_RX_PIN GPIO_PIN_6

#define KEY_UP_PORT GPIOB
#define KEY_UP_PIN GPIO_PIN_4
#define KEY_DOWN_PORT GPIOB
#define KEY_DOWN_PIN GPIO_PIN_9
#define KEY_ENTER_PORT GPIOB
#define KEY_ENTER_PIN GPIO_PIN_7

// 自行设置频率，单位是 Hz
#define RX_FREQ 0
#if RX_FREQ == 0
    #error "Please define RX_FREQ"
#endif

#define RX_BITRATE 1200
#define RX_FDEV 4500
#define RX_BW 12500

#define RX_BUF_SIZE 64

static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_total;
static uint32_t rx_count;

static void buttons_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = { 0 };
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = KEY_UP_PIN | KEY_DOWN_PIN | KEY_ENTER_PIN;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static bool button_down(int idx)
{
    uint16_t pins[3] = { KEY_UP_PIN, KEY_DOWN_PIN, KEY_ENTER_PIN };
    GPIO_TypeDef* ports[3] = { KEY_UP_PORT, KEY_DOWN_PORT, KEY_ENTER_PORT };
    return HAL_GPIO_ReadPin(ports[idx], pins[idx]) == GPIO_PIN_RESET;
}

static bool button_pressed(int idx)
{
    if (!button_down(idx))
        return false;
    HAL_Delay(20);
    if (!button_down(idx))
        return false;
    while (button_down(idx))
        ;
    return true;
}

static void uart_hex_dump(const uint8_t* data, uint16_t len, int16_t fifo_rssi,
    int16_t payload_rssi, int32_t fifo_afc_hz, int32_t payload_afc_hz,
    int32_t fifo_fei_hz, int32_t payload_fei_hz)
{
    char s[512];
    int p = snprintf(s, sizeof(s),
        "{\"fifo_rssi\":%d,\"payload_rssi\":%d,"
        "\"fifo_afc_hz\":%ld,\"payload_afc_hz\":%ld,"
        "\"fifo_fei_hz\":%ld,\"payload_fei_hz\":%ld,\"data\":\"",
        fifo_rssi, payload_rssi, (long)fifo_afc_hz, (long)payload_afc_hz,
        (long)fifo_fei_hz, (long)payload_fei_hz);
    for (int i = 0; i < len && p < (int)sizeof(s) - 5; i++) {
        p += snprintf(s + p, sizeof(s) - p, "%02X", data[i]);
    }
    p += snprintf(s + p, sizeof(s) - p, "\"}\r\n");
    HAL_UART_Transmit(&huart1, (uint8_t*)s, p, 500);
}

static void display_update(int16_t rssi)
{
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_profont12_mf);

    char s[32];
    snprintf(s, sizeof(s), "FSK RX %lu.%04luM",
        (unsigned long)(RX_FREQ / 1000000UL),
        (unsigned long)((RX_FREQ % 1000000UL) / 100UL));
    u8g2_DrawStr(&u8g2, 0, 12, s);

    snprintf(s, sizeof(s), "RSSI: %d dBm", rssi);
    u8g2_DrawStr(&u8g2, 0, 28, s);

    snprintf(s, sizeof(s), "PKTs: %lu", (unsigned long)rx_count);
    u8g2_DrawStr(&u8g2, 0, 44, s);

    snprintf(s, sizeof(s), "Last: %u B", rx_total);
    u8g2_DrawStr(&u8g2, 0, 60, s);

    u8g2_SendBuffer(&u8g2);
}

int main(void)
{
    HAL_Init();
    LED_Init();
    SystemClock_Config();
    MX_SPI1_Init();
    MX_I2C2_Init();
    MX_USART1_UART_Init();

    buttons_init();
    u8g2Init(&u8g2);

    SX1276_FSK_Init(RX_FREQ, RX_BITRATE, RX_FDEV, RX_BW);
    SX1276_FSK_StartRx();

    uint8_t ver, pa, op, frf_h, frf_m, frf_l, padac;
    SX1276_FSK_DumpRegs(&ver, &pa, &op, &frf_h, &frf_m, &frf_l, &padac);

    char s[64];
    snprintf(s, sizeof(s), "FSK RX init OK\r\nver=%02X\r\n", ver);
    HAL_UART_Transmit(&huart1, (uint8_t*)s, strlen(s), 100);

    int16_t display_rssi = SX1276_FSK_ReadRssi();
    display_update(display_rssi);

    while (1) {
        if (SX1276_FSK_ReadPacketIfReady(rx_buf, sizeof(rx_buf), &rx_total)) {
            int16_t fifo_rssi = SX1276_FSK_GetFifoLevelRssi();
            int16_t payload_rssi = SX1276_FSK_GetPayloadReadyRssi();
            int32_t fifo_afc_hz = SX1276_FSK_GetFifoLevelAfcHz();
            int32_t payload_afc_hz = SX1276_FSK_GetPayloadReadyAfcHz();
            int32_t fifo_fei_hz = SX1276_FSK_GetFifoLevelFeiHz();
            int32_t payload_fei_hz = SX1276_FSK_GetPayloadReadyFeiHz();
            display_rssi = fifo_rssi;

            for (int i = 0; i < rx_total; i++) {
                rx_buf[i] ^= 0xFF;
            }

            rx_count++;

            HAL_GPIO_WritePin(LED_RX_PORT, LED_RX_PIN, GPIO_PIN_RESET);

            uart_hex_dump(rx_buf, rx_total, fifo_rssi, payload_rssi,
                fifo_afc_hz, payload_afc_hz, fifo_fei_hz, payload_fei_hz);
            display_update(display_rssi);

            HAL_GPIO_WritePin(LED_RX_PORT, LED_RX_PIN, GPIO_PIN_SET);
        }

        if (button_pressed(0)) {
            snprintf(s, sizeof(s), "\r\nRSSI=%d PKTs=%lu Last=%uB\r\n",
                display_rssi, (unsigned long)rx_count, rx_total);
            HAL_UART_Transmit(&huart1, (uint8_t*)s, strlen(s), 100);
        }

        if (button_pressed(2)) {
            snprintf(s, sizeof(s), "\r\n--- CLEAR COUNT ---\r\n");
            HAL_UART_Transmit(&huart1, (uint8_t*)s, strlen(s), 100);
            rx_count = 0;
            display_update(display_rssi);
        }
    }
}

void SysTick_Handler(void) { HAL_IncTick(); }

void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
}

static void LED_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = { 0 };
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = LED_RX_PIN;
    HAL_GPIO_Init(LED_RX_PORT, &gpio);
    HAL_GPIO_WritePin(LED_RX_PORT, LED_RX_PIN, GPIO_PIN_SET);
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = { 0 };
    RCC_ClkInitTypeDef clk = { 0 };

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        Error_Handler();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK)
        Error_Handler();
}

static void MX_I2C2_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();

    GPIO_InitTypeDef gpio = { 0 };
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    hi2c2.Instance = I2C2;
    hi2c2.Init.ClockSpeed = 400000;
    hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c2) != HAL_OK)
        Error_Handler();
}

static void MX_SPI1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = { 0 };
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK)
        Error_Handler();
}

static void MX_USART1_UART_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = { 0 };
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
        Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        HAL_GPIO_WritePin(LED_RX_PORT, LED_RX_PIN, GPIO_PIN_RESET);
        for (volatile int i = 0; i < 500000; i++)
            ;
        HAL_GPIO_WritePin(LED_RX_PORT, LED_RX_PIN, GPIO_PIN_SET);
        for (volatile int i = 0; i < 500000; i++)
            ;
    }
}
