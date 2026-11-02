/*
 * foc_controller.h
 *
 *  Created on: Nov 1, 2026
 *      Author: jonny
 */



#ifndef INC_FOC_CONTROLLER_H_
#define INC_FOC_CONTROLLER_H_

class FocController {
public:
	FocController(float vbus, float vlimit);
	void computeDuties(float vd, float vq, float thetaE, float duty[3]);

private:
	float vbus_;
	float vLimit_;
	static constexpr float SQRT3_OVER_2 = 0.8660254f;
};



#endif /* INC_FOC_CONTROLLER_H_ */
