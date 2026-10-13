#include "board.h"
#include "main.h"

extern UART_HandleTypeDef huart2;
static ADC_HandleTypeDef adc;
static bool adc_ready;
static bool initialize_adc(void);
static volatile uint16_t rx_head, rx_tail;
static volatile bool rx_overflow;
static uint8_t rx_byte, rx_ring[256];
static volatile uint16_t tx_head, tx_tail;
static volatile bool tx_busy, tx_overflow;
static uint8_t tx_ring[2048];
static uint32_t bus_started;
static bool bus_timeout;

static void delay_us(uint32_t us) {
    uint32_t remaining = us * (SystemCoreClock / 1000000u);
    uint32_t previous = SysTick->VAL;
    while (remaining) {
        uint32_t current = SysTick->VAL;
        uint32_t elapsed = previous >= current ? previous - current : previous + SysTick->LOAD + 1u - current;
        if (elapsed >= remaining) break;
        remaining -= elapsed;
        previous = current;
    }
}
static void scl(bool high) { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, high ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static void sda(bool high) { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, high ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static bool clock_high(void) {
    scl(true);
    while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) != GPIO_PIN_SET) {
        if ((uint32_t)(HAL_GetTick() - bus_started) >= 10u) { bus_timeout = true; return false; }
    }
    delay_us(5);
    return true;
}
static bool start(void) {
    sda(true);
    if (!clock_high()) return false;
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) != GPIO_PIN_SET) return false;
    sda(false); delay_us(5); scl(false); delay_us(5);
    return true;
}
static void stop(void) {
    scl(false); sda(false); delay_us(5);
    (void)clock_high(); sda(true); delay_us(5);
}
static bool send_byte(uint8_t byte) {
    for (unsigned bit = 0; bit < 8; ++bit) {
        sda((byte & 0x80u) != 0); delay_us(5);
        if (!clock_high()) return false;
        scl(false); byte <<= 1;
    }
    sda(true); delay_us(5);
    if (!clock_high()) return false;
    bool ack = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET;
    scl(false); delay_us(5);
    return ack;
}
static bool receive_byte(uint8_t *byte, bool last) {
    *byte = 0; sda(true);
    for (unsigned bit = 0; bit < 8; ++bit) {
        delay_us(5);
        if (!clock_high()) return false;
        *byte = (uint8_t)((*byte << 1) | (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_SET));
        scl(false);
    }
    sda(last); delay_us(5);
    if (!clock_high()) return false;
    scl(false); sda(true); delay_us(5);
    return true;
}
static BusResult bus_read(void *context, uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
    (void)context; bus_started = HAL_GetTick(); bus_timeout = false;
    bool ok = start() && send_byte((uint8_t)(address << 1)) && send_byte(reg);
    if (ok) ok = start() && send_byte((uint8_t)((address << 1) | 1u));
    for (size_t index = 0; ok && index < length; ++index) ok = receive_byte(&data[index], index + 1 == length);
    stop();
    return bus_timeout ? BUS_TIMEOUT : ok ? BUS_OK : BUS_NACK;
}
static BusResult bus_write(void *context, uint8_t address, uint8_t reg, const uint8_t *data, size_t length) {
    (void)context; bus_started = HAL_GetTick(); bus_timeout = false;
    bool ok = start() && send_byte((uint8_t)(address << 1)) && send_byte(reg);
    for (size_t index = 0; ok && index < length; ++index) ok = send_byte(data[index]);
    stop();
    return bus_timeout ? BUS_TIMEOUT : ok ? BUS_OK : BUS_NACK;
}
SensorBus board_sensor_bus(void) { return (SensorBus){NULL, bus_read, bus_write}; }

static void transmit_next(void) {
    if (tx_head == tx_tail) { tx_busy = false; return; }
    tx_busy = HAL_UART_Transmit_IT(&huart2, &tx_ring[tx_tail], 1) == HAL_OK;
    if (!tx_busy) tx_overflow = true;
}
int board_put_char(int value) {
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint16_t next = (uint16_t)((tx_head + 1u) & 2047u);
    if (next == tx_tail) {
        tx_overflow = true;
        if (!mask) __enable_irq();
        return -1;
    }
    tx_ring[tx_head] = (uint8_t)value; tx_head = next;
    if (!tx_busy) transmit_next();
    if (!mask) __enable_irq();
    return value;
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart) {
    if (uart != &huart2) return;
    tx_tail = (uint16_t)((tx_tail + 1u) & 2047u);
    transmit_next();
}
bool board_tx_overflow(void) {
    uint32_t mask = __get_PRIMASK();
    __disable_irq(); bool value = tx_overflow; tx_overflow = false;
    if (!mask) __enable_irq();
    return value;
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart) {
    if (uart != &huart2) return;
    uint16_t next = (uint16_t)((rx_head + 1u) & 255u);
    if (next == rx_tail) rx_overflow = true;
    else { rx_ring[rx_head] = rx_byte; rx_head = next; }
    (void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart) {
    if (uart == &huart2) { rx_overflow = true; (void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1); }
}
int board_get_char(void) {
    if (rx_head == rx_tail) return -1;
    uint8_t value = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1u) & 255u);
    return value;
}
bool board_rx_overflow(void) {
    uint32_t mask = __get_PRIMASK();
    __disable_irq(); bool value = rx_overflow; rx_overflow = false;
    if (!mask) __enable_irq();
    return value;
}
bool board_motion(void) { return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET; }
void board_led(bool on) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
bool board_read_light(uint16_t *value) {
    if (!adc_ready) adc_ready = initialize_adc();
    if (!adc_ready) return false;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    bool ok = HAL_ADC_Start(&adc) == HAL_OK && HAL_ADC_PollForConversion(&adc, 2) == HAL_OK;
    if (ok) *value = (uint16_t)HAL_ADC_GetValue(&adc);
    if (HAL_ADC_Stop(&adc) != HAL_OK) ok = false;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    if (!ok) { (void)HAL_ADC_DeInit(&adc); adc_ready = false; }
    return ok;
}
void HAL_ADC_MspInit(ADC_HandleTypeDef *handle) {
    if (handle->Instance == ADC1) __HAL_RCC_ADC_CLK_ENABLE();
}
void board_init(void) {
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_DISABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9; gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP; gpio.Speed = GPIO_SPEED_FREQ_HIGH; HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_1; gpio.Mode = GPIO_MODE_INPUT; gpio.Pull = GPIO_PULLDOWN; HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_4; gpio.Mode = GPIO_MODE_OUTPUT_PP; gpio.Pull = GPIO_NOPULL; HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_0; gpio.Mode = GPIO_MODE_ANALOG; HAL_GPIO_Init(GPIOA, &gpio);
    adc_ready = initialize_adc();
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0); HAL_NVIC_EnableIRQ(USART2_IRQn);
    (void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}
static bool initialize_adc(void) {
    adc = (ADC_HandleTypeDef){0};
    adc.Instance = ADC1;
    adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
    adc.Init.Resolution = ADC_RESOLUTION_12B; adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    adc.Init.ScanConvMode = ADC_SCAN_SEQ_FIXED; adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    adc.Init.NbrOfConversion = 1; adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    adc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    adc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_160CYCLES_5;
    adc.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_160CYCLES_5;
    adc.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_LOW;
    adc_ready = HAL_ADC_Init(&adc) == HAL_OK;
    ADC_ChannelConfTypeDef channel = {0};
    channel.Channel = ADC_CHANNEL_0; channel.Rank = ADC_RANK_CHANNEL_NUMBER;
    channel.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    if (adc_ready) adc_ready = HAL_ADC_ConfigChannel(&adc, &channel) == HAL_OK;
    if (adc_ready) adc_ready = HAL_ADCEx_Calibration_Start(&adc) == HAL_OK;
    return adc_ready;
}
