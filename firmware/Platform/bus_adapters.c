#include "bus_adapters.h"
#include "stm32f4xx_hal.h"

extern I2C_HandleTypeDef hi2c1;
extern SPI_HandleTypeDef hspi2;
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim1;

//I2C1
static bus_status_t hal_i2c_read(uint16_t dev_addr, uint16_t reg_addr,
                                 uint8_t reg_len, uint8_t *data, size_t len)
{
    uint16_t hal_addr = (uint16_t)(dev_addr << 1);
    uint16_t mem_size = (reg_len == 2) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&hi2c1, hal_addr, reg_addr, mem_size,
                                                data, (uint16_t)len, 100);
    return (status == HAL_OK) ? BUS_OK : BUS_ERROR;
}

static bus_status_t hal_i2c_write(uint16_t dev_addr, uint16_t reg_addr,
                                  uint8_t reg_len, const uint8_t *data, size_t len)
{
    uint16_t hal_addr = (uint16_t)(dev_addr << 1);
    uint16_t mem_size = (reg_len == 2) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&hi2c1, hal_addr, reg_addr, mem_size,
                                                 (uint8_t *)data, (uint16_t)len, 100);
    return (status == HAL_OK) ? BUS_OK : BUS_ERROR;
}

const i2c_bus_t g_i2c_platform_bus = {
    .read  = hal_i2c_read,
    .write = hal_i2c_write
};

void platform_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

//SPI2, CS# = PB12
static bus_status_t hal_spi_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t len)
{
    HAL_StatusTypeDef status;

    if (tx_data != NULL && rx_data != NULL) {
        status = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx_data, rx_data, (uint16_t)len, 100);
    } else if (tx_data != NULL) {
        status = HAL_SPI_Transmit(&hspi2, (uint8_t *)tx_data, (uint16_t)len, 100);
    } else {
        status = HAL_SPI_Receive(&hspi2, rx_data, (uint16_t)len, 100);
    }
    return (status == HAL_OK) ? BUS_OK : BUS_ERROR;
}

static void hal_spi_cs_control(bool state)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, state ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

const spi_bus_t g_spi_platform_bus = {
    .transfer   = hal_spi_transfer,
    .cs_control = hal_spi_cs_control
};

//SCCB (OV7670): PB6 = SCL, PB7 = SDA
#define SCCB_PORT        GPIOB
#define SCCB_SCL_PIN     GPIO_PIN_6
#define SCCB_SDA_PIN     GPIO_PIN_7
#define SCCB_HALF_DELAY  500
#define OV7670_SCCB_ID_W 0x42
#define OV7670_SCCB_ID_R 0x43

static void sccb_delay_half(void)
{
    for (volatile uint32_t i = 0; i < SCCB_HALF_DELAY; i++) {
        __NOP();
    }
}

static void sccb_sda_input(void)
{
    GPIO_InitTypeDef gi = {0};
    gi.Pin = SCCB_SDA_PIN;
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SCCB_PORT, &gi);
}

static void sccb_sda_output(void)
{
	HAL_GPIO_WritePin(GPIOB, SCCB_SDA_PIN, GPIO_PIN_SET);

    GPIO_InitTypeDef gi = {0};
    gi.Pin = SCCB_SDA_PIN;
    gi.Mode = GPIO_MODE_OUTPUT_OD;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SCCB_PORT, &gi);
}

static void sccb_start(void)
{
    sccb_sda_output();
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SDA_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_SET);
    sccb_delay_half();
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SDA_PIN, GPIO_PIN_RESET);
    sccb_delay_half();
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_RESET);
    sccb_delay_half();
}

static void sccb_stop(void)
{
    sccb_sda_output();
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SDA_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_SET);
    sccb_delay_half();
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SDA_PIN, GPIO_PIN_SET);
    sccb_delay_half();
}

static bool sccb_write_byte(uint8_t b)
{
    bool ack;

    for (int8_t i = 7; i >= 0; i--) {
        HAL_GPIO_WritePin(SCCB_PORT, SCCB_SDA_PIN, ((b >> i) & 1u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        sccb_delay_half();
        HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_SET);
        sccb_delay_half();
        HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_RESET);
    }

    sccb_sda_input();
    sccb_delay_half();
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_SET);
    sccb_delay_half();
    ack = (HAL_GPIO_ReadPin(SCCB_PORT, SCCB_SDA_PIN) == GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_RESET);
    sccb_delay_half();
    sccb_sda_output();
    return ack;
}

static uint8_t sccb_read_byte(void)
{
    uint8_t b = 0;

    sccb_sda_input();
    for (int8_t i = 7; i >= 0; i--) {
        HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_SET);
        sccb_delay_half();
        if (HAL_GPIO_ReadPin(SCCB_PORT, SCCB_SDA_PIN) == GPIO_PIN_SET) {
            b |= (uint8_t)(1u << i);
        }
        HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_RESET);
        sccb_delay_half();
    }

    sccb_sda_output();
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SDA_PIN, GPIO_PIN_SET);
    sccb_delay_half();
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_SET);
    sccb_delay_half();
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_RESET);
    sccb_delay_half();
    return b;
}

static bus_status_t hal_sccb_write(uint8_t reg, uint8_t value)
{
    bool ack = true;

    sccb_start();
    ack &= sccb_write_byte(OV7670_SCCB_ID_W);
    ack &= sccb_write_byte(reg);
    ack &= sccb_write_byte(value);
    sccb_stop();
    sccb_delay_half();
    return ack ? BUS_OK : BUS_ERROR;
}

static bus_status_t hal_sccb_read(uint8_t reg, uint8_t *value)
{
    bool ack = true;

    sccb_start();
    ack &= sccb_write_byte(OV7670_SCCB_ID_W);
    ack &= sccb_write_byte(reg);
    sccb_stop();
    sccb_delay_half();

    sccb_start();
    ack &= sccb_write_byte(OV7670_SCCB_ID_R);
    *value = sccb_read_byte();
    sccb_stop();
    sccb_delay_half();
    return ack ? BUS_OK : BUS_ERROR;
}

const sccb_bus_t g_sccb_platform_bus = {
    .read  = hal_sccb_read,
    .write = hal_sccb_write
};

void platform_init(void)
{
    HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN | SCCB_SDA_PIN, GPIO_PIN_SET);

    hal_spi_cs_control(false);
}

void platform_camera_power_on(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_Delay(5);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_Delay(5);
}

int platform_uart1_read_byte(uint8_t *byte)
{
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
        *byte = (uint8_t)(huart1.Instance->DR & 0xFFu);
        return 1;
    }
    return 0;
}

void platform_uart1_write(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 100);
}
