/*
 * motor_driver.cpp
 *
 *  Created on: Nov 1, 2026
 *      Author: jonny
 */
#include "motor_driver.h"

MotorDriver::MotorDriver(TIM_HandleTypeDef* htim, GPIO_TypeDef* enPort, uint16_t enPin)
    : htim_(htim), enPort_(enPort), enPin_(enPin)
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
    // DRV_EN pin high
	HAL_GPIO_WritePin(enPort_, enPin_, GPIO_PIN_SET);
}

void MotorDriver::disable()
{
    // DRV_EN pin low
	HAL_GPIO_WritePin(enPort_, enPin_, GPIO_PIN_RESET);
}
