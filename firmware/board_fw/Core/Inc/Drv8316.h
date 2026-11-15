/*
 * drv8316.h
 *
 *  Created on: Nov 14, 2026
 *      Author: jonny
 */

#ifndef INC_DRV8316_H_
#define INC_DRV8316_H_
#include "main.h"

class Drv8316 {
public:
	Drv8316(SPI_HandleTypeDef* hspi, GPIO_TypeDef* csPort, uint16_t csPin);

	HAL_StatusTypeDef writeRegister(uint8_t address, uint8_t data);

private:
	SPI_HandleTypeDef* hspi_;
	GPIO_TypeDef* csPort_;
	uint16_t csPin_;
	uint16_t buildFrame(bool read, uint8_t address, uint8_t data);
	HAL_StatusTypeDef transfer(uint16_t txFrame, uint16_t& rxFrame);
};




#endif /* INC_DRV8316_H_ */
