/*
 * foc_controller.cpp
 *
 *  Created on: Nov 1, 2026
 *      Author: jonny
 */
#include "foc_controller.h"
#include <cmath>

FocController::FocController(float vbus, float vLimit, uint8_t polePairs)
    : vbus_(vbus), vLimit_(vLimit), offset_(0.0f), direction_(1), polePairs_(polePairs)
{
}

void FocController::computeDuties(float vd, float vq, float thetaE, float duty[3]) {
	//clamp v q and vd
	if (vd > vLimit_) {
		vd = vLimit_;
	}
	else if (vd < -vLimit_) {
		vd = -vLimit_;
	}
	if (vq > vLimit_) {
		vq = vLimit_;
	}
	else if (vq < -vLimit_) {
		vq = -vLimit_;
	}

	//Inverse Park
	float cosTheta = cosf(thetaE);
	float sinTheta = sinf(thetaE);

	float vAlpha = vd*cosTheta - vq*sinTheta;
	float vBeta = vd*sinTheta + vq*cosTheta;

	//Inverse Clarke
	float va = vAlpha;
	float vb = -0.5f*vAlpha + SQRT3_OVER_2*vBeta;
	float vc = -0.5f*vAlpha - SQRT3_OVER_2*vBeta;

	//find vmax and vmin
	float vmax = 0.0f;
	float vmin = 0.0f;

	if (va >= vb && va >= vc) {
		vmax = va;
	}
	else if (vb >= va && vb >= vc) {
		vmax = vb;
	}
	else {
		vmax = vc;
	}

	if (va <= vc && va <= vb) {
		vmin = va;
	}
	else if (vb <= vc && vb <= va) {
		vmin = vb;
	}
	else {
		vmin = vc;
	}

	//find common offset
	float voff = -(vmax + vmin)*0.5f;

	//move to center around 0 (so can offset all equally in the next step
	va += voff;
	vb += voff;
	vc += voff;

	//shift all to vbus/2 (=7.5 V at 15V) center
	va+= vbus_*0.5f;
	vb+= vbus_*0.5f;
	vc+= vbus_*0.5f;

	//convert to duty cycles
	duty[0] = va/vbus_;
	duty[1] = vb/vbus_;
	duty[2] = vc/vbus_;

	//clamp values
	for (int i = 0; i < 3; i++) {
		if (duty[i] > 1.0f) {
			duty[i] = 1.0f;
		}
		else if (duty[i] < 0.0f) {
			duty[i] = 0.0f;
		}
	}
}

void FocController::setCalibration(float offset, int8_t direction) {
	offset_ = offset;
	direction_ = direction;
}

float FocController::electricalAngle(float mechAngle) const {
	float elecAngle = fmodf(mechAngle*static_cast<float>(polePairs_)*direction_ - offset_, TWO_PI);
	if (elecAngle < 0.0f) {
		elecAngle += TWO_PI;
	}
	return elecAngle;
}
