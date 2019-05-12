/*
 * siggen_driver.h
 *
 *  Created on: May 9, 2019
 *      Author: ebots
 */

#ifndef SIGGEN_DRIVER_H_
#define SIGGEN_DRIVER_H_

enum pinDirections{
	inputPin = 0,
	outputPin = 1
};

enum XavierGPIONumber{
	gpio428 = 428,
	gpio351 = 351,
	gpio424 = 424,
	gpio256 = 256,
	gpio393 = 393,
	gpio344 = 344,
	gpio251 = 251,
	gpio250 = 250,
	gpio248 = 248,
	gpio257 = 257,
	gpio354 = 354,
	gpio429 = 429,
	gpio249 = 249,
	gpio353 = 353,
	gpio352 = 352,
	gpio495 = 495
};

#define	redLaserEnPin				gpio351
#define	blueLaserEnPin				gpio424
#define	camPwr3V3Pin				gpio256
#define	camPwr1V8Pin				gpio393
#define	camPwr1V2Pin    			gpio344

#define testPin					    gpio352
#define	DLPTriggerPin				gpio250

#define xtrigPin					gpio249

#define HIGH 						1
#define LOW 						0

#define XTRIG_ACTIVE				LOW
#define XTRIG_INACTIVE				HIGH

#define DLP_PULSE_WIDTH				100 			// 100uS
#define DLP_ACTIVE					HIGH
#define DLP_INACTIVE				LOW

#define rLaser_ACTIVE				HIGH
#define rLaser_INACTIVE				LOW

#define bLaser_ACTIVE				HIGH
#define bLaser_INACTIVE				LOW

#define TIMER_STAMP 				1000			// 1000nano second
enum modes { OFF, ONCE, CONTINUOUS };              ///< The available LED modes -- static not useful here
enum STSP_modes {STOP=0, START=1};

#endif /* SIGGEN_DRIVER_H_ */
