#ifndef FAKE_BUS_H
#define FAKE_BUS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "bus_interface.h"

#define FAKE_LOG_MAX   64
#define FAKE_FLASH_SIZE (8u * 1024u * 1024u)   

/* ---------- I2C ---------- */
typedef struct { bool is_write; uint16_t dev; uint16_t reg; size_t len; uint8_t first; } i2c_log_t;
extern uint8_t   fake_i2c_regs[128][256];  
extern i2c_log_t fake_i2c_log[FAKE_LOG_MAX];
extern int       fake_i2c_log_n;
extern int       fake_i2c_fail_at;         
extern int       fake_i2c_nack_addr;       
extern int32_t   fake_bmp180_ut, fake_bmp180_up;

/* ---------- SPI (W25Q64) ---------- */
typedef struct { uint8_t op; uint32_t addr; size_t tx_len; size_t rx_len; } spi_log_t;
extern spi_log_t fake_spi_log[FAKE_LOG_MAX];
extern int       fake_spi_log_n;
extern uint8_t   fake_flash[FAKE_FLASH_SIZE];
extern uint8_t   fake_spi_jedec[3];
extern int       fake_spi_busy_polls;      
extern int       fake_spi_fail_at;         
extern int       fake_spi_ignored_writes;  
extern int       fake_spi_cs_violations;   
extern bool      fake_spi_cs_low;
extern bool      fake_spi_read_corrupt;    

/* ---------- SCCB (OV7670) ---------- */
extern uint8_t   fake_sccb_regs[256];
extern bool      fake_sccb_fail_all;
extern int       fake_sccb_reads;          

/* ---------- UART1 ---------- */
extern uint8_t   fake_uart_rx[256]; extern int fake_uart_rx_len, fake_uart_rx_pos;
extern uint8_t   fake_uart_tx[1024]; extern int fake_uart_tx_len;
void fake_uart_feed(const char *s);

void fake_reset(void);
void fake_bmp180_load_datasheet_example(void);

#endif
