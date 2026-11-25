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
#include "adc.h"

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

/*
 * Finds the encoder reading that lines up with electrical angle 0, so the FOC math
 * knows where the motor's magnetic field really is.
 * Called once in app_main, after the nFAULT alarm is armed (first time current flows).
 * Returns HAL_OK, or HAL_ERROR after printing which step failed. No shutdown inside:
 * app_main turns the driver off (same pattern as bootDriver).
 *
 * Steps:
 * 1. hold vd = 2.5 V at thetaE = 0 for 1 s -> rotor snaps to a real electrical-0 spot
 * 2. read the encoder (up to 3 tries)
 * 3. check the magnet field strength
 * 4. offset = electrical angle measured there -> save it with setCalibration
 * 5. 0 V on the motor until the control loop starts
 */
HAL_StatusTypeDef calibrate(FocController& foc, MotorDriver& motor, Encoder& enc) {
	int len = 0;
	char msg[128];

	static constexpr float HOLD_VD = 2.5f;
	float duty[3];

	// Step 1: all voltage on the d axis (vd = 2.5 V, vq = 0) at thetaE = 0.
	// d = "direct": pulls the rotor to line up, makes no turning force once it's there.
	// 2.5 V because 1.5 V left the rotor ~50 deg electrical off on the dev rig.
	// (FocController's vLimit is 3.0 V, so 2.5 V isn't clamped.)
	foc.computeDuties(HOLD_VD, 0.0f, 0.0f, duty);
	motor.setDuty(duty);

	// Refresh first so the 1 s hold starts with the full 2 s watchdog timeout.
	// 1 s gives the rotor time to swing to the spot and stop.
	HAL_IWDG_Refresh(&hiwdg);
	HAL_Delay(1000);

	/*
	 * Step 2: read the encoder, up to 3 tries.
	 * Why retry: SPI2 is only turned on inside its first transfer, so the clock line moves
	 * from low to idle-high while CS is already low -> the first read after power-up may
	 * fail its CRC. One bad read shouldn't fail calibration.
	 * The loop stops early as soon as readOk becomes true (the "&& !readOk" part).
	 */
	bool readOk = false;
	for (int i = 0; i < 3 && !readOk; i++) {
		if (enc.update() == HAL_OK) {
			readOk = true;
		}
	}

	// All 3 tries failed -> the stored angle is not a real reading -> don't calibrate with it.
	// Checked before the magnet status, because a failed read leaves that stale too.
	if (readOk == false) {
		len = snprintf(msg, sizeof(msg), "cal failed: encoder read\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		return HAL_ERROR;
	}

	/*
	 * Step 3: field strength = magnet status bits 1-0 (Mg1 Mg0).
	 * | Mg1 Mg0 | meaning        |
	 * |   0 0   | normal  -> OK  |
	 * |   0 1   | too strong     |
	 * |   1 0   | too weak (gap) |
	 * Shift down by 0, then & 0b11 keeps only bits 1-0 (bit 3 loss-of-track, bit 2 button ignored).
	 * Anything but 0 -> the angle can't be trusted.
	 */
	if (((enc.getMagStatus() >> 0) & 0b11) != 0) {
		len = snprintf(msg, sizeof(msg), "cal failed: magnet status\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		return HAL_ERROR;
	}

	/*
	 * Step 4: offset_ is still 0 here, so electricalAngle() gives mech * 11 * direction, wrapped to 0..2pi.
	 * That value IS the offset: after setCalibration, this same rotor position gives thetaE = 0.
	 * Example: mech 1.0 rad -> 11.0 - 6.2832 = 4.7168 -> later 11.0 - 4.7168 = 6.2832 -> wraps to 0.
	 * direction = 1 is a guess for the board (dev rig measured +1).
	 * If the first spin jerks or runs away -> try -1.
	 */
	float offset = foc.electricalAngle(enc.getAngle());
	foc.setCalibration(offset, 1);

	// Step 5: vd = vq = 0 -> all three duties equal (0.5) -> no voltage across the windings
	// -> rotor is free and no current flows until the control loop takes over.
	foc.computeDuties(0.0f, 0.0f, 0.0f, duty);
	motor.setDuty(duty);

	len = snprintf(msg, sizeof(msg), "cal ok offset=%d\r\n", static_cast<int>(offset * 1000.0f));
	HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);

	return HAL_OK;
}

/*
 * Waits until the UART has finished sending a background (interrupt-driven) print.
 *
 * Why: HAL sends ONE line at a time, with no queue. HAL_UART_Transmit (blocking) first
 * checks huart1.gState. If a background line is still going out, it returns HAL_BUSY
 * right away and sends NOTHING -> the message is lost, not delayed.
 * The messages that matter most (why the motor stopped) are blocking prints,
 * so they wait here first.
 *
 * | huart1.gState           | meaning                             |
 * |-------------------------|-------------------------------------|
 * | HAL_UART_STATE_READY    | nothing being sent -> safe to print |
 * | anything else (BUSY_TX) | a line is still going out           |
 *
 * Longest wait: one plot line, ~32 chars x 87 us = ~2.8 ms (well under the 2 s watchdog).
 * Only called on the way to a stop, never in the normal control path.
 */
void waitForUart() {
	while (huart1.gState != HAL_UART_STATE_READY) {
	}
}

/*
* Checked at the start of every pass. The callback already turned the driver off;
* this part does the slow work the interrupt can't do:
* 1. disable() -> sets the same pins through the class, so the object matches the hardware
* 2. print that a fault happened
* 3. read IC Status (register 0) over SPI3 and print each flag (OT, OVP, OCP, ...)
*    -> tells us WHICH fault. NPOR is inverted (1 = normal).
* 4. stop forever. The driver stays off until a reset.
*/
void stopOnFault(Drv8316& drv, MotorDriver& motor) {
	waitForUart();   // a plot line may still be sending -> wait, or the fault message is refused and lost
	char msg[128];
	motor.disable();
	int len = snprintf(msg, sizeof(msg), "nFAULT: driver off (latched)\r\n");
	HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
	uint8_t faults = 0;
	if (drv.readFaults(faults) == HAL_OK) {
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

/*
 * Measures the motor supply voltage (VM) through the ADC. (Voltage, not current.)
 * Why: computeDuties() divides by vbus. Wrong vbus = wrong motor voltage
 *   (code says 12 V, real 15 V -> ask 1.0 V, get 1.25 V).
 * PA3 sees VM / 11 (100k/10k divider). ADC: 0..4095 counts = 0..3.24 V (measured 3V3).
 *   vbus = counts / 4095 x 3.24 x 11   (12 V -> ~1379 counts)
 * Run HAL_ADCEx_Calibration_Start() once at boot first.
 */
HAL_StatusTypeDef readVbus(float& vbus) {
	static constexpr float ADC_MAX_COUNTS = 4095.0f;  // 12-bit
	static constexpr float VDDA_VOLTS = 3.24f;        // ADC reference = 3V3 rail, measured on board A
	static constexpr float VM_DIVIDER = 11.0f;        // R2 100k / R3 10k

	if (HAL_ADC_Start(&hadc1) != HAL_OK) {
		return HAL_ERROR;
	}
	if (HAL_ADC_PollForConversion(&hadc1, 5) != HAL_OK) {
		return HAL_ERROR;
	}

	uint32_t counts = HAL_ADC_GetValue(&hadc1);
	vbus = counts / ADC_MAX_COUNTS * VDDA_VOLTS * VM_DIVIDER;

	return HAL_OK;
}

/*
 * Measures each phase's current-sense reading at ZERO current, so later:
 *   real current = (reading - offset) x ~1.3 mA per count
 * Why: zero isn't exactly 2085 counts (amp error, AVDD 3.30 V vs ADC ref 3.24 V).
 *   A 15-count error would show 20 mA that isn't there.
 * SOx = AVDD/2 + 0.6 V/A x I  ->  0 A = 1.65 V = ~2085 counts.
 * Call after bootDriver(): duty 0 + PHASE_EN high = all low-side FETs on = zero current.
 * Averages 64 readings (3.2 ms). Fails if any phase is more than 200 counts (~0.26 A) off.
 * offsets[3] is filled in directly (an array parameter writes into the caller's array).
 */
HAL_StatusTypeDef measureCurrentOffsets(float offsets[3]) {
	static constexpr int NUM_SAMPLES = 64;
	static constexpr float EXPECTED_ZERO = 2085.0f;   // 1.65 V (AVDD/2) / 3.24 V x 4095
	static constexpr float MAX_ERROR = 200.0f;         // counts, ~0.26 A

	// TIM1 channel 4 (no pin) fires the ADC at count 4249, near the PWM peak,
	// where all low-side FETs are on. Keeps firing every 50 us from now on.
	__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, __HAL_TIM_GET_AUTORELOAD(&htim1) - 1);

	// ADC: convert SOA, SOB, SOC (injected ranks 1-3) every time TIM1 triggers.
	if (HAL_ADCEx_InjectedStart(&hadc1) != HAL_OK) {
		return HAL_ERROR;
	}
	if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4) != HAL_OK) {
		return HAL_ERROR;
	}

	uint32_t sums[3] = {0,0,0};

	// Add up 64 fresh sets. Poll waits for all 3 (needs "End of sequence" in CubeMX).
	for(int i = 0; i < NUM_SAMPLES; i++) {
		if (HAL_ADCEx_InjectedPollForConversion(&hadc1, 5) != HAL_OK) {
			return HAL_ERROR;
		}
		sums[0] += HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
		sums[1] += HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
		sums[2] += HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_3);
	}

	// Average AFTER all samples, then check each phase is near 2085.
	for(int j = 0; j < 3; j++) {
		offsets[j] = sums[j] / static_cast<float>(NUM_SAMPLES);
		if (offsets[j] < EXPECTED_ZERO - MAX_ERROR || offsets[j] > EXPECTED_ZERO + MAX_ERROR) {
			return HAL_ERROR;
		}
	}

	return HAL_OK;
}

void app_main() {

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   // turn on the debug/trace block DWT lives in
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;              // start the cycle counter

	static constexpr bool RUN_MOTOR = false;

	char msg[128];

	// ADC self-calibration: measures the ADC's own small zero error (a few counts) and
	// corrects every later reading. Must run once, before the ADC is first started.
	// Covers all channels: vbus (regular) and SOA/SOB/SOC (injected).
	if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) {
		int len = snprintf(msg, sizeof(msg), "adc cal failed\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		while (true) {
			HAL_IWDG_Refresh(&hiwdg);
		}
	}

	/*
	* Drv8316 class configures the chip and reads its status. It talks through SPI
	* MotorDriver class runs the power stage: PWM duties + on/off. It talks through TIM1 and the GPIO pins
	* Encoder class reads the MT6701 angle + magnet status and checks the CRC. It talks through SPI2.
	* FocController class is pure math (park/Clarke/SVPWM to create duties). No hardware. (vbus, vlimit, polePairs), vbus must match the real supply
	*/
	Drv8316 myDrv8316(&hspi3, DRV_CS_GPIO_Port, DRV_CS_Pin);
	MotorDriver myMotorDriver(&htim1, DRV_OFF_GPIO_Port, DRV_OFF_Pin, PHASE_EN_GPIO_Port, PHASE_EN_Pin);
	Encoder myEncoder(&hspi2, ENC_CS_GPIO_Port, ENC_CS_Pin);
	FocController myFocController(12.0f, 3.0f, 11);
	PIController myPIController(0.3f, 3.0f, 2.0f);   // (kp, ki, outLimit) = your tuned dev-rig gains

	

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

	float currentOffsets[3] = {0.0f, 0.0f, 0.0f};
	if (measureCurrentOffsets(currentOffsets) != HAL_OK) {
		myMotorDriver.disable();
		int len = snprintf(msg, sizeof(msg), "current offsets failed, driver off\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		while (true) {
			HAL_IWDG_Refresh(&hiwdg);
		}
	}
	int len = snprintf(msg, sizeof(msg), "offsets A=%d B=%d C=%d\r\n", static_cast<int>(currentOffsets[0]), static_cast<int>(currentOffsets[1]), static_cast<int>(currentOffsets[2]));
	HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);

	if (RUN_MOTOR == false) {
		int counter = 0;
		while (true) {
			if (faultLatched == true) {
				stopOnFault(myDrv8316, myMotorDriver);
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

			float vbus = 0.0f;
			HAL_StatusTypeDef vbusStatus = readVbus(vbus);
			len = snprintf(msg, sizeof(msg), "vbus status=%d mV=%d\r\n", static_cast<int>(vbusStatus), static_cast<int>(vbus * 1000.0f));
			HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		}
	}

	// Calibrate after arming: if the DRV8316 faults during the 1 s hold, the nFAULT
	// interrupt already shuts it down. On failure: driver off, stop forever, keep the
	// watchdog fed so the chip doesn't reset and try again on its own.
	if (calibrate(myFocController, myMotorDriver, myEncoder) != HAL_OK) {
		myMotorDriver.disable();
		int len = snprintf(msg, sizeof(msg), "cal failed, driver off\r\n");
		HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
		while (true) {
			HAL_IWDG_Refresh(&hiwdg);
		}
	}
	
	//variable declarations for FOC control
	myPIController.reset();
	float dutyArray[3];
	uint8_t failedReads = 0;
	static constexpr uint8_t MAX_FAILED_READS = 20;
	static constexpr float VQ = 1.0f;
	static constexpr bool USE_VELOCITY_LOOP = false;
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

	/*
	* Serial Plotter output without freezing the control loop.
	*
	* Blocking print (HAL_UART_Transmit): CPU waits for every char -> 32 chars = 2.8 ms frozen.
	* Background print (HAL_UART_Transmit_IT): CPU hands over char 1 and returns at once.
	*   UART sends it (87 us) -> interrupt -> HAL's handler hands over the next char (~1 us)
	*   -> ... until the line is done -> gState goes back to READY.
	*   Loop loses ~32 us total, spread out, instead of 2.8 ms in one go.
	*
	* Rules:
	* 1. plotMsg is used ONLY by this print. HAL reads from it in the background while sending,
	*    so writing a new line into it early would mix old and new characters.
	* 2. Only start a print when gState == READY (previous line finished).
	* 3. Every 20 ms: 2.8 ms to send << 20 ms, so the UART is always free by the next one.
	*/
	char plotMsg[64];                                  // only used by the background print
	static constexpr uint32_t PRINT_INTERVAL_MS = 20;  // 50 lines per second
	uint32_t lastPrintMs = HAL_GetTick();

	while (true)
	{
		if (faultLatched == true) {
			stopOnFault(myDrv8316, myMotorDriver);
		}

		HAL_IWDG_Refresh(&hiwdg);

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
		}
		if (failedReads > MAX_FAILED_READS) {
			myMotorDriver.disable();
			myPIController.reset();

				waitForUart();   // same reason: make sure "encoder failed" actually gets sent
			
			int len = snprintf(msg, sizeof(msg), "encoder failed, driver off\r\n");
			HAL_UART_Transmit(&huart1, reinterpret_cast<uint8_t*>(msg), len, 10);
			while (true) {
				HAL_IWDG_Refresh(&hiwdg);
			}
		}

		// Every 20 ms, if the previous line has finished: write a new line into plotMsg,
		// start sending it in the background, and carry on with the loop straight away.
		// With USE_VELOCITY_LOOP = false, vq is the PI's output, not the applied VQ -> ignore it.
		if (HAL_GetTick() - lastPrintMs >= PRINT_INTERVAL_MS) {
			if (huart1.gState == HAL_UART_STATE_READY) {
				lastPrintMs = HAL_GetTick();
				int len = snprintf(plotMsg, sizeof(plotMsg), "target:%d vel:%d vq:%d\r\n",
						static_cast<int>(target * 1000.0f),
						static_cast<int>(myEncoder.getVelocity() * 1000.0f),
						static_cast<int>(vq * 1000.0f));
				HAL_UART_Transmit_IT(&huart1, reinterpret_cast<uint8_t*>(plotMsg), len);
			}
		}
	}
}

