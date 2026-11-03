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
	FocController myFocController(12.0f, 6.0f, 11);
	MotorDriver myMotorDriver(&htim1, DRV_EN_GPIO_Port, DRV_EN_Pin);

	myMotorDriver.start();
	myMotorDriver.enable();

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	Encoder myEncoder(&hi2c1, 0x06);

	char msg[128];
	int n = 0;
	float dutyArray[3];

	static constexpr float TWO_PI = 6.2831853f;
	static constexpr float HALF_PI = 1.5707963f;
	static constexpr float HOLD_VD = 2.5f;

	// ---- Calibration: hold at thetaE = 0, measure offset ----
	myFocController.computeDuties(HOLD_VD, 0.0f, 0.0f, dutyArray);
	myMotorDriver.setDuty(dutyArray);
	HAL_Delay(1000);

	float calOffset = 0.0f;
	if (myEncoder.update() == HAL_OK) {
		calOffset = myFocController.electricalAngle(myEncoder.getAngle());
	}
	myFocController.setCalibration(calOffset, 1);

	// ---- Diagnostic: step hold angle by pi/2 every ~2 s ----
	float holdAngle = 0.0f;
	int loopCount = 0;

	while (true)
	{
		loopCount++;
		if (loopCount >= 200) {
			loopCount = 0;
			holdAngle += HALF_PI;
			if (holdAngle >= TWO_PI) {
				holdAngle -= TWO_PI;
			}
		}

		myFocController.computeDuties(HOLD_VD, 0.0f, holdAngle, dutyArray);
		myMotorDriver.setDuty(dutyArray);

		HAL_StatusTypeDef result = myEncoder.update();
		if (result == HAL_OK) {
			float thetaE = myFocController.electricalAngle(myEncoder.getAngle());

			n = snprintf(msg, sizeof(msg), "cmd:%d elec:%d off:%d\r\n", static_cast<int>(holdAngle * 1000.0f), static_cast<int>(thetaE * 1000.0f), static_cast<int>(calOffset * 1000.0f));
			CDC_Transmit_FS(reinterpret_cast<uint8_t*>(msg), static_cast<uint16_t>(n));
		}
		else {
			n = snprintf(msg, sizeof(msg), "err:%d\r\n", static_cast<int>(result));
			CDC_Transmit_FS(reinterpret_cast<uint8_t*>(msg), static_cast<uint16_t>(n));
		}

		HAL_Delay(10);
	}
}

