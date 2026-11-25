#include "tim.h"
#include "app.h"
#include "main.h"
#include <cstdio>
#include "foc_controller.h"
#include "motor_driver.h"
#include "encoder.h"
#include "PIController.h"
#include "usart.h"
#include "Drv8316.h"
#include "spi.h"
#include "iwdg.h"

/*
 * Two flags shared between the nFAULT interrupt and the main code.
 *
 * | flag         | set to true by                         | read by           | meaning when true                |
 * |--------------|----------------------------------------|-------------------|----------------------------------|
 * | faultArmed   | app_main, after bootDriver() succeeds  | the callback      | real faults may now shut down    |
 * | faultLatched | the callback, or the level check       | the blink loop    | a fault happened, driver is off  |
 *
 * volatile = an interrupt can change these, so the compiler must read them
 * from RAM every time instead of reusing an old copy kept in a CPU register.
 * Nothing ever sets them back to false: only a reset clears a fault ("latched").
 */
volatile bool faultArmed = false;    // set by app_main after "drv ready"; read by the callback
volatile bool faultLatched = false;  // set by the callback (or the level check); read by the loop

/*
 * Runs automatically when PB11 (DRV_FAULT = DRV8316 nFAULT) goes from high to low.
 * Chain: PB11 falls -> EXTI15_10_IRQHandler (stm32g4xx_it.c) -> HAL_GPIO_EXTI_IRQHandler
 *        (clears the pending bit) -> this function.
 * ST's copy of this function is "weak" (empty), so the linker uses this one instead.
 * extern "C" keeps the plain C name so ST's C code can find it.
 *
 * Steps:
 * 1. pins 10-15 share one interrupt -> leave if it wasn't the fault pin
 * 2. not armed yet -> leave (at boot, DRV_OFF high can pull nFAULT low: not a real fault)
 * 3. driver off right now: DRV_OFF high (all 6 FETs off), PHASE_EN low (phases Hi-Z)
 *    -> direct register writes, same as HardFault_Handler. myMotorDriver lives inside
 *       app_main, so this function can't see it.
 * 4. leave a note for the main loop
 *
 * No SPI here: the HAL timeout counts with HAL_GetTick(), which only goes up in the TIM6
 * interrupt. TIM6 has the same priority (0), so it can't run while this one is running
 * -> a timeout could never run out. The main loop reads the fault registers instead.
 */
extern "C" void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
	if (pin != DRV_FAULT_Pin) {
		return;
	}
	if (faultArmed == false) {
		return;
	}

	DRV_OFF_GPIO_Port->BSRR = DRV_OFF_Pin;
	PHASE_EN_GPIO_Port->BSRR = PHASE_EN_Pin << 16;
	faultLatched = true;
}

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

//used to print each bit in a register (address is the register number, value is the value of a bit
void printRegister(uint8_t address, uint8_t value) {
	char msg[128];
	int len = snprintf(msg, sizeof(msg), "reg %d = %d%d%d%d%d%d%d%d\r\n", address, (
			value >> 7) & 0b1, (value >> 6) & 0b1, (value >> 5) & 0b1, (value >> 4) & 0b1,
			(value >> 3) & 0b1, (value >> 2) & 0b1, (value >> 1) & 0b1, (value >> 0) & 0b1);
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

	//outputs the entire state (8 bits) of registers 3 - 8.
	for(uint8_t address = 3; address <= 8; address++) {
		uint8_t value = 0;
		if (drv.readRegister(address, value) != HAL_OK) {
			len = snprintf(msg, sizeof(msg), "read register failed\r\n");
			HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
			return HAL_ERROR;
		}
		printRegister(address, value);
	}

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
	 * Encoder class reads the MT6701 angle + magnet status and checks the CRC. It talks through SPI2.
	 */
	Drv8316 myDrv8316(&hspi3, DRV_CS_GPIO_Port, DRV_CS_Pin);
	MotorDriver myMotorDriver(&htim1, DRV_OFF_GPIO_Port, DRV_OFF_Pin, PHASE_EN_GPIO_Port, PHASE_EN_Pin);
	Encoder myEncoder(&hspi2, ENC_CS_GPIO_Port, ENC_CS_Pin);

	char msg[128];

	if(bootDriver(myDrv8316, myMotorDriver) != HAL_OK) {
		myMotorDriver.disable();
		int len =snprintf(msg, sizeof(msg), "boot failed, driver off\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		while (true) {
			HAL_IWDG_Refresh(&hiwdg);
		}
	}

	/*
	 * Arm the fault alarm. bootDriver() has finished: DRV_OFF is low and CLR_FLT has
	 * cleared the boot faults, so from here on nFAULT low means a real fault.
	 *
	 * Level check: the interrupt only runs on a falling EDGE. If nFAULT was already low
	 * before arming, it won't fall again, so no interrupt would ever come.
	 * -> read the pin once now. GPIO_PIN_RESET = pin reads low = fault.
	 */
	faultArmed = true;
	if (HAL_GPIO_ReadPin(DRV_FAULT_GPIO_Port, DRV_FAULT_Pin) == GPIO_PIN_RESET) {
		faultLatched = true;
	}

	int counter = 0;
	while (true) {
		/*
		 * Checked at the start of every pass. The callback already turned the driver off;
		 * this part does the slow work the interrupt can't do:
		 * 1. disable() -> sets the same pins through the class, so the object matches the hardware
		 * 2. print that a fault happened
		 * 3. read IC Status (register 0) over SPI3 and print each flag (OT, OVP, OCP, ...)
		 *    -> tells us WHICH fault. NPOR is inverted (1 = normal).
		 * 4. stop forever. The driver stays off until a reset.
		 */
		if (faultLatched == true) {
			myMotorDriver.disable();
			int len = snprintf(msg, sizeof(msg), "nFAULT: driver off (latched)\r\n");
			HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
			uint8_t faults = 0;
			if (myDrv8316.readFaults(faults) == HAL_OK) {
				printFaults(faults);
			}
			else {
				len = snprintf(msg, sizeof(msg), "read faults failed\r\n");
				HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
			}
			while(true) {
				HAL_IWDG_Refresh(&hiwdg);
			}
		}

		HAL_IWDG_Refresh(&hiwdg);

		HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
		HAL_Delay(1000);

		int len = snprintf(msg, sizeof(msg), "blink %d\r\n", counter);
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		counter++;

		HAL_StatusTypeDef encStatus = myEncoder.update();
		len = snprintf(msg, sizeof(msg), "enc status=%d count=%d mag=%d\r\n", static_cast<int>(encStatus), myEncoder.getRawCount(), myEncoder.getMagStatus());
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
	}
}
