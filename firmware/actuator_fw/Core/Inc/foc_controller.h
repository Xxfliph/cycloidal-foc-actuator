/*
 * foc_controller.h
 *
 *  Created on: Nov 1, 2026
 *      Author: jonny
 */

#include <cstdint>

#ifndef INC_FOC_CONTROLLER_H_
#define INC_FOC_CONTROLLER_H_

class FocController {
public:
	FocController(float vbus, float vlimit, uint8_t polePairs);
	void computeDuties(float vd, float vq, float thetaE, float duty[3]);
	void setCalibration(float offset, int8_t direction);
	float electricalAngle(float mechAngle) const;

private:
	float vbus_;
	float vLimit_;
	static constexpr float SQRT3_OVER_2 = 0.8660254f;
	uint8_t polePairs_;
	float offset_;
	int8_t direction_;
	static constexpr float TWO_PI = 6.2831853f;
};



#endif /* INC_FOC_CONTROLLER_H_ */
