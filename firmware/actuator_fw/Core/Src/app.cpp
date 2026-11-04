#include "tim.h"
#include "app.h"
#include "main.h"
#include "usbd_cdc_if.h"
#include <cstdio>
#include "i2c.h"
#include "foc_controller.h"
#include "motor_driver.h"
#include "encoder.h"
#include "PIController.h"


void app_main(void)
{
	//declarations and object creation
	FocController myFocController(12.0f, 6.0f, 11);
	MotorDriver myMotorDriver(&htim1, DRV_EN_GPIO_Port, DRV_EN_Pin);
	PIController myPIController(0.1f, 0.0f, 6.0f);
	Encoder myEncoder(&hi2c1, 0x06);

	myMotorDriver.start();
	myMotorDriver.enable();

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;


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
	myPIController.reset();

	//variable declarations for FOC control
	uint8_t failedReads = 0;
	uint8_t printCounter = 0;
	static constexpr float VQ = 1.0f;
	static constexpr bool USE_VELOCITY_LOOP = false;
	static constexpr float STEP_VEL = 10.0f;
	static constexpr uint32_t STEP_DELAY_MS = 1000;
	float vq = 0.0f;

	//timer variable declaration
	uint32_t newTime = 0;
	uint32_t oldTime = DWT->CYCCNT;
	uint32_t startMs = HAL_GetTick();
	static constexpr uint32_t CYCLES_PER_MS = 170000;
	static constexpr float CYCLES_PER_SECOND = 170000000.0f;
	float target = 0.0f;

	while (true)
	{
		newTime = DWT->CYCCNT;
		uint32_t elapsed = newTime-oldTime;
		if (elapsed > CYCLES_PER_MS) {
			oldTime = newTime;

			if (HAL_GetTick()-startMs <	STEP_DELAY_MS) {
				target = 0.0f;
			}
			else {
				target = STEP_VEL;
			}
			float dt = elapsed/CYCLES_PER_SECOND;
			float error = target - myEncoder.getVelocity();
			vq = myPIController.update(error, dt);
		}




		if (myEncoder.update() == HAL_OK) {
			failedReads = 0;
			float thetaE = myFocController.electricalAngle(myEncoder.getAngle());
			if (USE_VELOCITY_LOOP) {
				myFocController.computeDuties(0.0f, vq, thetaE, dutyArray);
			}
			else {
				myFocController.computeDuties(0.0f, VQ, thetaE, dutyArray);
			}
			myMotorDriver.setDuty(dutyArray);
		}
		else {
            failedReads++;
            if (failedReads > 20) {
                myMotorDriver.disable();
                myPIController.reset();
			}
		}
		printCounter++;
		if (printCounter > 50) {
			printCounter = 0;
			n = snprintf(msg, sizeof(msg), "target:%d vel:%d vq:%d\r\n",
					static_cast<int>(target*1000.0f),
					static_cast<int>(myEncoder.getVelocity() * 1000.0f),
					static_cast<int>(vq*1000.0f));
			CDC_Transmit_FS(reinterpret_cast<uint8_t*>(msg), static_cast<uint16_t>(n));
		}
	}
}
