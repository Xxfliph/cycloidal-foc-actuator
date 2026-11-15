/*
 * PIController.h
 *
 *  Created on: Nov 4, 2026
 *      Author: jonny
 */

#ifndef INC_PICONTROLLER_H_
#define INC_PICONTROLLER_H_

class PIController {
public:
	PIController(float kp, float ki, float outLimit);
	float update(float error, float dt);
	void reset();

private:
	float kp_;
	float ki_;
	float outLimit_;
	float integral_ = 0.0f;
};



#endif /* INC_PICONTROLLER_H_ */
