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
	//declarations and object creation
	FocController myFocController(12.0f, 6.0f, 11);
	MotorDriver myMotorDriver(&htim1, DRV_EN_GPIO_Port, DRV_EN_Pin);

	myMotorDriver.start();
	myMotorDriver.enable();

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	Encoder myEncoder(&hi2c1, 0x06);
	float dutyArray[3];

	//print stuff
	char msg[128];
	int n = 0;


	//calibration code
	static constexpr float HOLD_VD = 2.5f;
	myFocController.computeDuties(HOLD_VD, 0.0f, 0.0f, dutyArray);
	myMotorDriver.setDuty(dutyArray);
	HAL_Delay(1000);
	float calOffset = 0.0f;
	if (myEncoder.update() == HAL_OK) {
		calOffset = myFocController.electricalAngle(myEncoder.getAngle());
	}
	else {
		myMotorDriver.disable();
		while (true) {
			n = snprintf(msg, sizeof(msg), "cal failed\r\n");
			CDC_Transmit_FS(reinterpret_cast<uint8_t*>(msg), static_cast<uint16_t>(n));
			HAL_Delay(1000);
		}
	}
	myFocController.setCalibration(calOffset, 1);

	//variable declarations for FOC control
	uint8_t failedReads = 0;
	uint8_t printCounter = 0;
	static constexpr float VQ = 1.0f;

	while (true)
	{
		if (myEncoder.update() == HAL_OK) {
			failedReads = 0;
			float thetaE = myFocController.electricalAngle(myEncoder.getAngle());
			myFocController.computeDuties(0.0f, VQ, thetaE, dutyArray);
			myMotorDriver.setDuty(dutyArray);
		}
		else {
            failedReads++;
            if (failedReads > 20) {
                myMotorDriver.disable();
			}
		}
		printCounter++;
		if (printCounter > 50) {
			printCounter = 0;
			n = snprintf(msg, sizeof(msg), "vel:%d\r\n",
					static_cast<int>(myEncoder.getVelocity() * 1000.0f));
			CDC_Transmit_FS(reinterpret_cast<uint8_t*>(msg), static_cast<uint16_t>(n));
		}
	}
}
