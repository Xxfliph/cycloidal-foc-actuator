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
	Encoder(SPI_HandleTypeDef* hspi, GPIO_TypeDef* csPort, uint16_t csPin);
	HAL_StatusTypeDef update();
	uint16_t getRawCount() const;
	float getAngle() const;
	float getMultiTurnAngle() const;
	float getRawVelocity() const;
	float getVelocity() const;
	uint8_t getMagStatus() const;



private:
	SPI_HandleTypeDef* hspi_;
	GPIO_TypeDef* csPort_;
	uint16_t csPin_;
	uint16_t rawCount_;
	uint8_t magStatus_;
	uint16_t prevCount_;
	int32_t totalCount_;
	bool firstRead_;
	uint32_t prevCycles_;
	float rawVelocity_;
	float filteredVelocity_;

	static constexpr int32_t HALF_TURN = 8192;
	static constexpr int32_t FULL_TURN = 16384;
	static constexpr float COUNTS_PER_REV = 16384.0f;
	static constexpr float TWO_PI = 6.2831853f;
	static constexpr float VEL_FILTER_TAU = 0.02f;

	//0b1000011 because that is what the MT6701 data sheet says: x^6 + x + 1. This is equivalent
	//to 0b1000011 (1s in the position of the exponent in the polynomial)
	static constexpr uint32_t CRC_DIVISOR = 0b1000011;
	//private function since only update() will ever call it
	uint8_t computeCrc(uint32_t data18) const;





};




#endif /* INC_ENCODER_H_ */
