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

//txFrame = transmit, rxFrame = recieve. (from the perspective of the MCU)
HAL_StatusTypeDef Drv8316::transfer(uint16_t txFrame, uint16_t& rxFrame) {
	HAL_GPIO_WritePin(csPort_, csPin_, GPIO_PIN_RESET);
	HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(hspi_, reinterpret_cast<uint8_t*>(&txFrame), reinterpret_cast<uint8_t*>(&rxFrame), 1, 5);
	HAL_GPIO_WritePin(csPort_, csPin_, GPIO_PIN_SET);
	return status;
}

HAL_StatusTypeDef Drv8316::writeRegister(uint8_t address, uint8_t data) {
    // line 1: frame = buildFrame(...)
	uint16_t frame = buildFrame(false, address, data);

	uint16_t reply = 0;

	return transfer(frame, reply);

    // line 2: reply box
    // line 3: return transfer(...)
}

