#!/bin/bash
echo "=== Checking Architecture Boundaries ==="
ERRORS=0

if grep -rn 'stm32f4xx_hal.h' firmware/Devices/ firmware/App/ 2>/dev/null; then
    echo "ERROR: Found stm32f4xx_hal.h in forbidden layers (Devices/ or App/)!"
    ERRORS=1
else
    echo "SUCCESS: No direct HAL dependencies found in Devices/ or App/."
fi

exit $ERRORS
