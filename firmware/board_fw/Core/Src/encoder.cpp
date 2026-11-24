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
	: hspi_(hspi), csPort_(csPort), csPin_(csPin), rawCount_(0), magStatus_(0), prevCount_(0), totalCount_(0), firstRead_(true), prevCycles_(0), rawVelocity_(0.0f), filteredVelocity_(0.0f)
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

	if(status != HAL_OK) {
		return status;
	}


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
	// Unpack the angle: count = (rxBytes[0] << 6) | (rxBytes[1] >> 2)
	//   rxBytes[0] << 6 : A13 A12 A11 A10 A9 A8 A7 A6  0  0  0  0  0  0
	//   rxBytes[1] >> 2 :  0   0   0   0   0  0  0  0 A5 A4 A3 A2 A1 A0   (M3, M2 fall off)
	//   OR (|)          : A13 A12 A11 A10 A9 A8 A7 A6 A5 A4 A3 A2 A1 A0
	// (uint8_t is widened to int before shifting, so no bits fall off the top.
	// which is also why you need to cast it back into a uint16_t since its an int now.
	// uint16_t because the angle number is 0 - 16383 which is a 14 bit angle.
	uint16_t count = static_cast<uint16_t>((rxBytes[0]<<6) | (rxBytes[1]>>2));

	/*
	 * Magnet status M3..M0 (from the MT6701 datasheet, SSI section):
	 *   M1 M0 : 00 = field normal, 01 = magnet too strong, 10 = magnet too weak
	 *   M2    : 1 = push-button detected (not used)
	 *   M3    : 1 = loss of track (angle may be wrong)
	 *
	 * The status is split across two bytes:
	 *            box: 1    2    3    4    5    6    7    8
	 *            bit: 7    6    5    4    3    2    1    0
	 *   rxBytes[1]:  A5   A4   A3   A2   A1   A0   M3   M2
	 *   rxBytes[2]:  M1   M0   C5   C4   C3   C2   C1   C0
	 *
	 * Pattern: shift the wanted bits down to the bottom, then & with a mask.
	 * Mask 0b11 = 0 0 0 0 0 0 1 1 -> keeps the bottom 2 boxes, erases the rest.
	 *
	 *   magHigh = (rxBytes[1] >> 0) & 0b11
	 *     rxBytes[1] >> 0 : A5 A4 A3 A2 A1 A0 M3 M2
	 *     & 0b11          :  0  0  0  0  0  0 M3 M2   (angle bits erased)
	 *
	 *   magLow = (rxBytes[2] >> 6) & 0b11
	 *     rxBytes[2] >> 6 :  0  0  0  0  0  0 M1 M0   (CRC bits fall off)
	 *     & 0b11          :  0  0  0  0  0  0 M1 M0
	 *
	 *   magStatus_ = (magHigh << 2) | magLow
	 *     magHigh << 2    :  0  0  0  0 M3 M2  0  0
	 *     magLow          :  0  0  0  0  0  0 M1 M0
	 *     OR (|)          :  0  0  0  0 M3 M2 M1 M0
	 */

	//declare local variables for magStatus (instead of magStatus_) and count (instead of count_)
	//so that after checking the CRC only good values can be written into the actual object variables
	uint8_t magHigh = static_cast<uint8_t>((rxBytes[1] >> 0) & 0b11);
	uint8_t magLow = static_cast<uint8_t>((rxBytes[2] >> 6) & 0b11);
	uint8_t magStatus = static_cast<uint8_t>((magHigh << 2) | magLow);

	uint8_t crcReceived = static_cast<uint8_t>((rxBytes[2] >> 0) & 0b111111);
	uint32_t data18 = (static_cast<uint32_t>(count) << 4 | magStatus);

	if (computeCrc(data18) != crcReceived) {
		return HAL_ERROR;
	}

	rawCount_ = count;
	magStatus_ = magStatus;

	uint32_t now = DWT->CYCCNT;

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
	return HAL_OK;
}

uint8_t Encoder::computeCrc(uint32_t data18) const {
	//shift 18 bit data left 6 to make room for the 6 bit CRC
	//Work is the number being divided. its the row that is getting operated on
	uint32_t work = data18 << 6;
	//start from the left side (23rd bit) and count down to check for 1
	for(int i = 23; i >= 6; i--) {
		//slide the entire work to the right by i, (basically making everything fall off the
		//right edge except the ith bit so that its at position 0. Since it's at position 0,
		//if you and it with 1, you will get 1 if there is a 1 and 0 if there is a 0. If
		//there is a 1, that means you need to perform the operation
		if(((work >> i) & 0b1) == 1) {
			//XOR work with the divisor shifted left to the left most 1, then reassign the
			//divided work
			work = work ^ (CRC_DIVISOR << (i-6));
		}
	}
	//return work and'ed with the mask so you keep only the last 6 bits and erase everything
	//to the left of them. (since only the last 6 bits are the CRC)
	return static_cast<uint8_t>(work & 0b111111);
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

uint8_t Encoder::getMagStatus() const
{
	return magStatus_;
}
