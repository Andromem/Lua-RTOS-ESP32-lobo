/*
 * Lua RTOS, ADC driver
 *
 * Copyright (C) 2015 - 2016
 * IBEROXARXA SERVICIOS INTEGRALES, S.L. & CSS IBÉRICA, S.L.
 * 
 * Author: Jaume Olivé (jolive@iberoxarxa.com / jolive@whitecatboard.org)
 * 
 * All rights reserved.  
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */

#ifndef ADC_H
#define	ADC_H

#include <stdint.h>

#include "driver/adc.h"

#include <drivers/cpu.h>
#include <sys/driver.h>

// ADC channel
typedef struct {
	uint8_t setup;
	uint8_t resolution;
	uint16_t max_val;
} adc_channel_t;

// Resources used by ADC
typedef struct {
	uint8_t pin;
} adc_resources_t;

// ADC errors
#define ADC_ERR_CANT_INIT                (DRIVER_EXCEPTION_BASE(ADC_DRIVER_ID) |  0)
#define ADC_ERR_INVALID_UNIT             (DRIVER_EXCEPTION_BASE(ADC_DRIVER_ID) |  1)
#define ADC_ERR_INVALID_CHANNEL          (DRIVER_EXCEPTION_BASE(ADC_DRIVER_ID) |  2)
#define ADC_ERR_INVALID_ATTENUATION      (DRIVER_EXCEPTION_BASE(ADC_DRIVER_ID) |  4)

driver_error_t *adc_setup(int8_t unit);
driver_error_t *adc_setup_channel(int8_t channel, int8_t resolution, int8_t attenuation);
driver_error_t *adc_read(int8_t channel, int *raw, double *mvols);

#endif	/* ADC_H */
