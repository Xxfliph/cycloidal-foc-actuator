   #include "app.h"
   #include "main.h"

   void app_main(void)
   {
       while (true)
       {
           HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
           HAL_Delay(500);
       }
   }
