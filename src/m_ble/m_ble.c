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

#include <dk_buttons_and_leds.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

#include <bluetooth/gatt_dm.h>
#include <bluetooth/services/nus.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/settings/settings.h>

#include <stdio.h>

#include <zephyr/logging/log.h>

#include "m_ble.h"
#include "m_event_queue.h"

#define LOG_MODULE_NAME ble
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BLE_MODULE_LOG_LEVEL);

#define STACKSIZE CONFIG_BT_NUS_THREAD_STACK_SIZE
#define PRIORITY 7

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN	(sizeof(DEVICE_NAME) - 1)

static K_SEM_DEFINE(ble_init_ok, 0, 1);

static struct bt_conn *current_conn;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static const struct gpio_dt_spec lte_wakeup = GPIO_DT_SPEC_GET(DT_NODELABEL(lte0), gpios);

#define RUN_STATUS_LED DK_LED1
#define RUN_LED_BLINK_INTERVAL 1000

enum{
	eBLE_CON_IDLE,
	eBLE_CONNECTED,
	eBLE_DISCONNECTED
};

static struct{
	uint8_t con_status;
} m_cfg = {
	.con_status = eBLE_CON_IDLE
};

static void discover_all_completed(struct bt_gatt_dm *dm, void *ctx)
{
	char uuid_str[37];

	const struct bt_gatt_dm_attr *gatt_service_attr =
			bt_gatt_dm_service_get(dm);
	const struct bt_gatt_service_val *gatt_service =
			bt_gatt_dm_attr_service_val(gatt_service_attr);

	size_t attr_count = bt_gatt_dm_attr_cnt(dm);

	bt_uuid_to_str(gatt_service->uuid, uuid_str, sizeof(uuid_str));
	printk("Found service %s\n", uuid_str);
	printk("Attribute count: %d\n", attr_count);

	bt_gatt_dm_data_print(dm);
	bt_gatt_dm_data_release(dm);

	bt_gatt_dm_continue(dm, NULL);
}

static void discover_all_service_not_found(struct bt_conn *conn, void *ctx)
{
	printk("No more services\n");
}

static void discover_all_error_found(struct bt_conn *conn, int err, void *ctx)
{
	printk("The discovery procedure failed, err %d\n", err);
}

static struct bt_gatt_dm_cb discover_all_cb = {
	.completed = discover_all_completed,
	.service_not_found = discover_all_service_not_found,
	.error_found = discover_all_error_found,
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("Connection failed (err %u)", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected %s", addr);
	if(IS_ENABLED(CONFIG_BT_GATT_DM)){
		err = bt_gatt_dm_start(conn, NULL, &discover_all_cb, NULL);
		if (err) {
			printk("Failed to start discovery (err %d)\n", err);
		}
	}else{
		current_conn = bt_conn_ref(conn);
	}
	push_event0(EVT_connect);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s (reason %u)", addr, reason);

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
	//dk_set_led_off(0);
	push_event0(EVT_disconnected);
}

#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d\n", addr, level,
			err);
	}
}
#endif

static struct bt_conn_cb conn_callbacks = {
	.connected    = connected,
	.disconnected = disconnected,
#ifdef CONFIG_BT_GATT_NUS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};
#if defined(CONFIG_BT_NUS_SECURITY_ENABLED)
static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_confirm(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	__ASSERT_NO_MSG(!pairing_confirmation_conn);
	pairing_conn = bt_conn_ref(conn);

	printk("Pairing confirmation required for %s\n", addr);
	printk("Press Button 1 to confirm, Button 2 to reject.\n");
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d\n", addr, reason);

	if (pairing_conn) {
		bt_conn_unref(pairing_conn);
		pairing_conn = NULL;
	}
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
	.pairing_confirm = pairing_confirm,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif
static void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data,
			  uint16_t len)
{
	char addr[BT_ADDR_LE_STR_LEN] = {0};

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, ARRAY_SIZE(addr));

	LOG_INF("Received data from: %s ", addr);
	push_event0_param(EVT_received, (void *)data, len);
}

int m_ble_write_text(void)
{
	int err;
 	char buf[20]={0x02, 0x14, 0xA0}, dev_id[8] = {0x07, 0x2E, 0x17, 0x02, 0x1B, 0x07, 0x15, 0x5C};	

	memcpy(&buf[3], dev_id, 8);
	buf[18] = 0xEF;
	buf[19] = 0xFF;

	err = bt_nus_send(NULL, buf, 20);
	REPORT_IF_ERROR(err);
	if (err) {
		LOG_WRN("Failed to send data over BLE connection");
		return 1;
	}

	return 0;
}

static struct bt_nus_cb nus_cb = {
	.received = bt_receive_cb,
};

int m_ble_init(void)
{
	int err;

	if(!device_is_ready(lte_wakeup.port)){
		return 1;
	}
	
	err = gpio_pin_configure_dt(&lte_wakeup, GPIO_OUTPUT_INACTIVE);
	if(err){
		return 1;
	}
	gpio_pin_set_dt(&lte_wakeup, 0);
	// struct bt_conn *conn = current_conn;

	// current_conn = NULL;
	bt_conn_cb_register(&conn_callbacks);

	if(IS_ENABLED(CONFIG_BT_LBS_SECURITY_ENABLED)){
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			printk("Failed to register authorization callbacks.\n");
			return 1;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			printk("Failed to register authorization info callbacks.\n");
			return 1;
		}
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Ble Enable Error!!");
		return err;
	}
	
	LOG_INF("Bluetooth initialized");

	k_sem_give(&ble_init_ok);
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}
	
	err = bt_nus_init(&nus_cb);
	if (err) {
		LOG_ERR("Failed to initialize UART service (err: %d)", err);
		return 1;
	}	

	LOG_INF("BLE Adv Start!!");
	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if(err){
		LOG_ERR("BLE Adv start error!!");
		return 1;
	}

	// int blink_status = 0;
	// uint8_t send_buf[20];
	// uint16_t interval = RUN_LED_BLINK_INTERVAL;
	// while(1)
	// {
	// 	if(m_cfg.con_status == eBLE_CONNECTED){
	// 		sprintf(send_buf, "Hyunsung %d", blink_status);
	// 		if (bt_nus_send(NULL, send_buf, 20)) {
	// 			LOG_WRN("Failed to send data over BLE connection");		
	// 			interval = RUN_LED_BLINK_INTERVAL * 2;
	// 		}else{
	// 			interval = RUN_LED_BLINK_INTERVAL;
	// 		}
	// 	}else if(m_cfg.con_status == eBLE_CON_IDLE){
	// 		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
	// 		LOG_WRN("Wait Connection..");
	// 	}		
	// 	k_sleep(K_MSEC(interval));
	// }
	return err;
}

static void On_Receive_data(void *data, uint16_t size)
{
	uint8_t buf[25];
	
	sprintf(buf, "Received Data len [%d]", size);
	if (bt_nus_send(NULL, buf, 20)) {
		LOG_WRN("Failed to send data over BLE connection");
	}	
}

static void on_Enable_LTE_Module(void)
{
	gpio_pin_set_dt(&lte_wakeup, 1);
	k_sleep(K_MSEC(100));
	gpio_pin_set_dt(&lte_wakeup, 0);
}

static void evt_handler(event_t const* evt, void* p_context)
{
	switch(evt->event)
	{
		case EVT_connect:
			m_cfg.con_status = eBLE_CONNECTED;
			break;
		case EVT_disconnected:
			m_cfg.con_status = eBLE_DISCONNECTED;
			on_Enable_LTE_Module();
			break;
		case EVT_received:
			On_Receive_data(evt->p_event_data, evt->event_data_size);
			break;
		default:
			break;
	}
}

void ble_write_thread(void)
{
	int blink_status = 0;
	static uint8_t old_status = eBLE_CON_IDLE;

	/* Don't go any further until BLE is initialized */
	k_sem_take(&ble_init_ok, K_FOREVER);

	LOG_DBG("Run ble watch thread");
	for (;;) {
		/* Wait indefinitely for data to be sent over bluetooth */
		if(m_cfg.con_status != eBLE_CONNECTED){
			dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
			LOG_DBG("Wait Connection...");
		}else{
			if(old_status != m_cfg.con_status){
				dk_set_led(RUN_STATUS_LED, 1);
			}
			m_ble_write_text();
		}
		old_status = m_cfg.con_status;

		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}

REGISTER_EVT_HANDLER() = {
    .handler = evt_handler,
    .p_context = NULL,
};

K_THREAD_DEFINE(ble_write_thread_id, STACKSIZE, ble_write_thread, NULL, NULL,
		NULL, PRIORITY, 0, 0);
