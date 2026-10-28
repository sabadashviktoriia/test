#include <string.h>
#include <stdio.h>
#include "bringup.h"
#include "bus_adapters.h"
#include "metrics.h"

#define LSM303D_ADDR   0x1D
#define L3GD20H_ADDR   0x6B
#define BMP180_ADDR    0x77

#define WHO_AM_I_REG   0x0F
#define CHIP_ID_REG    0xD0
#define LSM303D_OUT_A  0x28
#define L3GD20H_OUT    0x28
#define I2C_AUTO_INC   0x80
#define CTRL1_REG      0x20
#define CTRL2_REG      0x21

#define LSM303D_ID     0x49
#define L3GD20H_ID     0xD4
#define BMP180_ID      0x55

#define BMP180_CAL_REG   0xAA
#define BMP180_CTRL_REG  0xF4
#define BMP180_OUT_REG   0xF6

#define CMD_JEDEC_ID      0x9F
#define CMD_WREN          0x06
#define CMD_READ_SR1      0x05
#define CMD_READ_DATA     0x03
#define CMD_PAGE_PROG     0x02
#define CMD_SECTOR_ERASE  0x20

#define JEDEC_MFR   0xEF
#define JEDEC_TYPE  0x40
#define JEDEC_CAP   0x17

#define FLASH_TEST_ADDR  0x7FF000UL
#define FLASH_TEST_LEN   16

#define OV7670_COM7_REG    0x12
#define OV7670_COM7_RESET  0x80
#define OV7670_PID_REG     0x0A
#define OV7670_PID_VAL     0x76
#define OV7670_VER_REG     0x0B
#define OV7670_VER_VAL     0x73

#define CMD_BUF_SIZE  64
static char cmd_buf[CMD_BUF_SIZE];
static uint8_t cmd_idx = 0;

static bus_status_t read_xyz(uint8_t dev_addr, uint8_t reg, int num, int den,
                             int *x, int *y, int *z)
{
    uint8_t b[6];

    if (g_i2c_platform_bus.read(dev_addr, (uint16_t)(reg | I2C_AUTO_INC), 1, b, 6) != BUS_OK) {
        return BUS_ERROR;
    }
    *x = (int16_t)(((uint16_t)b[1] << 8) | b[0]) * num / den;
    *y = (int16_t)(((uint16_t)b[3] << 8) | b[2]) * num / den;
    *z = (int16_t)(((uint16_t)b[5] << 8) | b[4]) * num / den;
    return BUS_OK;
}

static uint16_t be16(const uint8_t *b, int i)
{
    return (uint16_t)(((uint16_t)b[i] << 8) | b[i + 1]);
}

static struct {
    int16_t  ac1, ac2, ac3;
    uint16_t ac4, ac5, ac6;
    int16_t  b1, b2, mb, mc, md;
} bmp_cal;

static int32_t bmp_b5;


static int32_t bmp180_calc_temp(int32_t ut)
{
    int32_t x1 = ((ut - bmp_cal.ac6) * bmp_cal.ac5) >> 15;
    int32_t x2 = (bmp_cal.mc * 2048) / (x1 + bmp_cal.md);

    bmp_b5 = x1 + x2;
    return (bmp_b5 + 8) >> 4;
}

static int32_t bmp180_calc_pressure(int32_t up)
{
    int32_t b6 = bmp_b5 - 4000;
    int32_t x1 = (bmp_cal.b2 * ((b6 * b6) >> 12)) >> 11;
    int32_t x2 = (bmp_cal.ac2 * b6) >> 11;
    int32_t x3 = x1 + x2;
    int32_t b3 = ((bmp_cal.ac1 * 4 + x3) + 2) >> 2;
    uint32_t b4, b7;
    int32_t p;

    x1 = (bmp_cal.ac3 * b6) >> 13;
    x2 = (bmp_cal.b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    b4 = ((uint32_t)bmp_cal.ac4 * (uint32_t)(x3 + 32768)) >> 15;
    b7 = ((uint32_t)up - (uint32_t)b3) * 50000U;
    p = (b7 < 0x80000000U) ? (int32_t)((b7 * 2) / b4) : (int32_t)((b7 / b4) * 2);

    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    return p + ((x1 + x2 + 3791) >> 4);
}

static bus_status_t bmp180_read(int32_t *temp_x10, int32_t *pressure_pa)
{
    uint8_t b[22];
    uint8_t cmd;

    if (g_i2c_platform_bus.read(BMP180_ADDR, BMP180_CAL_REG, 1, b, 22) != BUS_OK) return BUS_ERROR;
    bmp_cal.ac1 = (int16_t)be16(b, 0);
    bmp_cal.ac2 = (int16_t)be16(b, 2);
    bmp_cal.ac3 = (int16_t)be16(b, 4);
    bmp_cal.ac4 = be16(b, 6);
    bmp_cal.ac5 = be16(b, 8);
    bmp_cal.ac6 = be16(b, 10);
    bmp_cal.b1  = (int16_t)be16(b, 12);
    bmp_cal.b2  = (int16_t)be16(b, 14);
    bmp_cal.mb  = (int16_t)be16(b, 16);
    bmp_cal.mc  = (int16_t)be16(b, 18);
    bmp_cal.md  = (int16_t)be16(b, 20);

    cmd = 0x2E;
    g_i2c_platform_bus.write(BMP180_ADDR, BMP180_CTRL_REG, 1, &cmd, 1);
    platform_delay_ms(5);
    if (g_i2c_platform_bus.read(BMP180_ADDR, BMP180_OUT_REG, 1, b, 2) != BUS_OK) return BUS_ERROR;
    *temp_x10 = bmp180_calc_temp((int32_t)be16(b, 0));

    cmd = 0x34;
    g_i2c_platform_bus.write(BMP180_ADDR, BMP180_CTRL_REG, 1, &cmd, 1);
    platform_delay_ms(5);
    if (g_i2c_platform_bus.read(BMP180_ADDR, BMP180_OUT_REG, 1, b, 3) != BUS_OK) return BUS_ERROR;
    *pressure_pa = bmp180_calc_pressure(
        (int32_t)((((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2]) >> 8));
    return BUS_OK;
}

static void spi_command(const uint8_t *cmd, uint16_t cmd_len, uint8_t *rx, uint16_t rx_len)
{
    g_spi_platform_bus.cs_control(true);
    g_spi_platform_bus.transfer(cmd, NULL, cmd_len);
    if (rx_len > 0) g_spi_platform_bus.transfer(NULL, rx, rx_len);
    g_spi_platform_bus.cs_control(false);
}

static void make_cmd4(uint8_t *c, uint8_t op, uint32_t addr)
{
    c[0] = op;
    c[1] = (uint8_t)(addr >> 16);
    c[2] = (uint8_t)(addr >> 8);
    c[3] = (uint8_t)addr;
}

static void w25_read_jedec(uint8_t jedec[3])
{
    uint8_t cmd = CMD_JEDEC_ID;
    spi_command(&cmd, 1, jedec, 3);
}

static void w25_write_enable(void)
{
    uint8_t cmd = CMD_WREN;
    spi_command(&cmd, 1, NULL, 0);
}

static void w25_read_sr1(uint8_t *sr1)
{
    uint8_t cmd = CMD_READ_SR1;
    spi_command(&cmd, 1, sr1, 1);
}

static void w25_wait_ready(void)
{
    uint8_t sr1;

    do {
        sr1 = 0;
        w25_read_sr1(&sr1);
    } while (sr1 & 0x01);
}

static int flash_roundtrip(void)
{
    uint8_t prog[4 + FLASH_TEST_LEN];
    uint8_t in[FLASH_TEST_LEN];
    uint8_t c[4];

    w25_write_enable();
    make_cmd4(c, CMD_SECTOR_ERASE, FLASH_TEST_ADDR);
    spi_command(c, 4, NULL, 0);
    w25_wait_ready();

    make_cmd4(prog, CMD_PAGE_PROG, FLASH_TEST_ADDR);
    for (int i = 0; i < FLASH_TEST_LEN; i++) prog[4 + i] = (uint8_t)(i * 7 + 3);
    w25_write_enable();
    spi_command(prog, sizeof(prog), NULL, 0);
    w25_wait_ready();

    make_cmd4(c, CMD_READ_DATA, FLASH_TEST_ADDR);
    spi_command(c, 4, in, FLASH_TEST_LEN);
    return memcmp(&prog[4], in, FLASH_TEST_LEN) == 0;
}

static int selftest_lsm303d(void)
{
    uint8_t id = 0;
    uint8_t ctrl1 = 0x5F;
    uint8_t ctrl2 = 0x00;
    int x = 0, y = 0, z = 0;
    bus_status_t s;
    int ok;

    g_i2c_platform_bus.read(LSM303D_ADDR, WHO_AM_I_REG, 1, &id, 1);
    g_i2c_platform_bus.write(LSM303D_ADDR, CTRL1_REG, 1, &ctrl1, 1);
    g_i2c_platform_bus.write(LSM303D_ADDR, CTRL2_REG, 1, &ctrl2, 1);
    platform_delay_ms(100);
    s = read_xyz(LSM303D_ADDR, LSM303D_OUT_A, 61, 1000, &x, &y, &z);

    ok = (id == LSM303D_ID && s == BUS_OK);
    printf("[I2C] LSM303D id=0x%02X x=%d y=%d z=%d mg -> %s\r\n", id, x, y, z, ok ? "OK" : "FAIL");
    return ok;
}

static int selftest_l3gd20h(void)
{
    uint8_t id = 0;
    uint8_t ctrl1 = 0x0F;
    int x = 0, y = 0, z = 0;
    bus_status_t s;
    int ok;

    g_i2c_platform_bus.read(L3GD20H_ADDR, WHO_AM_I_REG, 1, &id, 1);
    g_i2c_platform_bus.write(L3GD20H_ADDR, CTRL1_REG, 1, &ctrl1, 1);
    platform_delay_ms(100);
    s = read_xyz(L3GD20H_ADDR, L3GD20H_OUT, 875, 100, &x, &y, &z);

    ok = (id == L3GD20H_ID && s == BUS_OK);
    printf("[I2C] L3GD20H id=0x%02X x=%d y=%d z=%d mdps -> %s\r\n", id, x, y, z, ok ? "OK" : "FAIL");
    return ok;
}

static int selftest_bmp180(void)
{
    uint8_t id = 0;
    int32_t t = 0, p = 0;
    bus_status_t s;
    int ok;

    g_i2c_platform_bus.read(BMP180_ADDR, CHIP_ID_REG, 1, &id, 1);
    s = bmp180_read(&t, &p);

    ok = (id == BMP180_ID && s == BUS_OK);
    printf("[I2C] BMP180 id=0x%02X temp_x10=%ld pressure=%ld Pa -> %s\r\n",
           id, (long)t, (long)p, ok ? "OK" : "FAIL");
    return ok;
}

static int selftest_w25q64(void)
{
    uint8_t jedec[3] = {0, 0, 0};
    int ok;

    w25_read_jedec(jedec);
    ok = (jedec[0] == JEDEC_MFR && jedec[1] == JEDEC_TYPE && jedec[2] == JEDEC_CAP)
         && flash_roundtrip();
    printf("[SPI] W25Q64 jedec=%02X %02X %02X -> %s\r\n", jedec[0], jedec[1], jedec[2],
           ok ? "OK" : "FAIL");
    return ok;
}

static int selftest_ov7670(void)
{
    uint8_t pid = 0, ver = 0;
    bus_status_t w;
    int ok;

    w = g_sccb_platform_bus.write(OV7670_COM7_REG, OV7670_COM7_RESET);
    platform_delay_ms(2);
    g_sccb_platform_bus.read(OV7670_PID_REG, &pid);
    g_sccb_platform_bus.read(OV7670_VER_REG, &ver);

    ok = (pid == OV7670_PID_VAL && ver == OV7670_VER_VAL);
    printf("[SCCB] OV7670 pid=0x%02X ver=0x%02X write_ack=%d -> %s\r\n",
           pid, ver, (int)(w == BUS_OK), ok ? "OK" : "FAIL");
    return ok;
}

static void app_selftest(void)
{
    int ok = 0;

    printf("\r\n=== SELFTEST ===\r\n");
    ok += selftest_lsm303d();
    ok += selftest_l3gd20h();
    ok += selftest_bmp180();
    ok += selftest_w25q64();
    ok += selftest_ov7670();
    printf("=== END (%d/5 OK) ===\r\n\r\n", ok);
}

static void cmd_i2c(void)
{
    uint8_t buf[6] = {0};

    printf("[I2C] WHO_AM_I x3 + 6-byte acc read\r\n");
    g_i2c_platform_bus.read(LSM303D_ADDR, WHO_AM_I_REG, 1, buf, 1);
    platform_delay_ms(1);
    g_i2c_platform_bus.read(L3GD20H_ADDR, WHO_AM_I_REG, 1, buf, 1);
    platform_delay_ms(1);
    g_i2c_platform_bus.read(BMP180_ADDR, CHIP_ID_REG, 1, buf, 1);
    platform_delay_ms(1);
    g_i2c_platform_bus.read(LSM303D_ADDR, LSM303D_OUT_A | I2C_AUTO_INC, 1, buf, 6);
    printf("[I2C] acc=[%02X %02X %02X %02X %02X %02X]\r\n",
           buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
}


static void cmd_data(void)
{
    int x = 0, y = 0, z = 0;
    int32_t t = 0, p = 0;
    bus_status_t s;

    s = read_xyz(LSM303D_ADDR, LSM303D_OUT_A, 61, 1000, &x, &y, &z);
    printf("[DATA] accel x=%d y=%d z=%d mg err=%d\r\n", x, y, z, (int)s);
    s = read_xyz(L3GD20H_ADDR, L3GD20H_OUT, 875, 100, &x, &y, &z);
    printf("[DATA] gyro x=%d y=%d z=%d mdps err=%d\r\n", x, y, z, (int)s);
    s = bmp180_read(&t, &p);
    printf("[DATA] baro temp_x10=%ld pressure=%ld Pa err=%d\r\n", (long)t, (long)p, (int)s);
}

static void cmd_spi(void)
{
    uint8_t jedec[3] = {0};
    uint8_t sr1 = 0;

    printf("[SPI] JEDEC ID + WREN + read SR1\r\n");
    w25_read_jedec(jedec);
    platform_delay_ms(1);
    w25_write_enable();
    platform_delay_ms(1);
    w25_read_sr1(&sr1);
    printf("[SPI] jedec=%02X %02X %02X, sr1=0x%02X, WEL=%s\r\n",
           jedec[0], jedec[1], jedec[2], sr1, (sr1 & 0x02) ? "set -> OK" : "NOT set -> FAIL");
}

static void cmd_flash(void)
{
    int ok = flash_roundtrip();

    printf("[FLASH] addr=0x%06lX len=%d result=%s\r\n", FLASH_TEST_ADDR, FLASH_TEST_LEN,
           ok ? "match" : "MISMATCH");
}

static void cmd_sccb(void)
{
    uint8_t pid = 0, ver = 0;

    printf("[SCCB] read OV7670 PID/VER\r\n");
    g_sccb_platform_bus.read(OV7670_PID_REG, &pid);
    platform_delay_ms(1);
    g_sccb_platform_bus.read(OV7670_VER_REG, &ver);
    printf("[SCCB] pid=0x%02X ver=0x%02X\r\n", pid, ver);
}

static void cmd_uart(void)
{
    const char *s = "Hello!\r\n";

    platform_uart1_write((const uint8_t *)s, (uint16_t)strlen(s));
    printf("[UART] sent 'Hello!' @115200 8N1\r\n");
}

void app_bringup_init(void)
{
    platform_init();
    platform_camera_power_on();

    app_selftest();
}

void app_bringup_run(void)
{
    uint8_t byte;

    if (!platform_uart1_read_byte(&byte)) return;
    platform_uart1_write(&byte, 1);   /* echo */

    if (byte == '\r' || byte == '\n') {
    	const char newline[] = "\r\n";
    	platform_uart1_write((const uint8_t *)newline, 2);

        if (cmd_idx == 0) return;
        cmd_buf[cmd_idx] = '\0';
        cmd_idx = 0;

        metrics_evt_log(METRICS_EVT_CMD_RX);
        if (strcmp(cmd_buf, "help") == 0) printf("Commands: help id i2c spi flash sccb uart data stats statrst evt evtrst stack heap load probe\r\n");
        else if (strcmp(cmd_buf, "id") == 0) app_selftest();
        else if (strcmp(cmd_buf, "i2c") == 0) cmd_i2c();
        else if (strcmp(cmd_buf, "data") == 0) cmd_data();
        else if (strcmp(cmd_buf, "spi") == 0) cmd_spi();
        else if (strcmp(cmd_buf, "flash") == 0) cmd_flash();
        else if (strcmp(cmd_buf, "sccb") == 0) cmd_sccb();
        else if (strcmp(cmd_buf, "uart") == 0) cmd_uart();
        else if (strcmp(cmd_buf, "stats") == 0) metrics_stats();
        else if (strcmp(cmd_buf, "statrst") == 0) metrics_stats_reset();
        else if (strcmp(cmd_buf, "evt") == 0) metrics_evt_dump();
        else if (strcmp(cmd_buf, "evtrst") == 0) metrics_evt_reset();
        else if (strcmp(cmd_buf, "stack") == 0) metrics_stack_report();
        else if (strcmp(cmd_buf, "heap") == 0) metrics_heap_report();
        else if (strcmp(cmd_buf, "load") == 0) metrics_load_report();
        else if (strcmp(cmd_buf, "probe") == 0) metrics_probe_set(!metrics_probe_enabled());
        else printf("[?] unknown: '%s'\r\n", cmd_buf);
        metrics_evt_log(METRICS_EVT_CMD_DONE);
    } else if (cmd_idx < CMD_BUF_SIZE - 1) {
        cmd_buf[cmd_idx++] = (char)byte;
    } else {
        cmd_idx = 0;
    }
}
