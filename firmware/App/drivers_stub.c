#include "drivers_stub.h"
#include <stddef.h>

void Drivers_InitAll(void) {}

uint8_t Drivers_Sensors_ReadData(SensorData_t *out_data) {
    if (out_data == NULL) {
        return 0;
    }
    out_data->temperature = 25.0f;
    out_data->humidity = 50.0f;
    out_data->timestamp = 0;
    return 1;
}

void Drivers_UART_ProcessRxChar(uint8_t rx_char) {
    (void)rx_char;
}
