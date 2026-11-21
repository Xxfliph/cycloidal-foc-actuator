/*
 * encoder.cpp
 *
 *  Created on: Nov 2, 2026
 *      Author: jonny
 */
#include "encoder.h"
/*
 * Chip Select (CS): CS low = MT6701 sends its angle on the clock; CS high = it ignores the bus
 * Encoder uses SPI now, needs to know the SPI Bus (2) and the Port (B) and the pin number
 */
Encoder::Encoder(SPI_HandleTypeDef* hspi, GPIO_TypeDef* csPort, uint16_t csPin)
	: hspi_(hspi), csPort_(csPort), csPin_(csPin), rawCount_(0), prevCount_(0), totalCount_(0), firstRead_(true), prevCycles_(0), rawVelocity_(0.0f), filteredVelocity_(0.0f)
{
}


HAL_StatusTypeDef Encoder::update() {
	uint8_t txBytes[3] = {0, 0, 0}; //remember, t = transmit and r is receive
	uint8_t rxBytes[3];
	HAL_StatusTypeDef status;

	/*
	 * Pull CS low so the MT6701 reads values and transmits.
	 * Parameters:
	 * - SPI handle (which SPI hardware),
	 *  -tx pointer (where the bytes to send are),
	 * - rx pointer (where to put the bytes received,
	 * - size (how many bytes),
	 * - timeout (max wait in ms)
	 * Pull CS high to signal that data transfer is over
	 */
	HAL_GPIO_WritePin(csPort_, csPin_, GPIO_PIN_RESET);
	status = HAL_SPI_TransmitReceive(hspi_, txBytes, rxBytes, 3, 5);
	HAL_GPIO_WritePin(csPort_, csPin_, GPIO_PIN_SET);

	if(status == HAL_OK) {

		uint32_t now = DWT->CYCCNT;

		// MT6701 SSI frame: 24 bits, MSB first, 3 bytes
		//   A13..A0 = 14-bit angle (0..16383 = one turn)
		//   M3..M0  = magnet status
		//   C5..C0  = CRC (checks the frame wasn't corrupted)
		//
		//            box: 1    2    3    4    5    6    7    8
		//   rxBytes[0]:  A13  A12  A11  A10  A9   A8   A7   A6
		//   rxBytes[1]:  A5   A4   A3   A2   A1   A0   M3   M2
		//   rxBytes[2]:  M1   M0   C5   C4   C3   C2   C1   C0
		//
		// Unpack the angle: rawCount_ = (rxBytes[0] << 6) | (rxBytes[1] >> 2)
		//   rxBytes[0] << 6 : A13 A12 A11 A10 A9 A8 A7 A6  0  0  0  0  0  0
		//   rxBytes[1] >> 2 :  0   0   0   0   0  0  0  0 A5 A4 A3 A2 A1 A0   (M3, M2 fall off)
		//   OR (|)          : A13 A12 A11 A10 A9 A8 A7 A6 A5 A4 A3 A2 A1 A0
		// (uint8_t is widened to int before shifting, so no bits fall off the top.
		// which is also why you need to cast it back into a uint16_t since its an int now.
		// uint16_t because the angle number is 0 - 16383 which is a 14 bit angle.
		rawCount_ = static_cast<uint16_t>((rxBytes[0]<<6) | (rxBytes[1]>>2));
		if (firstRead_) {
			prevCount_ = rawCount_;
			totalCount_ = rawCount_;
			firstRead_ = false;
			prevCycles_ = now;
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

			uint32_t deltaCycles = now - prevCycles_;
			if (deltaCycles > 0) {
				float deltat = static_cast<float>(deltaCycles)/SystemCoreClock;
				rawVelocity_ = ((static_cast<float>(diff)/COUNTS_PER_REV)/deltat)*TWO_PI;

				float filterStep = deltat/(VEL_FILTER_TAU + deltat);
				filteredVelocity_ += filterStep*(rawVelocity_-filteredVelocity_);
			}
			prevCycles_ = now;
		}
	}
	return status;
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

float Encoder::getRawVelocity() const {
	return rawVelocity_;
}

float Encoder::getVelocity() const {
	return filteredVelocity_;
}
