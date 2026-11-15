#include "tim.h"
#include "app.h"
#include "main.h"
#include <cstdio>
#include "foc_controller.h"
#include "motor_driver.h"
//#include "encoder.h"
#include "PIController.h"
#include "usart.h"

void focVelocityTest(void)
{
	//declarations and object creation
	FocController myFocController(15.0f, 3.0f, 11);
	MotorDriver myMotorDriver(&htim1, DRV_OFF_GPIO_Port, DRV_OFF_Pin);
	PIController myPIController(0.3f, 3.0f, 2.0f);
	//Encoder myEncoder(&hi2c1, 0x06);

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
	myFocController.setCalibration(calOffset, 1);
	myPIController.reset();

	//variable declarations for FOC control
	uint8_t failedReads = 0;
	uint8_t printCounter = 0;
	static constexpr float VQ = 1.0f;
	static constexpr bool USE_VELOCITY_LOOP = true;
	static constexpr float STEP_VEL = 10.0f;
	static constexpr uint32_t STEP_DELAY_MS = 3000;
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

			//vq = myPIController.update(error, dt);
		}

	}
}

void app_main() {
	char msg[128];

	int counter = 0;
	while (true) {
		HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
		HAL_Delay(1000);

		int len = snprintf(msg, sizeof(msg), "blink %d\r\n", counter);
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		counter++;

	}
}
