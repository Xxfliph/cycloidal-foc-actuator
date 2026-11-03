#include "tim.h"
#include "app.h"
#include "main.h"
#include "usbd_cdc_if.h"
#include <cstdio>
#include "i2c.h"
#include "foc_controller.h"
#include "motor_driver.h"
#include "encoder.h"



void app_main(void)
{
//	FocController myFocController(15.0f, 6.0f);
//	MotorDriver myMotorDriver(&htim1, DRV_EN_GPIO_Port, DRV_EN_Pin);
//
//	myMotorDriver.start();
//
//	float electricalAngle = 0.0f;
//	const float TWO_PI = 6.2831853f;
//	float dutyArray[3];

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	Encoder myEncoder(&hi2c1,0x06);

	char msg[128];

	int n = 0;

    while (true)
    {
    	int result = myEncoder.update();
    	if(result == HAL_OK) {
			n = snprintf(msg, sizeof(msg), "count:%u mrad:%d multi:%d raw:%d filt:%d\r\n", myEncoder.getRawCount(), static_cast<int>(myEncoder.getAngle()*1000.0f), static_cast<int>(myEncoder.getMultiTurnAngle()*1000.0f), static_cast<int>(myEncoder.getRawVelocity()*1000.0f), static_cast<int>(myEncoder.getVelocity()*1000.0f));
			CDC_Transmit_FS(reinterpret_cast<uint8_t*>(msg), static_cast<uint16_t>(n));
    	}
    	else {
    		n = snprintf(msg, sizeof(msg), "err:%d\r\n", result);
    		CDC_Transmit_FS(reinterpret_cast<uint8_t*>(msg), static_cast<uint16_t>(n));
    	}


//    	electricalAngle += 0.15f;
//    	if (electricalAngle > TWO_PI) {
//    		electricalAngle -= TWO_PI;
//    	}
//
//		myFocController.computeDuties(0.0f, 6.0f, electricalAngle, dutyArray);
//		myMotorDriver.setDuty(dutyArray);
//
//		int n = snprintf(msg, sizeof(msg), "a:%d b:%d c:%d\r\n",static_cast<int>(dutyArray[0]*1000.0f),static_cast<int>(dutyArray[1]*1000.0f),static_cast<int>(dutyArray[2]*1000.0f));
//		CDC_Transmit_FS(reinterpret_cast<uint8_t*>(msg), static_cast<uint16_t>(n));
//
        HAL_Delay(10);
    }
}
