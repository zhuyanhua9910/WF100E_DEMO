/*
 * CmdHelper.h
 *
 *  Created on: Feb 17, 2022
 *      Author: ITry
 */

#ifndef INC_CMDHELPER_H_
#define INC_CMDHELPER_H_

#include "main.h"

uint8_t SetProductVpp(uint8_t dp);
uint8_t SetProductVdd(uint8_t dp);
void Parse(uint16_t* cmd, uint8_t cmd_lens, uint16_t* out, uint16_t out_lens);

#endif /* INC_CMDHELPER_H_ */
