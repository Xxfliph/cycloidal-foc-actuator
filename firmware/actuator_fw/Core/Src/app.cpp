#include "app.h"
#include "main.h"
#include "usbd_cdc_if.h"
#include <cstdio>

void app_main(void)
{
    uint32_t count = 0;
    char msg[64];

    while (true)
    {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

        int len = snprintf(msg, sizeof(msg), "hello %lu, tick=%lu\r\n", count, HAL_GetTick());
        CDC_Transmit_FS(reinterpret_cast<uint8_t*>(msg), static_cast<uint16_t>(len));
        count++;

        HAL_Delay(500);
    }
}
