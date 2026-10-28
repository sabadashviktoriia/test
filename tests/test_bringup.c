/* Хостові тести логіки.
 * Файл .c підключається напряму.
 * Шини підмінені fake-реалізацією (fakes/fake_bus.c) замість HAL. */
#include <string.h>
#include "minitest.h"
#include "fakes/fake_bus.h"
#include "../firmware/App/bringup.c"

static void setup(void)
{
    fake_reset();
    memset(&bmp_cal, 0, sizeof bmp_cal);
    bmp_b5 = 0;
    cmd_idx = 0;
}

static void expect_i2c(int i, bool w, uint16_t dev, uint16_t reg, size_t len)
{
    CHECK(i < fake_i2c_log_n);
    if (i >= fake_i2c_log_n) return;
    CHECK_EQ(w, fake_i2c_log[i].is_write);
    CHECK_EQ(dev, fake_i2c_log[i].dev);
    CHECK_EQ(reg, fake_i2c_log[i].reg);
    CHECK_EQ(len, fake_i2c_log[i].len);
}

/* ===================== Перетворення сирих даних ===================== */
static void test_be16(void)
{
    const uint8_t b[] = {0x12, 0x34, 0xFF, 0xFE};
    setup();
    CHECK_EQ(0x1234, be16(b, 0));
    CHECK_EQ(0xFFFE, be16(b, 2));
}

static void test_read_xyz_lsm303d_values(void)
{   
    int x = 0, y = 0, z = 0;
    setup();
    uint8_t *r = &fake_i2c_regs[0x1D][0x28];
    r[0] = 0x00; r[1] = 0x40;            
    r[2] = 0x00; r[3] = 0x80;            
    r[4] = 0xFF; r[5] = 0x7F;            
    CHECK_EQ(BUS_OK, read_xyz(LSM303D_ADDR, LSM303D_OUT_A, 61, 1000, &x, &y, &z));
    CHECK_EQ(999, x); CHECK_EQ(-1998, y); CHECK_EQ(1998, z);
    expect_i2c(0, false, 0x1D, 0x28 | 0x80, 6);      
}

static void test_read_xyz_l3gd20h_values(void)
{   
    int x = 0, y = 0, z = 0;
    setup();
    uint8_t *r = &fake_i2c_regs[0x6B][0x28];
    r[0] = 0x00; r[1] = 0x01;            
    r[2] = 0xFF; r[3] = 0xFF;            
    r[4] = 0x00; r[5] = 0x00;            
    CHECK_EQ(BUS_OK, read_xyz(L3GD20H_ADDR, L3GD20H_OUT, 875, 100, &x, &y, &z));
    CHECK_EQ(2240, x); CHECK_EQ(-8, y); CHECK_EQ(0, z);
}

static void test_read_xyz_bus_error_keeps_outputs(void)
{
    int x = 111, y = 222, z = 333;
    setup();
    fake_i2c_fail_at = 0;
    CHECK_EQ(BUS_ERROR, read_xyz(LSM303D_ADDR, LSM303D_OUT_A, 61, 1000, &x, &y, &z));
    CHECK_EQ(111, x); CHECK_EQ(222, y); CHECK_EQ(333, z);   
}

static void test_read_xyz_nack(void)
{
    int x = 0, y = 0, z = 0;
    setup();
    fake_i2c_nack_addr = LSM303D_ADDR;
    CHECK_EQ(BUS_ERROR, read_xyz(LSM303D_ADDR, LSM303D_OUT_A, 61, 1000, &x, &y, &z));
}

/* ===================== BMP180: приклад із datasheet ===================== */
static void load_cal_into_struct(void)
{
    bmp_cal.ac1 = 408;   bmp_cal.ac2 = -72;   bmp_cal.ac3 = -14383;
    bmp_cal.ac4 = 32741; bmp_cal.ac5 = 32757; bmp_cal.ac6 = 23153;
    bmp_cal.b1 = 6190;   bmp_cal.b2 = 4;      bmp_cal.mb = -32768;
    bmp_cal.mc = -8711;  bmp_cal.md = 2868;
}

static void test_bmp180_datasheet_temperature(void)
{
    setup(); load_cal_into_struct();
    CHECK_EQ(150, bmp180_calc_temp(27898));          
    CHECK(bmp_b5 == 2399 || bmp_b5 == 2400);
}

static void test_bmp180_datasheet_pressure(void)
{
    setup(); load_cal_into_struct();
    bmp180_calc_temp(27898);
    CHECK_EQ(69964, bmp180_calc_pressure(23843));    
}

static void test_bmp180_read_full_sequence(void)
{
    int32_t t = 0, p = 0;
    setup(); fake_bmp180_load_datasheet_example();
    CHECK_EQ(BUS_OK, bmp180_read(&t, &p));
    CHECK_EQ(150, t); CHECK_EQ(69964, p);
    CHECK_EQ(5, fake_i2c_log_n);
    expect_i2c(0, false, 0x77, 0xAA, 22);            
    expect_i2c(1, true,  0x77, 0xF4, 1);  CHECK_EQ(0x2E, fake_i2c_log[1].first);
    expect_i2c(2, false, 0x77, 0xF6, 2);             /* UT */
    expect_i2c(3, true,  0x77, 0xF4, 1);  CHECK_EQ(0x34, fake_i2c_log[3].first);
    expect_i2c(4, false, 0x77, 0xF6, 3);             /* UP */
}

static void test_bmp180_read_errors(void)
{
    int32_t t = 0, p = 0;
    for (int fail = 0; fail < 5; fail++) {
        if (fail == 1 || fail == 3) continue;        
        setup(); fake_bmp180_load_datasheet_example();
        fake_i2c_fail_at = fail;
        CHECK_EQ(BUS_ERROR, bmp180_read(&t, &p));
    }
}

static void test_bmp180_write_error_is_ignored(void)
{
    int32_t t = 0, p = 0;
    setup(); fake_bmp180_load_datasheet_example();
    fake_i2c_fail_at = 1;                            
    bus_status_t s = bmp180_read(&t, &p);
    KNOWN_DEFECT(s == BUS_ERROR, "bmp180_read ігнорує помилку запису команди вимірювання (повертає BUS_OK)");
}

/* ===================== Ідентифікація (self-test) ===================== */
static void test_selftest_lsm303d(void)
{
    setup();
    CHECK_EQ(1, selftest_lsm303d());
    expect_i2c(0, false, 0x1D, 0x0F, 1);             
    expect_i2c(1, true,  0x1D, 0x20, 1); CHECK_EQ(0x5F, fake_i2c_log[1].first);

    setup(); fake_i2c_regs[0x1D][0x0F] = 0x00;       
    CHECK_EQ(0, selftest_lsm303d());

    setup(); fake_i2c_nack_addr = LSM303D_ADDR;      
    CHECK_EQ(0, selftest_lsm303d());

    setup(); fake_i2c_fail_at = 1;                   
    KNOWN_DEFECT(selftest_lsm303d() == 0, "selftest_lsm303d ігнорує помилку запису CTRL1/CTRL2");
}

static void test_selftest_l3gd20h_and_bmp180_ids(void)
{
    setup(); fake_bmp180_load_datasheet_example();
    CHECK_EQ(1, selftest_l3gd20h());
    CHECK_EQ(1, selftest_bmp180());
    fake_i2c_regs[0x6B][0x0F] = 0x00; CHECK_EQ(0, selftest_l3gd20h());
    fake_i2c_regs[0x77][0xD0] = 0x00; CHECK_EQ(0, selftest_bmp180());
}

static void test_selftest_ov7670(void)
{
    setup();
    CHECK_EQ(1, selftest_ov7670());
    CHECK_EQ(0x80, fake_sccb_regs[0x12]);            
    setup(); fake_sccb_regs[0x0A] = 0x00; CHECK_EQ(0, selftest_ov7670());
    setup(); fake_sccb_regs[0x0B] = 0x00; CHECK_EQ(0, selftest_ov7670());
}

/* ===================== SPI-flash W25Q64 ===================== */
static void test_w25_jedec(void)
{
    uint8_t j[3] = {0};
    setup();
    w25_read_jedec(j);
    CHECK_EQ(0xEF, j[0]); CHECK_EQ(0x40, j[1]); CHECK_EQ(0x17, j[2]);
    CHECK_EQ(1, fake_spi_log_n);
    CHECK_EQ(0x9F, fake_spi_log[0].op); CHECK_EQ(1, fake_spi_log[0].tx_len); CHECK_EQ(3, fake_spi_log[0].rx_len);
    CHECK_EQ(0, fake_spi_cs_violations);             
    CHECK_EQ(0, fake_spi_cs_low);                    
}

static void test_selftest_w25q64_wrong_jedec(void)
{
    setup(); fake_spi_jedec[0] = 0x00;
    CHECK_EQ(0, selftest_w25q64());
    setup(); CHECK_EQ(1, selftest_w25q64());
}

static void test_flash_roundtrip_command_sequence(void)
{
    setup();
    CHECK_EQ(1, flash_roundtrip());
    CHECK_EQ(0, fake_spi_ignored_writes);            
    CHECK_EQ(0, fake_spi_cs_violations);

    int i = 0;
    CHECK_EQ(0x06, fake_spi_log[i++].op);
    CHECK_EQ(0x20, fake_spi_log[i].op); CHECK_EQ(0x7FF000, fake_spi_log[i].addr); i++;
    CHECK_EQ(0x05, fake_spi_log[i++].op);            
    while (fake_spi_log[i].op == 0x05) i++;
    CHECK_EQ(0x06, fake_spi_log[i++].op);
    CHECK_EQ(0x02, fake_spi_log[i].op); CHECK_EQ(0x7FF000, fake_spi_log[i].addr);
    CHECK_EQ(4 + FLASH_TEST_LEN, fake_spi_log[i].tx_len); i++;
    CHECK_EQ(0x05, fake_spi_log[i++].op);
    while (fake_spi_log[i].op == 0x05) i++;
    CHECK_EQ(0x03, fake_spi_log[i].op); CHECK_EQ(0x7FF000, fake_spi_log[i].addr);
    CHECK_EQ(FLASH_TEST_LEN, fake_spi_log[i].rx_len); i++;
    CHECK_EQ(i, fake_spi_log_n);                     

    for (int k = 0; k < FLASH_TEST_LEN; k++) CHECK_EQ((k * 7 + 3) & 0xFF, fake_flash[0x7FF000 + k]);
}

static void test_flash_roundtrip_waits_for_busy(void)
{
    setup(); fake_spi_busy_polls = 3;
    CHECK_EQ(1, flash_roundtrip());
    int polls = 0;
    for (int i = 0; i < fake_spi_log_n; i++) if (fake_spi_log[i].op == 0x05) polls++;
    CHECK(polls >= 2 * 4);                           
}

static void test_flash_roundtrip_detects_data_mismatch(void)
{
    setup(); fake_spi_read_corrupt = true;           
    CHECK_EQ(0, flash_roundtrip());

    setup(); fake_spi_fail_at = 5;                   
    CHECK_EQ(0, flash_roundtrip());                  
}

static void test_spi_cs_released_on_error(void)
{
    uint8_t j[3] = {0};
    setup(); fake_spi_fail_at = 0;
    w25_read_jedec(j);
    CHECK_EQ(0, fake_spi_cs_low);                    
    CHECK_EQ(0, j[0]);                               
}

/* ===================== UART-команди ===================== */
static void test_uart_echo_and_unknown_command(void)
{
    setup();
    fake_uart_feed("xyz\r");
    for (int i = 0; i < 4; i++) app_bringup_run();
    CHECK_EQ(0, cmd_idx);                            
    CHECK(fake_uart_tx_len >= 5);                    
    CHECK(memcmp(fake_uart_tx, "xyz\r\r\n", 6) == 0);
}

static void test_uart_long_line_no_overflow(void)
{
    setup();
    for (int i = 0; i < 70; i++) fake_uart_feed("a");
    fake_uart_feed("\r");
    for (int i = 0; i < 71; i++) app_bringup_run();
    CHECK_EQ(0, cmd_idx);
    CHECK(cmd_idx < CMD_BUF_SIZE);
}

static void test_uart_command_id_runs_selftest(void)
{
    setup(); fake_bmp180_load_datasheet_example();
    fake_uart_feed("id\r");
    for (int i = 0; i < 3; i++) app_bringup_run();
    CHECK(fake_i2c_log_n > 0);                       
    CHECK(fake_spi_log_n > 0);
}

/* ===================== Обробники UART-команд ===================== */
static void run_cmd(const char *name)
{
    fake_uart_feed(name);
    fake_uart_feed("\r");
    for (size_t i = 0; i < strlen(name) + 1; i++) app_bringup_run();
}

static bool uart_tx_contains(const char *text)
{
    size_t n = strlen(text);
    for (int i = 0; i + (int)n <= fake_uart_tx_len; i++)
        if (memcmp(&fake_uart_tx[i], text, n) == 0) return true;
    return false;
}

static void test_cmd_i2c(void)
{
    setup();
    run_cmd("i2c");
    CHECK_EQ(4, fake_i2c_log_n);
    expect_i2c(0, false, 0x1D, 0x0F, 1);
    expect_i2c(1, false, 0x6B, 0x0F, 1);
    expect_i2c(2, false, 0x77, 0xD0, 1);
    expect_i2c(3, false, 0x1D, 0x28 | 0x80, 6);
}

static void test_cmd_data(void)
{
    setup(); fake_bmp180_load_datasheet_example();
    run_cmd("data");
    CHECK_EQ(1 + 1 + 5, fake_i2c_log_n);             
    expect_i2c(0, false, 0x1D, 0xA8, 6);
    expect_i2c(1, false, 0x6B, 0xA8, 6);
    expect_i2c(2, false, 0x77, 0xAA, 22);
}

static void test_cmd_spi(void)
{
    setup();
    run_cmd("spi");
    CHECK_EQ(3, fake_spi_log_n);
    CHECK_EQ(0x9F, fake_spi_log[0].op);              
    CHECK_EQ(0x06, fake_spi_log[1].op);              
    CHECK_EQ(0x05, fake_spi_log[2].op);              
}

static void test_cmd_flash(void)
{
    setup();
    run_cmd("flash");
    CHECK_EQ(0, fake_spi_ignored_writes);
    CHECK_EQ(0x03, fake_spi_log[fake_spi_log_n - 1].op);   
}

static void test_cmd_sccb(void)
{
    setup();
    run_cmd("sccb");
    CHECK_EQ(2, fake_sccb_reads);                    
}

static void test_cmd_uart_and_help(void)
{
    setup();
    run_cmd("uart");
    CHECK(uart_tx_contains("Hello!\r\n"));
    setup();
    run_cmd("help");
    CHECK_EQ(0, cmd_idx);
}

static void test_empty_line_is_ignored(void)
{
    setup();
    fake_uart_feed("\r");
    app_bringup_run();
    CHECK_EQ(0, cmd_idx);
    CHECK(memcmp(fake_uart_tx, "\r\r\n", 3) == 0);
}

static void test_app_bringup_init_runs_selftest(void)
{
    setup(); fake_bmp180_load_datasheet_example();
    app_bringup_init();
    CHECK(fake_i2c_log_n > 0);
    CHECK(fake_spi_log_n > 0);
}

/* ===================== Реальні вектори з логічного аналізатора ===================== */

static const uint8_t cap_bmp180_cal[22] = {
    0x19,0x9C, 0xFB,0x72, 0xC6,0x8E, 0x85,0xF0, 0x62,0x60, 0x34,0x0D,
    0x19,0x73, 0x00,0x2D, 0x80,0x00, 0xD1,0xF6, 0x0A,0x94 };

static void test_vector_bmp180_real_capture(void)
{
    int32_t t = 0, p = 0;
    setup();
    memcpy(&fake_i2c_regs[0x77][0xAA], cap_bmp180_cal, sizeof cap_bmp180_cal);
    fake_bmp180_ut = 0x54BB;                         
    fake_bmp180_up = 0x9FD6;                         
    CHECK_EQ(BUS_OK, bmp180_read(&t, &p));

    CHECK_EQ(6556, bmp_cal.ac1);   CHECK_EQ(-1166, bmp_cal.ac2);  CHECK_EQ(-14706, bmp_cal.ac3);
    CHECK_EQ(34288, bmp_cal.ac4);  CHECK_EQ(25184, bmp_cal.ac5);  CHECK_EQ(13325, bmp_cal.ac6);
    CHECK_EQ(6515, bmp_cal.b1);    CHECK_EQ(45, bmp_cal.b2);      CHECK_EQ(-32768, bmp_cal.mb);
    CHECK_EQ(-11786, bmp_cal.mc);  CHECK_EQ(2708, bmp_cal.md);

    CHECK_EQ(237, t);                                
    CHECK_EQ(99817, p);                              
    CHECK(p >= 99790 && p <= 99890);                
}

static void check_accel_vector(const uint8_t raw[6], int ex, int ey, int ez, const char *name)
{
    int x = 0, y = 0, z = 0;
    setup();
    memcpy(&fake_i2c_regs[0x1D][0x28], raw, 6);
    CHECK_EQ(BUS_OK, read_xyz(LSM303D_ADDR, LSM303D_OUT_A, 61, 1000, &x, &y, &z));
    CHECK_EQ(ex, x); CHECK_EQ(ey, y); CHECK_EQ(ez, z);
    long m2 = (long)x * x + (long)y * y + (long)z * z;
    (void)name;
    KNOWN_DEFECT(m2 >= 900L * 900L && m2 <= 1100L * 1100L,
                 "ДАНІ ПОТРЕБУЮТЬ ПЕРЕВІРКИ НА СТЕНДІ: модуль прискорення із запису ≈ 2,26 g, а не 1 g "
                 "(вісь Y = 0x7FF8, майже повна шкала ±2 g)");
}

static void test_vector_accel_real_capture(void)
{
    const uint8_t cap01[6] = {0x3F,0xFE, 0xF8,0x7F, 0x9B,0x43};   
    const uint8_t cap02[6] = {0x4D,0xFE, 0xF8,0x7F, 0x8E,0x43};   
    check_accel_vector(cap01, -27, 1998, 1055, "cap01");
    check_accel_vector(cap02, -26, 1998, 1054, "cap02");
}

static void test_vector_flash_real_capture(void)
{
    static const uint8_t cap04[16] = {0x03,0x0A,0x11,0x18,0x1F,0x26,0x2D,0x34,
                                      0x3B,0x42,0x49,0x50,0x57,0x5E,0x65,0x6C};
    setup();
    CHECK_EQ(1, flash_roundtrip());
    CHECK(memcmp(&fake_flash[0x7FF000], cap04, sizeof cap04) == 0);   
    int erase_seen = 0;
    for (int i = 0; i < fake_spi_log_n; i++) if (fake_spi_log[i].op == 0x20) erase_seen = 1;
    CHECK_EQ(1, erase_seen);
}

static void test_vector_jedec_real_capture(void)
{
    uint8_t j[3] = {0};
    setup();
    w25_read_jedec(j);
    CHECK_EQ(0xEF, j[0]); CHECK_EQ(0x40, j[1]); CHECK_EQ(0x17, j[2]);
}

static void test_vector_uart_hello_real_capture(void)
{
    static const uint8_t cap07[11] = {0x0D,0x0D,0x0A,0x48,0x65,0x6C,0x6C,0x6F,0x21,0x0D,0x0A};
    setup();
    run_cmd("uart");
    CHECK(fake_uart_tx_len >= (int)sizeof cap07);
    CHECK(memcmp(&fake_uart_tx[fake_uart_tx_len - (int)sizeof cap07], cap07, sizeof cap07) == 0);
}

int main(void)
{
    RUN(test_be16);
    RUN(test_read_xyz_lsm303d_values);
    RUN(test_read_xyz_l3gd20h_values);
    RUN(test_read_xyz_bus_error_keeps_outputs);
    RUN(test_read_xyz_nack);
    RUN(test_bmp180_datasheet_temperature);
    RUN(test_bmp180_datasheet_pressure);
    RUN(test_bmp180_read_full_sequence);
    RUN(test_bmp180_read_errors);
    RUN(test_bmp180_write_error_is_ignored);
    RUN(test_selftest_lsm303d);
    RUN(test_selftest_l3gd20h_and_bmp180_ids);
    RUN(test_selftest_ov7670);
    RUN(test_w25_jedec);
    RUN(test_selftest_w25q64_wrong_jedec);
    RUN(test_flash_roundtrip_command_sequence);
    RUN(test_flash_roundtrip_waits_for_busy);
    RUN(test_flash_roundtrip_detects_data_mismatch);
    RUN(test_spi_cs_released_on_error);
    RUN(test_uart_echo_and_unknown_command);
    RUN(test_uart_long_line_no_overflow);
    RUN(test_uart_command_id_runs_selftest);
    RUN(test_cmd_i2c);
    RUN(test_cmd_data);
    RUN(test_cmd_spi);
    RUN(test_cmd_flash);
    RUN(test_cmd_sccb);
    RUN(test_cmd_uart_and_help);
    RUN(test_empty_line_is_ignored);
    RUN(test_app_bringup_init_runs_selftest);
    RUN(test_vector_bmp180_real_capture);
    RUN(test_vector_accel_real_capture);
    RUN(test_vector_flash_real_capture);
    RUN(test_vector_jedec_real_capture);
    RUN(test_vector_uart_hello_real_capture);
    return MT_SUMMARY();
}
