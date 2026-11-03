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
	float getAngle() const;
	float getMultiTurnAngle() const;
	float getRawVelocity() const;
	float getVelocity() const;


private:
	I2C_HandleTypeDef* hi2c_;
	uint16_t addr_;
	uint16_t rawCount_;
	uint16_t prevCount_;
	int32_t totalCount_;
	bool firstRead_;
	static constexpr int32_t HALF_TURN = 8192;
	static constexpr int32_t FULL_TURN = 16384;
	static constexpr float COUNTS_PER_REV = 16384.0f;
	static constexpr float TWO_PI = 6.2831853f;

	uint32_t prevCycles_;
	float rawVelocity_;
	float filteredVelocity_;

	static constexpr float VEL_FILTER_TAU = 0.02f;

};




#endif /* INC_ENCODER_H_ */
