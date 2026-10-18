#ifndef BUS_ADAPTERS_H
#define BUS_ADAPTERS_H

#include <stdint.h>
#include "bus_interface.h"

extern const i2c_bus_t g_i2c_platform_bus;
extern const spi_bus_t g_spi_platform_bus;
extern const sccb_bus_t g_sccb_platform_bus;

void platform_init(void);
void platform_delay_ms(uint32_t ms);
void platform_camera_power_on(void);
int  platform_uart1_read_byte(uint8_t *byte);
void platform_uart1_write(const uint8_t *data, uint16_t len);

#endif
