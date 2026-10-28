/* Test double для Platform-шару: замість HAL — модель чіпів у пам'яті.
 * Fake (працює як чіп) + журнал транзакцій (можна перевіряти як mock). */
#include <string.h>
#include "bus_adapters.h"
#include "fake_bus.h"

uint8_t   fake_i2c_regs[128][256];
i2c_log_t fake_i2c_log[FAKE_LOG_MAX];
int       fake_i2c_log_n, fake_i2c_fail_at = -1, fake_i2c_nack_addr = -1;
int32_t   fake_bmp180_ut, fake_bmp180_up;
static int i2c_calls;

spi_log_t fake_spi_log[FAKE_LOG_MAX];
int       fake_spi_log_n;
uint8_t   fake_flash[FAKE_FLASH_SIZE];
uint8_t   fake_spi_jedec[3] = {0xEF, 0x40, 0x17};
int       fake_spi_busy_polls, fake_spi_fail_at = -1, fake_spi_ignored_writes, fake_spi_cs_violations;
bool      fake_spi_cs_low, fake_spi_read_corrupt;
static int      spi_calls, busy_left;
static bool     wel;
static uint8_t  txn[300];
static size_t   txn_len, rx_total;

uint8_t fake_sccb_regs[256];
bool    fake_sccb_fail_all;
int     fake_sccb_reads;

uint8_t fake_uart_rx[256]; int fake_uart_rx_len, fake_uart_rx_pos;
uint8_t fake_uart_tx[1024]; int fake_uart_tx_len;

/* ---------------- I2C ---------------- */
static void i2c_log_add(bool w, uint16_t dev, uint16_t reg, size_t len, uint8_t first)
{
    if (fake_i2c_log_n < FAKE_LOG_MAX) {
        fake_i2c_log[fake_i2c_log_n++] = (i2c_log_t){ w, dev, reg, len, first };
    }
}

static bool i2c_should_fail(uint16_t dev)
{
    int idx = i2c_calls++;
    return (fake_i2c_fail_at == idx) || (fake_i2c_nack_addr == (int)dev);
}

/* ---------------- L3GD20H/LSM303D ---------------- */
static uint16_t reg_mask(uint16_t dev, uint16_t reg)
{
    return (dev == 0x1D || dev == 0x6B) ? (uint16_t)(reg & 0x7F) : reg;
}

static bus_status_t f_i2c_read(uint16_t dev, uint16_t reg, uint8_t reg_len, uint8_t *data, size_t len)
{
    (void)reg_len;
    i2c_log_add(false, dev, reg, len, 0);
    if (i2c_should_fail(dev)) return BUS_ERROR;
    uint16_t r = reg_mask(dev, reg);
    for (size_t i = 0; i < len; i++) data[i] = fake_i2c_regs[dev & 0x7F][(r + i) & 0xFF];
    return BUS_OK;
}

static bus_status_t f_i2c_write(uint16_t dev, uint16_t reg, uint8_t reg_len, const uint8_t *data, size_t len)
{
    (void)reg_len;
    i2c_log_add(true, dev, reg, len, len ? data[0] : 0);
    if (i2c_should_fail(dev)) return BUS_ERROR;
    uint16_t r = reg_mask(dev, reg);
    for (size_t i = 0; i < len; i++) fake_i2c_regs[dev & 0x7F][(r + i) & 0xFF] = data[i];
    if (dev == 0x77 && reg == 0xF4 && len == 1) {            /* BMP180: запуск вимірювання */
        uint8_t *o = &fake_i2c_regs[0x77][0xF6];
        if (data[0] == 0x2E) { o[0] = (uint8_t)(fake_bmp180_ut >> 8); o[1] = (uint8_t)fake_bmp180_ut; }
        if (data[0] == 0x34) { o[0] = (uint8_t)(fake_bmp180_up >> 8); o[1] = (uint8_t)fake_bmp180_up; o[2] = 0; }
    }
    return BUS_OK;
}

const i2c_bus_t g_i2c_platform_bus = { .read = f_i2c_read, .write = f_i2c_write };

/* ---------------- SPI + W25Q64 ---------------- */
static void spi_commit(void)
{
    if (txn_len == 0) return;
    uint8_t op = txn[0];
    uint32_t addr = (txn_len >= 4) ? ((uint32_t)txn[1] << 16 | (uint32_t)txn[2] << 8 | txn[3]) : 0;
    if (fake_spi_log_n < FAKE_LOG_MAX)
        fake_spi_log[fake_spi_log_n++] = (spi_log_t){ op, addr, txn_len, rx_total };

    if (op == 0x06) wel = true;
    else if (op == 0x20 || op == 0x02) {
        if (!wel) { fake_spi_ignored_writes++; return; }
        if (op == 0x20) memset(&fake_flash[addr & ~0xFFFu], 0xFF, 4096);
        else for (size_t i = 4; i < txn_len; i++) fake_flash[(addr + i - 4) % FAKE_FLASH_SIZE] &= txn[i];
        wel = false;
        busy_left = fake_spi_busy_polls;
    }
}

static void f_cs(bool state)
{
    if (state) { txn_len = 0; rx_total = 0; fake_spi_cs_low = true; }
    else       { spi_commit(); fake_spi_cs_low = false; }
}

static bus_status_t f_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    int idx = spi_calls++;
    if (!fake_spi_cs_low) fake_spi_cs_violations++;
    if (fake_spi_fail_at == idx) return BUS_ERROR;
    if (tx) {
        for (size_t i = 0; i < len && txn_len < sizeof txn; i++) txn[txn_len++] = tx[i];
    } else if (rx && txn_len > 0) {
        uint32_t addr = (txn_len >= 4) ? ((uint32_t)txn[1] << 16 | (uint32_t)txn[2] << 8 | txn[3]) : 0;
        for (size_t i = 0; i < len; i++) {
            switch (txn[0]) {
            case 0x9F: rx[i] = fake_spi_jedec[i % 3]; break;
            case 0x05: rx[i] = (uint8_t)((busy_left > 0 ? 1 : 0) | (wel ? 2 : 0)); if (busy_left > 0) busy_left--; break;
            case 0x03: rx[i] = fake_flash[(addr + rx_total + i) % FAKE_FLASH_SIZE];
                       if (fake_spi_read_corrupt && rx_total + i == 0) rx[i] ^= 0x01;
                       break;
            default:   rx[i] = 0xFF;
            }
        }
        rx_total += len;
    }
    return BUS_OK;
}

const spi_bus_t g_spi_platform_bus = { .transfer = f_spi_transfer, .cs_control = f_cs };

/* ---------------- SCCB ---------------- */
static bus_status_t f_sccb_read(uint8_t reg, uint8_t *v)
{ fake_sccb_reads++; if (fake_sccb_fail_all) return BUS_ERROR; *v = fake_sccb_regs[reg]; return BUS_OK; }
static bus_status_t f_sccb_write(uint8_t reg, uint8_t v)
{ if (fake_sccb_fail_all) return BUS_ERROR; fake_sccb_regs[reg] = v; return BUS_OK; }
const sccb_bus_t g_sccb_platform_bus = { .read = f_sccb_read, .write = f_sccb_write };

/* ---------------- Platform ---------------- */
void platform_init(void) {}
void platform_delay_ms(uint32_t ms) { (void)ms; }          
void platform_camera_power_on(void) {}
int  platform_uart1_read_byte(uint8_t *b)
{
    if (fake_uart_rx_pos >= fake_uart_rx_len) return 0;
    *b = fake_uart_rx[fake_uart_rx_pos++];
    return 1;
}
void platform_uart1_write(const uint8_t *d, uint16_t len)
{
    for (uint16_t i = 0; i < len && fake_uart_tx_len < (int)sizeof fake_uart_tx; i++)
        fake_uart_tx[fake_uart_tx_len++] = d[i];
}
void fake_uart_feed(const char *s)
{
    while (*s && fake_uart_rx_len < (int)sizeof fake_uart_rx) fake_uart_rx[fake_uart_rx_len++] = (uint8_t)*s++;
}

/* ---------------- Скидання між тестами ---------------- */
void fake_reset(void)
{
    memset(fake_i2c_regs, 0, sizeof fake_i2c_regs);
    fake_i2c_log_n = 0; fake_i2c_fail_at = -1; fake_i2c_nack_addr = -1; i2c_calls = 0;
    fake_bmp180_ut = fake_bmp180_up = 0;
    memset(fake_flash, 0xFF, sizeof fake_flash);
    fake_spi_log_n = 0; fake_spi_busy_polls = 0; fake_spi_fail_at = -1; spi_calls = 0;
    fake_spi_ignored_writes = 0; fake_spi_cs_violations = 0; fake_spi_cs_low = false; fake_spi_read_corrupt = false;
    wel = false; busy_left = 0; txn_len = 0; rx_total = 0;
    fake_spi_jedec[0] = 0xEF; fake_spi_jedec[1] = 0x40; fake_spi_jedec[2] = 0x17;
    memset(fake_sccb_regs, 0, sizeof fake_sccb_regs);
    fake_sccb_regs[0x0A] = 0x76; fake_sccb_regs[0x0B] = 0x73; fake_sccb_fail_all = false; fake_sccb_reads = 0;
    fake_uart_rx_len = fake_uart_rx_pos = fake_uart_tx_len = 0;
    /* ідентифікатори чіпів */
    fake_i2c_regs[0x1D][0x0F] = 0x49;   /* LSM303D  WHO_AM_I */
    fake_i2c_regs[0x6B][0x0F] = 0xD4;   /* L3GD20H  WHO_AM_I */
    fake_i2c_regs[0x77][0xD0] = 0x55;   /* BMP180   chip id  */
}

/* Приклад із datasheet Bosch BMP180 */
void fake_bmp180_load_datasheet_example(void)
{
    static const int16_t cal[11] = { 408, -72, -14383, (int16_t)32741, (int16_t)32757, 23153,
                                     6190, 4, -32768, -8711, 2868 };
    for (int i = 0; i < 11; i++) {
        fake_i2c_regs[0x77][0xAA + 2 * i]     = (uint8_t)((uint16_t)cal[i] >> 8);
        fake_i2c_regs[0x77][0xAA + 2 * i + 1] = (uint8_t)((uint16_t)cal[i] & 0xFF);
    }
    fake_bmp180_ut = 27898;
    fake_bmp180_up = 23843;
}
