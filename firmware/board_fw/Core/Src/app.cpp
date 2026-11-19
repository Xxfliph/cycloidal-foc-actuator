#include "tim.h"
#include "app.h"
#include "main.h"
#include <cstdio>
#include "foc_controller.h"
#include "motor_driver.h"
//#include "encoder.h"
#include "PIController.h"
#include "usart.h"
#include "Drv8316.h"
#include "spi.h"



void focVelocityTest(void)
{
	//declarations and object creation
	FocController myFocController(15.0f, 3.0f, 11);
	MotorDriver myMotorDriver(&htim1, DRV_OFF_GPIO_Port, DRV_OFF_Pin, PHASE_EN_GPIO_Port, PHASE_EN_Pin);
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

/*
 * This function prints the fault fault flags in words.
 * Note that NPOR is opposite from other flags. For every other flag, 1 means there's a problem. For NPOR, 1 means
 * there's no problem. (the N before it stands for not).
 * printFaults() takes in 8 boxes from readFaults() and sends one line over UART that shows each flag by name with 0 or 1
 * Each value is extracted by shifting its position to 0, then "and"ing it with 1 (0b1) to keep only that box
 */
void printFaults(uint8_t faults) {
	char msg[128];

	uint8_t fault = (faults >> 0) & 0b1;
	uint8_t ot = (faults >> 1) & 0b1;
	uint8_t ovp	= (faults >> 2) & 0b1;
	//NPOR is inverted: 1 = normal, 0 = power-on reset not cleared yet
	uint8_t npor = (faults >> 3) & 0b1;
	uint8_t ocp = (faults >> 4) & 0b1;
	uint8_t spi	= (faults >> 5) & 0b1;
	uint8_t bk = (faults >> 6) & 0b1;

	int len = snprintf(msg, sizeof(msg), "FAULT=%d OT=%d OVP=%d NPOR=%d OCP=%d SPI=%d BK=%d\r\n", fault, ot, ovp, npor, ocp, spi, bk);
	HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
}

HAL_StatusTypeDef bootDriver(Drv8316& drv, MotorDriver& motor) {
	char msg[128];
	int len = 0;

	if (drv.init() != HAL_OK) {
		len = snprintf(msg, sizeof(msg), "drv init failed\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		return HAL_ERROR;
	}
	len =snprintf(msg, sizeof(msg), "drv init ok\r\n");
	HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);

	motor.start();
	motor.enable();
	//let the driver settle before the next steps
	HAL_Delay(1);

	//read the current flags
	uint8_t faults = 0;
	if (drv.readFaults(faults) != HAL_OK) {
		len =snprintf(msg, sizeof(msg), "read faults failed\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		return HAL_ERROR;
	}
	len =snprintf(msg, sizeof(msg), "before clear: \r\n");
	HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
	printFaults(faults);

	//resets all latched flags, then the chip re-checks everything. Any problem that still happening gets flagged again.
	//flags whose cause is gone stay cleared
	if (drv.clearFaults() != HAL_OK) {
		len =snprintf(msg, sizeof(msg), "clear faults failed\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		return HAL_ERROR;
	}

	//read the persisting flags that remained after clearFaults. The remaining flags are real issues.
	if (drv.readFaults(faults) != HAL_OK) {
		len =snprintf(msg, sizeof(msg), "read faults failed\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		return HAL_ERROR;
	}
	len = snprintf(msg, sizeof(msg), "after clear: \r\n");
	HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
	printFaults(faults);

	if (((faults >> 0) & (0b1)) == 1) {
		len =snprintf(msg, sizeof(msg), "fault still present, driver off\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		return HAL_ERROR;
	}
	len =snprintf(msg, sizeof(msg), "drv ready\r\n");
	HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);

	return HAL_OK;
}

void app_main() {

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   // turn on the debug/trace block DWT lives in
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;              // start the cycle counter

	/*
	 * Drv8316 class configures the chip and reads its status. It talks through SPI
	 * MotorDriver class runs the power stage: PWM duties + on/off. It talks through TIM1 and the GPIO pins
	 */
	Drv8316 myDrv8316(&hspi3, DRV_CS_GPIO_Port, DRV_CS_Pin);
	MotorDriver myMotorDriver(&htim1, DRV_OFF_GPIO_Port, DRV_OFF_Pin, PHASE_EN_GPIO_Port, PHASE_EN_Pin);

	char msg[128];

	if(bootDriver(myDrv8316, myMotorDriver) != HAL_OK) {
		myMotorDriver.disable();
		int len =snprintf(msg, sizeof(msg), "boot failed, driver off\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		while (true) {}
	}

	int counter = 0;
	while (true) {
		HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
		HAL_Delay(1000);

		int len = snprintf(msg, sizeof(msg), "blink %d\r\n", counter);
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		counter++;

	}
}
