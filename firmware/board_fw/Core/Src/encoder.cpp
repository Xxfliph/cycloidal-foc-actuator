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
	uint8_t txBytes[4] = {0, 0, 0, 0}; //remember, t = transmit and r is receive
	uint8_t rxBytes[4];
	HAL_StatusTypeDef status;

	/*
	 * Pull CS low so the MT6701 reads values and transmits.
	 * 4 bytes = 32 clocks: the MT6701 sends 1 extra bit before A13, so the
	 * 24-bit frame needs 25 clocks. 3 bytes (24 clocks) cuts off the last CRC bit.
	 * Pull CS high to signal that data transfer is over
	 */
	HAL_GPIO_WritePin(csPort_, csPin_, GPIO_PIN_RESET);
	status = HAL_SPI_TransmitReceive(hspi_, txBytes, rxBytes, 4, 5);
	HAL_GPIO_WritePin(csPort_, csPin_, GPIO_PIN_SET);

	if(status != HAL_OK) {
		return status;
	}

	/*
	 * Combine the 4 bytes into one 32-bit number, MSB first:
	 *
	 *   bit:      31      30 ... 17   16 ... 13   12 ... 7   6 ... 0
	 *   contents: extra   A13 ... A0  M3 ... M0   C5 ... C0  extra clocks
	 *
	 * Shift right by 7 to drop the extra clocks, then mask to 24 bits to drop
	 * the extra leading bit. What's left is the real MT6701 frame:
	 *
	 *   frame bit: 23 ... 10   9 ... 6    5 ... 0
	 *   contents:  A13 ... A0  M3 ... M0  C5 ... C0
	 */
	uint32_t word = (static_cast<uint32_t>(rxBytes[0]) << 24) |
	                (static_cast<uint32_t>(rxBytes[1]) << 16) |
	                (static_cast<uint32_t>(rxBytes[2]) << 8)  |
	                (static_cast<uint32_t>(rxBytes[3]) << 0);
	uint32_t frame = (word >> 7) & 0xFFFFFF;

	//declare local variables (instead of rawCount_ / magStatus_) so that after checking
	//the CRC only good values get written into the actual object variables
	uint16_t count = static_cast<uint16_t>((frame >> 10) & 0x3FFF);
	uint8_t magStatus = static_cast<uint8_t>((frame >> 6) & 0xF);
	uint8_t crcReceived = static_cast<uint8_t>(frame & 0x3F);

	// the CRC covers the top 18 bits: angle + magnet status
	uint32_t data18 = frame >> 6;

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
