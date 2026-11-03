/*
 * encoder.cpp
 *
 *  Created on: Nov 2, 2026
 *      Author: jonny
 */
#include "encoder.h"

Encoder::Encoder(I2C_HandleTypeDef* hi2c, uint8_t addr)
	: hi2c_(hi2c), addr_(static_cast<uint16_t>(addr<<1)), rawCount_(0), prevCount_(0), totalCount_(0), firstRead_(true)
{
}


HAL_StatusTypeDef Encoder::update() {
	uint8_t memArray[2];
	HAL_StatusTypeDef memRead;

	memRead = HAL_I2C_Mem_Read(hi2c_, addr_, 0x03, I2C_MEMADD_SIZE_8BIT, memArray, 2, 5);
	if(memRead == HAL_OK) {
		rawCount_ = static_cast<uint16_t>((memArray[0]<<6) | (memArray[1]>>2));
		if (firstRead_) {
			prevCount_ = rawCount_;
			totalCount_ = rawCount_;
			firstRead_ = false;
		}
		else {
			int32_t diff = static_cast<int32_t>(rawCount_) - static_cast<int32_t>(prevCount_);
			if (diff > HALF_TURN) {
				diff = diff - FULL_TURN;
			}
			else if (diff < - HALF_TURN){
				diff = diff + FULL_TURN;
			}
			totalCount_ += diff;
			prevCount_ = rawCount_;
		}
	}
	return memRead;
}

uint16_t Encoder::getRawCount() const {
	return rawCount_;
}

float Encoder::getAngle() const {
	return TWO_PI*(rawCount_/COUNTS_PER_REV);
}

float Encoder::getMultiTurnAngle() const {
	return (static_cast<float>(totalCount_)/COUNTS_PER_REV)*TWO_PI;
}

