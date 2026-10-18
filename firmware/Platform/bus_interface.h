#ifndef BUS_INTERFACE_H
#define BUS_INTERFACE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUS_OK    = 0,
    BUS_ERROR = 1
} bus_status_t;

typedef bus_status_t (*i2c_read_fn)(uint16_t dev_addr, uint16_t reg_addr,
                                    uint8_t reg_len, uint8_t *data, size_t len);

typedef bus_status_t (*i2c_write_fn)(uint16_t dev_addr, uint16_t reg_addr,
                                     uint8_t reg_len, const uint8_t *data, size_t len);

typedef struct {
    i2c_read_fn  read;
    i2c_write_fn write;
} i2c_bus_t;

typedef bus_status_t (*spi_transfer_fn)(const uint8_t *tx_data, uint8_t *rx_data, size_t len);
typedef void (*spi_cs_control_fn)(bool state);
typedef struct {
    spi_transfer_fn   transfer;
    spi_cs_control_fn cs_control;
} spi_bus_t;

typedef bus_status_t (*sccb_read_fn)(uint8_t reg, uint8_t *data);
typedef bus_status_t (*sccb_write_fn)(uint8_t reg, uint8_t value);

typedef struct {
    sccb_read_fn  read;
    sccb_write_fn write;
} sccb_bus_t;

#ifdef __cplusplus
}
#endif

#endif
