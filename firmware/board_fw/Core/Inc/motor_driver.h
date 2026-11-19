/*
 * motor_driver.h
 *
 *  Created on: Nov 1, 2026
 *      Author: jonny
 */

#ifndef INC_MOTOR_DRIVER_H_
#define INC_MOTOR_DRIVER_H_
#include "main.h"

class MotorDriver {

public:
	MotorDriver(TIM_HandleTypeDef* htim, GPIO_TypeDef* offPort,uint16_t offPin, GPIO_TypeDef* phasePort, uint16_t phasePin);
	void start();
	void setDuty(const float duty[3]);
	void enable();
	void disable();

private:
    TIM_HandleTypeDef* htim_;
    GPIO_TypeDef* offPort_;
    uint16_t offPin_;
    GPIO_TypeDef* phasePort_;
    uint16_t phasePin_;

};


#endif /* INC_MOTOR_DRIVER_H_ */
