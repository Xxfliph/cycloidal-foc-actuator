/*
 * encoder.h
 *
 *  Created on: Nov 2, 2026
 *      Author: jonny
 */

#ifndef INC_ENCODER_H_
#define INC_ENCODER_H_
#include "main.h"

class Encoder {
public:
	Encoder(I2C_HandleTypeDef* hi2c, uint8_t addr);
	HAL_StatusTypeDef update();
	uint16_t getRawCount() const;

private:
	I2C_HandleTypeDef* hi2c_;
	uint16_t addr_;
	uint16_t rawCount_;

};




#endif /* INC_ENCODER_H_ */
