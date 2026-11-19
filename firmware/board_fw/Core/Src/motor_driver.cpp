/*
 * motor_driver.cpp
 *
 *  Created on: Nov 1, 2026
 *      Author: jonny
 */
#include "motor_driver.h"

MotorDriver::MotorDriver(TIM_HandleTypeDef* htim, GPIO_TypeDef* offPort, uint16_t offPin, GPIO_TypeDef* phasePort, uint16_t phasePin)
    : htim_(htim), offPort_(offPort), offPin_(offPin), phasePort_(phasePort), phasePin_(phasePin)
{
}

void MotorDriver::start()
{
	// set all 3 CCRs to 0 (outputs low before starting)
	const float zeroes[3] = {0.0f,0.0f,0.0f};
	setDuty(zeroes);

    // HAL_TIM_PWM_Start on CH1, CH2, CH3
	HAL_TIM_PWM_Start(htim_, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(htim_, TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(htim_, TIM_CHANNEL_3);
}

void MotorDriver::setDuty(const float duty[3])
{
    // read ARR once
	uint32_t arr = __HAL_TIM_GET_AUTORELOAD(htim_);

	uint32_t ccr1 = static_cast<uint32_t>(duty[0]*arr);
	uint32_t ccr2 = static_cast<uint32_t>(duty[1]*arr);
	uint32_t ccr3 = static_cast<uint32_t>(duty[2]*arr);

    // write duty[0] * ARR into CH1, duty[1] into CH2, duty[2] into CH3
	__HAL_TIM_SET_COMPARE(htim_, TIM_CHANNEL_1, ccr1);
	__HAL_TIM_SET_COMPARE(htim_, TIM_CHANNEL_2, ccr2);
	__HAL_TIM_SET_COMPARE(htim_, TIM_CHANNEL_3, ccr3);
}

void MotorDriver::enable()
{
	// PHASE_EN high: phases connected (with PWM at 0, all phases go to ground)
	HAL_GPIO_WritePin(phasePort_, phasePin_, GPIO_PIN_SET);
	// DRV_OFF low: driver ON (the pin's name is "off", so low = not off)
	HAL_GPIO_WritePin(offPort_, offPin_, GPIO_PIN_RESET);
}

void MotorDriver::disable()
{
	// PHASE_EN low: phases disconnected (Hi-Z), no current can flow
	HAL_GPIO_WritePin(phasePort_, phasePin_, GPIO_PIN_RESET);
	// DRV_OFF high: driver OFF (standby)
	HAL_GPIO_WritePin(offPort_, offPin_, GPIO_PIN_SET);
}
