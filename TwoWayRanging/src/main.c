/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/zephyr.h>
#include <dw3000_hw.h>
#include <dw3000_spi.h>
#include <deca_probe_interface.h>

void main(void) {
	dw3000_hw_init();
	dw3000_hw_reset();
	dw3000_hw_init_interrupt();
	dw3000_spi_speed_fast();

	int ret = dwt_probe((struct dwt_probe_s*)&dw3000_probe_interf);
	if (ret < 0) {
		printk("DWT Probe failed");
		return;
	}

	printk("Hello World! %s\n", CONFIG_BOARD);
}
