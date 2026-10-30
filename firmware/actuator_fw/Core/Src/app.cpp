#include "tim.h"
#include "app.h"
#include "main.h"
#include "usbd_cdc_if.h"
#include <cstdio>


void app_main(void)
{
    uint32_t count = 0;
    char msg[64];

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
// Duty = CCR / ARR. CRR is
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1062);   // ~25% duty
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 2125);   // 50% duty
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 3187);   // ~75% duty

    while (true)
    {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

        int len = snprintf(msg, sizeof(msg), "hello %lu, tick=%lu\r\n", count, HAL_GetTick());
        CDC_Transmit_FS(reinterpret_cast<uint8_t*>(msg), static_cast<uint16_t>(len));
        count++;

        HAL_Delay(500);
    }
}
