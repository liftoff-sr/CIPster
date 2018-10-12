/*
 * xorshiftrandom.h
 *
 *  Created on: Dec 1, 2013
 *      Author: mmm
 */

/**
 * @file xorshiftrandom.h
 *
 * The public interface of the XOR shift pseudo-random number generator
 */
#include <stdint.h>

#ifndef CIPSTER_XORSHIFTRANDOM_H_
#define CIPSTER_XORSHIFTRANDOM_H_

/**
 * Function SetXorShiftSeed
 * sets the initial seed for the XOR shift pseudo-random algorithm
 * @param aSeed The initial seed value
 */
void SetXorShiftSeed( uint32_t aSeed );

/**
 * Function NextXorShiftUint32
 * returns the next generated pseudo-random number
 * @return uint32_t - The next pseudo-random number
 */
uint32_t NextXorShiftUint32();

#endif // CIPSTER__XORSHIFTRANDOM_H_
