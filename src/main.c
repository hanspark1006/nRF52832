/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Nordic UART Bridge Service (NUS) sample
 */
#include <zephyr/types.h>
#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <soc.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/settings/settings.h>

#include <stdio.h>

#include <zephyr/logging/log.h>

#include "compile_date.h"
#include "m_event_queue.h"
#include "m_ble.h"

#define LOG_MODULE_NAME main
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_APPLICATION_LOG_LEVEL);

static void banner(void)
{
	LOG_INF("==================================================");
	LOG_INF("\tHyunSung TravelTracker");
	LOG_INF("\tBuild Date : %04d-%02d-%02d %02d:%02d:%02d", 
			BUILD_YEAR, BUILD_MONTH, BUILD_DAY, 
			BUILD_HOUR, BUILD_MIN, BUILD_SEC);
	LOG_INF("==================================================");			
}

static void configure_gpio(void)
{
	int err;

	err = dk_leds_init();
	if (err) {
		LOG_ERR("Cannot init LEDs (err: %d)", err);
	}
}

static int init_device(void)
{
	int err = 0;

	do{
		err = m_ble_init();
		if(err){
			LOG_ERR("Ble init error!!");
			break;
		}
	}while(0);

	return err;
}

int main(void)
{
	int err;

	configure_gpio();
	banner();	
	err = init_device();
	if(err){
		printk("init error!!\r\n");
		return -1;
	}

	for (;;) {
		m_event_dispatch();
	}
}
