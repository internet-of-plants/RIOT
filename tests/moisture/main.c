/*
 * Copyright (C) 2015 HAW Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser General
 * Public License v2.1. See the file LICENSE in the top level directory for more
 * details.
 */

/**
 * @ingroup tests
 * @{
 *
 * @file
 * @brief       Test case for moisture sensor
 *
 * @author      Peter Kietzmann <peter.kietzmann@haw-hamburg.de>
 *
 * @}
 */

#include <stdio.h>

#include "cpu.h"
#include "board.h"
#include "xtimer.h"
#include "periph/adc.h"
#include "periph/gpio.h"


#if ADC_NUMOF < 1
#error "Please enable at least 1 ADC device to run this test"
#endif

#define ADC_NUM         0
#define ADC_CH          5
#define RES             ADC_RES_12BIT
#define DELAY           (100 * 1000U)

#define GPIO_VCC_PORT   1
#define GPIO_VCC_PIN    2

/* Compare http://www.watr.li/Sensing-moisture.html for setup */


int main(void)
{
    int value;

    puts("\nRIOT ADC/moisture sensor test\n");


    /* initialize GPIO pin as power supply */
    gpio_init(GPIO_PIN(GPIO_VCC_PORT, GPIO_VCC_PIN), GPIO_DIR_OUT, 0);

    /* initialize ADC device */
    printf("Initializing ADC @ %i bit resolution", (6 + (2* RES)));
    if (adc_init(ADC_NUM, RES) == 0) {
        puts("    ...[ok]");
    }
    else {
        puts("    ...[failed]");
        return 1;
    }

    puts("\n");

    while (1) {

        gpio_set(GPIO_PIN(GPIO_VCC_PORT, GPIO_VCC_PIN));
        xtimer_usleep(1000);
        value = adc_sample(ADC_NUM, ADC_CH);
        gpio_clear(GPIO_PIN(GPIO_VCC_PORT, GPIO_VCC_PIN));

        printf("ADC_%i-CH%i: %4i \n", ADC_NUM, ADC_CH, value);
        /* sleep a little while */
        xtimer_usleep(DELAY);
    }

    return 0;
}
