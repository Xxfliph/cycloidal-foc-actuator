/*
 * drv8316.cpp
 *
 *  Created on: Nov 14, 2026
 *      Author: jonny
 */
#include "Drv8316.h"

Drv8316::Drv8316(SPI_HandleTypeDef* hspi, GPIO_TypeDef* csPort, uint16_t csPin)
	: hspi_(hspi), csPort_(csPort), csPin_(csPin)
{
}

//creates the 16bit transfer you want
uint16_t Drv8316::buildFrame(bool read, uint8_t address, uint8_t data) {
	//take the data from the drv and then combine it into one 16 bit unit
	//read 1 bit, address 6 bit, data 8 bit, polarity 1 bit.
	uint16_t returnData = (read << 15 | address << 9 | data);
	int numOnes = 0;

	//for the polarity bit
	for (int i = 0; i < 16; i++) {
		if (((returnData >> i) & 1) == 1) {
			numOnes++;
		}
	}
	if (numOnes % 2 == 1) {
		returnData |= 1 << 8;
	}

	return returnData;
}

//txFrame = transmit, rxFrame = receive. (from the perspective of the MCU)
//this function actually sends data to the DRV
HAL_StatusTypeDef Drv8316::transfer(uint16_t txFrame, uint16_t& rxFrame) {
	HAL_GPIO_WritePin(csPort_, csPin_, GPIO_PIN_RESET);
	HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(hspi_, reinterpret_cast<uint8_t*>(&txFrame), reinterpret_cast<uint8_t*>(&rxFrame), 1, 5);
	HAL_GPIO_WritePin(csPort_, csPin_, GPIO_PIN_SET);


	//DRV datasheet says CS must stay high for at least 400 ns between two messages
	//create timer for that
	//CPU clock is 170MHz = 5.88ns. 400ns/5.88ns = 68, so 68 minimum, but increase to maybe 80 for a safe margin
	uint32_t start = DWT->CYCCNT;
	while (DWT->CYCCNT - start < 80) {}

	return status;
}

//writes the data you want into the register you want

/*
 * Three ways to write a register:
 *
 *   writeRegister(addr, data)
 *       Sends one write. No checking.
 *       Use for BUTTONS only - registers that don't read back what you
 *       wrote: CLR_FLT (resets itself to 0) and REG_LOCK (password write).
 *
 *   writeVerified(addr, data)
 *       writeRegister -> read back -> compare -> retry up to 3 times.
 *       Use for a SWITCH register when you set all 8 bits yourself.
 *
 *   modifyRegister(addr, mask, newBits)
 *       Read -> change only the bits in mask -> writeVerified.
 *       Use for a SWITCH setting that shares its register with other
 *       settings (almost every setting).
 *
 * Quick test: "If I read it right after writing, do I get back exactly
 * what I wrote?"  yes -> modifyRegister / writeVerified,  no -> writeRegister
 */
HAL_StatusTypeDef Drv8316::writeRegister(uint8_t address, uint8_t data) {
    // line 1: frame = buildFrame(...)
	uint16_t frame = buildFrame(false, address, data);

	//make space for the reply since SPI always sends and receives in one interaction
	uint16_t reply = 0;

	//returns a status (HAL_StatusTypeDef) so app.cpp knows whether the data transfer worked or not
	//transfer is what actually sends the transfer message and takes in the receive data
	return transfer(frame, reply);
}

//type& variable is the same as type &variable
HAL_StatusTypeDef Drv8316::readRegister(uint8_t address, uint8_t& data) {
	//read, so true, and data is 0 since were not sending anything, only reading
	uint16_t frame = buildFrame(true, address, 0);

	//make space for reply since SPI always sends and receives in one interaction
	uint16_t reply = 0;

	HAL_StatusTypeDef status = transfer(frame, reply);

	//if data transfer is okay, read the reply straight into data.
	if(status == HAL_OK) {
		data = static_cast<uint8_t>(reply);
	}
	return status;
}

HAL_StatusTypeDef Drv8316::writeVerified(uint8_t address, uint8_t data) {
   //loop three times, if errors more than three times return error
	for (int i = 0; i < 3; i++) {
		//checks if write register worked
		if (writeRegister(address, data) != HAL_OK) {
			continue;
		}
		//checks if read register worked
		uint8_t readBack = 0;
		if (readRegister(address, readBack) != HAL_OK) {
			continue;
		}
		//if the data was read and stored, return success
		if (readBack == data) {
			return HAL_OK;
		}
	}
	//exit loop and return error
	return HAL_ERROR;
}
/*
DRV <-> STM read write procedure:
Write:                                                             Read:
R/W BIT: 0    													   1
STM sends: register number + the value to store                    register number(data = 0 , ignored)
DRV does: stores the value after CS goes high (if parity is OK)    nothing changes (not writing things to it)
DRV replies with: top 8 status, bottom 8 ignored by us             top 8 status, bottom 8 the register's value (it's data)
What we use: nothing from the reply, only HAL_OK status            the bottom 8 bits (which is the data)
 */

//used for writing settings into the DRV, using a read, edit, write sequence.
/*
 * modifyRegister(addr, mask, newBits): mask vs newBits
 *
 *   mask    = WHICH boxes the setting uses.
 *             Always all 1s across the setting's boxes,
 *             slid left to the setting's lowest bit.
 *             e.g. PWM_MODE (bits 2-1):    0b11 << 1  -> 0000 0110
 *
 *   newBits = WHAT goes in those boxes.
 *             The option number from the datasheet,
 *             slid left by the SAME amount as the mask.
 *             e.g. PWM_MODE = 3x (option 2): 0b10 << 1 -> 0000 0100
 *
 *   They only look identical when the option is all 1s,
 *   e.g. BUCK_PS_DIS = 1:  mask = 0b1 << 4,  newBits = 0b1 << 4.
 *
 *   Pattern for any setting:
 *     mask    = (one 1 per box)  << (lowest bit number)
 *     newBits = (option number)  << (lowest bit number)
 */
HAL_StatusTypeDef Drv8316::modifyRegister(uint8_t address, uint8_t mask, uint8_t newBits) {
	uint8_t value = 0;

	//value gets changed by readRegister(), and becomes the current bit sequence in the DRV
	HAL_StatusTypeDef status = readRegister(address, value);
	if (status != HAL_OK) {
		return status;
	}

	//value is the current state of DRV, mask is the two bits you want to change
	//THe goal is to clear the boxes you want to change first in case there is something in there already
	//~mask changes 1s to 0s and 0s to 1s, and when you & it with value, it essentially just clears the boxes you want to change
	//then newBits are the bits that you want to add, already in position. You & it with mask so incase newBits isn't correct, you can
	//only ever change the bits that you want to change, not a neighbour bits (which is a different setting)
	value = static_cast<uint8_t>((value & ~mask) | (newBits & mask));

	//This writes the new settings into DRV.
	//writeVerified calls writeRegister
	return writeVerified(address, value);
}

HAL_StatusTypeDef Drv8316::init() {
	//data sheet says SPI only read 1ms after power up
	HAL_Delay(1);

	//write unlock
	if (writeRegister(3, 0b011) != HAL_OK) {
		return HAL_ERROR;
	}

	//CR6: BUCK_PS_DIS = 1 (no power sequencing at 3.3 V buck)
	if (modifyRegister(8, 0b1 << 4, 1 << 4) != HAL_OK) {
		return HAL_ERROR;
	}

	//set PWM mode
	if (modifyRegister(4, 0b11 << 1, 2 << 1) != HAL_OK) {
		return HAL_ERROR;
	}

	//set current sensing gain
	if (modifyRegister(7, 0b11, 2 << 0) != HAL_OK) {
		return HAL_ERROR;
	}

	//only return ok if everything was written without error
	return HAL_OK;
}
