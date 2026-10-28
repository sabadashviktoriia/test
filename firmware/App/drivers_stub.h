#ifndef DRIVERS_STUB_H
#define DRIVERS_STUB_H

#include <stdint.h>

typedef struct {
    float temperature;
    float humidity;
    uint32_t timestamp;
} SensorData_t;

void Drivers_InitAll(void);
uint8_t Drivers_Sensors_ReadData(SensorData_t *out_data);
void Drivers_UART_ProcessRxChar(uint8_t rx_char);

#endif
