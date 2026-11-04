/*
 * PIController.cpp
 *
 *  Created on: Nov 4, 2026
 *      Author: jonny
 */
#include "PIController.h"

PIController::PIController(float kp, float ki, float outLimit)
	: kp_(kp), ki_(ki), outLimit_(outLimit)
{
}

void PIController::reset()
{
	integral_ = 0.0f;
}

float PIController::update(float error, float dt)
{

	if (dt > 0.0f) {
		integral_ += ki_*error*dt;
		if (integral_ > outLimit_) {
			integral_ = outLimit_;
		}
		else if(integral_ < -outLimit_) {
			integral_ = -outLimit_;
		}
	}

	float output = kp_*error + integral_;
	if (output > outLimit_) {
		output = outLimit_;
	}
	else if (output < -outLimit_) {
		output = -outLimit_;
	}


	return output;
}
