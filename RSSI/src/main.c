#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/services/nus.h>
#include <bluetooth/services/nus_client.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <math.h>
#include <stdio.h>

#define INITIATOR

#define DEVICE_NAME "BLE_RSSI_TEST"

#define LOG_MODULE_NAME ble_rssi
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#ifdef INITIATOR
#define MEASURED_RSSI_AT_1_M -52
#define ENVIROMENTAL_FACTOR 2

void printDistance(struct bt_scan_device_info *deviceInfo, struct bt_scan_filter_match *filterMatch, bool connectable) {
	double distance = pow(10, ((double)(MEASURED_RSSI_AT_1_M - deviceInfo->recv_info->rssi)) / (ENVIROMENTAL_FACTOR * 10));
	printf("Distance = %.2f m\n", distance);
}

BT_SCAN_CB_INIT(scanCallback, printDistance, NULL, NULL, NULL);
#else
#define DEVICE_NAME_LEN	(sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};
#endif

void main(void)
{
	if (bt_enable(NULL)) {
		printk("Failed to enable Bluetooth\n");
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS))
		settings_load();

#ifdef INITIATOR
	bt_scan_init(NULL);
	bt_scan_cb_register(&scanCallback);

	if (bt_scan_filter_add(BT_SCAN_FILTER_TYPE_NAME, DEVICE_NAME)) {
		printk("Failed to add scan filter\n");
		return;
	}

	if (bt_scan_filter_enable(BT_SCAN_NAME_FILTER, false)) {
		printk("Failed to enable scan filter\n");
		return;
	}

	if (bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE)) {
		printk("Failed to start scan\n");
		return;
	}
#else
	if (bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0)) {
		printk("Advertising failed to start");
		return;
	}
#endif
}
