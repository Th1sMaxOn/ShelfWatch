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
static BusResult wait_i2c(uint32_t flag) {
    for (;;) {
        uint32_t status = I2C1->ISR;
        if (status & I2C_ISR_NACKF) return BUS_NACK;
        if (status & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) return BUS_ERROR;
        if (status & flag) return BUS_OK;
        if ((uint32_t)(HAL_GetTick() - bus_started) >= 10u) return BUS_TIMEOUT;
    }
}
static void clear_i2c(void) {
    I2C1->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
}
static BusResult finish_i2c(BusResult result) {
    if (result != BUS_OK || (I2C1->ISR & I2C_ISR_BUSY)) {
        I2C1->CR2 |= I2C_CR2_STOP;
        I2C1->CR1 &= ~I2C_CR1_PE;
        I2C1->CR1 |= I2C_CR1_PE;
    }
    clear_i2c();
    I2C1->CR2 = 0;
    return result;
}
/* AUTOEND retains the MPU pointer across STOP. Whole CR2 writes avoid partial
 * model transactions; a TX settle interval is bounded by the same deadline. */
static BusResult write_phase(uint8_t address, const uint8_t *data, size_t length) {
    clear_i2c();
    I2C1->CR2 = ((uint32_t)address << 1) | ((uint32_t)length << 16) | I2C_CR2_AUTOEND | I2C_CR2_START;
    delay_us(120);
    for (size_t index = 0; index < length; ++index) {
        BusResult result = wait_i2c(I2C_ISR_TXIS);
        if (result != BUS_OK) return result;
        I2C1->TXDR = data[index];
        delay_us(120);
    }
    BusResult result = wait_i2c(I2C_ISR_STOPF);
    if (result == BUS_OK) { clear_i2c(); delay_us(120); }
    return result;
}
static BusResult bus_read(void *context, uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
    (void)context;
    if (!data || !length || length > 255 || address > 127) return BUS_ERROR;
    bus_started = HAL_GetTick();
    BusResult result = write_phase(address, &reg, 1);
    if (result != BUS_OK) return finish_i2c(result);
    I2C1->CR2 = ((uint32_t)address << 1) | ((uint32_t)length << 16) | I2C_CR2_RD_WRN | I2C_CR2_AUTOEND | I2C_CR2_START;
    for (size_t index = 0; index < length; ++index) {
        result = wait_i2c(I2C_ISR_RXNE);
        if (result != BUS_OK) return finish_i2c(result);
        data[index] = (uint8_t)I2C1->RXDR;
    }
    result = wait_i2c(I2C_ISR_STOPF);
    if (result == BUS_OK) delay_us(120);
    return finish_i2c(result);
}
static BusResult bus_write(void *context, uint8_t address, uint8_t reg, const uint8_t *data, size_t length) {
    (void)context;
    if (!data || !length || length > 254 || address > 127) return BUS_ERROR;
    uint8_t bytes[255]; bytes[0] = reg;
    for (size_t index = 0; index < length; ++index) bytes[index + 1] = data[index];
    bus_started = HAL_GetTick();
    return finish_i2c(write_phase(address, bytes, length + 1));
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
