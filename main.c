/**
 * Copyright (c) 2014 - 2018, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/**
 * @brief BLE LED Button Service central and client application main file.
 *
 * This file contains the source code for a sample client application using the LED Button service.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrf_pwr_mgmt.h"
#include "app_timer.h"
#include "boards.h"
#include "bsp.h"
#include "bsp_btn_ble.h"
#include "ble.h"
#include "ble_hci.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_db_discovery.h"
#include "ble_lbs_c.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_scan.h"
#include "nrf_drv_timer.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#define CENTRAL_SCANNING_LED            BSP_BOARD_LED_0                     /**< Scanning LED will be on when the device is scanning. */
#define CENTRAL_CONNECTED_LED           BSP_BOARD_LED_1                     /**< Connected LED will be on when the device is connected. */
#define LEDBUTTON_LED                   BSP_BOARD_LED_2                     /**< LED to indicate a change of state of the the Button characteristic on the peer. */

#define SCAN_INTERVAL                   0x00A0                              /**< Determines scan interval in units of 0.625 millisecond. */
#define SCAN_WINDOW                     0x0050                              /**< Determines scan window in units of 0.625 millisecond. */
#define SCAN_DURATION                   0x0000                              /**< Timout when scanning. 0x0000 disables timeout. */

#define MIN_CONNECTION_INTERVAL         MSEC_TO_UNITS(7.5, UNIT_1_25_MS)    /**< Determines minimum connection interval in milliseconds. */
#define MAX_CONNECTION_INTERVAL         MSEC_TO_UNITS(30, UNIT_1_25_MS)     /**< Determines maximum connection interval in milliseconds. */
#define SLAVE_LATENCY                   0                                   /**< Determines slave latency in terms of connection events. */
#define SUPERVISION_TIMEOUT             MSEC_TO_UNITS(4000, UNIT_10_MS)     /**< Determines supervision time-out in units of 10 milliseconds. */

#define LEDBUTTON_BUTTON_PIN            BSP_BUTTON_0                        /**< Button that will write to the LED characteristic of the peer */
#define BUTTON_DETECTION_DELAY          APP_TIMER_TICKS(50)                 /**< Delay from a GPIOTE event until a button is reported as pushed (in number of timer ticks). */

#define APP_BLE_CONN_CFG_TAG            1                                   /**< A tag identifying the SoftDevice BLE configuration. */
#define APP_BLE_OBSERVER_PRIO           3                                   /**< Application's BLE observer priority. You shouldn't need to modify this value. */


//Blink master configuration
#define MAX_RSSI_BUFF_SIZE          25
#define RSSI_THRESHOLD              -50
#define DISCONNECTION_RSSI_THRESHOLD              -60
#define BLINK_TIME_INTERVAL_MS      500

NRF_BLE_SCAN_DEF(m_scan);                                       /**< Scanning module instance. */
BLE_LBS_C_DEF(m_ble_lbs_c);                                     /**< Main structure used by the LBS client module. */
NRF_BLE_GATT_DEF(m_gatt);                                       /**< GATT module instance. */
BLE_DB_DISCOVERY_DEF(m_db_disc);                                /**< DB discovery module instance. */

static char const m_target_periph_name[] = "Nordic_Blinky";     /**< Name of the device we try to connect to. This name is searched in the scan report data*/
static char const periph_uuid[] = "23D1BCEA5F782315DEEF121223150000";

const nrf_drv_timer_t TIMER_LED = NRF_DRV_TIMER_INSTANCE(1);

uint8_t ledStatus = 0;

typedef struct __attribute__((packed)) {
    uint8_t header;
    uint8_t address[6];
    uint8_t payload[37];
} adv_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t adv_len;
    uint8_t adv_type;
    uint8_t data[37];
} adv_data_t;

int8_t rssi_filter_buff[MAX_RSSI_BUFF_SIZE];
int rssi_filter_counter = 0;

int8_t calcMode (int8_t *rssi, int len);

/**@brief Function to handle asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num     Line number of the failing ASSERT call.
 * @param[in] p_file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(0xDEADBEEF, line_num, p_file_name);
}

/**@brief Function for the LEDs initialization.
 *
 * @details Initializes all LEDs used by the application.
 */
static void leds_init(void)
{
    bsp_board_init(BSP_INIT_LEDS);
}

/**@brief Function to start scanning.
 */
static void scan_start(void) {
    ret_code_t err_code;
    err_code = nrf_ble_scan_start(&m_scan);
    APP_ERROR_CHECK(err_code);
}

/**@brief Handles events coming from the LED Button central module.
 */
static void lbs_c_evt_handler(ble_lbs_c_t * p_lbs_c, ble_lbs_c_evt_t * p_lbs_c_evt)
{
    switch (p_lbs_c_evt->evt_type)
    {
        case BLE_LBS_C_EVT_DISCOVERY_COMPLETE:
        {
            ret_code_t err_code;

            err_code = ble_lbs_c_handles_assign(&m_ble_lbs_c, p_lbs_c_evt->conn_handle, &p_lbs_c_evt->params.peer_db);
            NRF_LOG_RAW_INFO("LED Button service discovered on conn_handle 0x%x.", p_lbs_c_evt->conn_handle);

            err_code = app_button_enable();
            APP_ERROR_CHECK(err_code);

            // LED Button service discovered. Enable notification of Button.
            err_code = ble_lbs_c_button_notif_enable(p_lbs_c);

            nrf_drv_timer_enable(&TIMER_LED);

           APP_ERROR_CHECK(err_code);
        } break; // BLE_LBS_C_EVT_DISCOVERY_COMPLETE

        case BLE_LBS_C_EVT_BUTTON_NOTIFICATION:
            NRF_LOG_RAW_INFO("BLE_LBS_C_EVT_BUTTON_NOTIFICATION\n");
        break;

        default:
            break;
    }
}

/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
    ret_code_t err_code;

    // For readability.
    ble_gap_evt_t const * p_gap_evt = &p_ble_evt->evt.gap_evt;

    switch (p_ble_evt->header.evt_id)
    {
        // Upon connection, check which peripheral has connected (HR or RSC), initiate DB
        // discovery, update LEDs status and resume scanning if necessary. */
        case BLE_GAP_EVT_CONNECTED:
        {
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_CONNECTED\n");
            NRF_LOG_RAW_INFO("handle = 0x%X\n", p_gap_evt->conn_handle);
            err_code = ble_lbs_c_handles_assign(&m_ble_lbs_c, p_gap_evt->conn_handle, NULL);
            APP_ERROR_CHECK(err_code);

            err_code = ble_db_discovery_start(&m_db_disc, p_gap_evt->conn_handle);
            APP_ERROR_CHECK(err_code);
            //start receive rssi during connection
            err_code = sd_ble_gap_rssi_start(p_gap_evt->conn_handle, 5, 1);
            //reset filter counter
            rssi_filter_counter = 0;
            //delete previous rssi
            rssi_filter_counter = 0;
            APP_ERROR_CHECK(err_code);
        } 
        break;

        // Upon disconnection, reset the connection handle of the peer which disconnected, update
        // the LEDs status and start scanning again.
        case BLE_GAP_EVT_DISCONNECTED:
        {
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_DISCONNECTED\n");
            bsp_board_led_off(BSP_BOARD_LED_0);
            nrf_drv_timer_disable(&TIMER_LED);
            scan_start();
        } break;

        case BLE_GAP_EVT_TIMEOUT:
        {
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_TIMEOUT\n");
            // We have not specified a timeout for scanning, so only connection attemps can timeout.
            if (p_gap_evt->params.timeout.src == BLE_GAP_TIMEOUT_SRC_CONN) {
                NRF_LOG_RAW_INFO("BLE_GAP_EVT_TIMEOUT\n");
            }
        } 
        break;

        case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
        {
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST\n");
            // Accept parameters requested by peer.
            err_code = sd_ble_gap_conn_param_update(p_gap_evt->conn_handle, &p_gap_evt->params.conn_param_update_request.conn_params);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
        {
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_PHY_UPDATE_REQUEST\n");
            ble_gap_phys_t const phys = {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_GATTC_EVT_TIMEOUT:
        {
            // Disconnect on GATT Client timeout event.
            NRF_LOG_RAW_INFO("BLE_GATTC_EVT_TIMEOUT\n");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_GATTS_EVT_TIMEOUT:
        {
            // Disconnect on GATT Server timeout event.
            NRF_LOG_RAW_INFO("BLE_GATTS_EVT_TIMEOUT\n");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_GAP_EVT_CONN_PARAM_UPDATE:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_CONN_PARAM_UPDATE\n");
        break;

        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_SEC_PARAMS_REQUEST\n");
        break;

        case BLE_GAP_EVT_SEC_INFO_REQUEST:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_SEC_INFO_REQUEST\n");
        break;

        case BLE_GAP_EVT_PASSKEY_DISPLAY:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_PASSKEY_DISPLAY\n");
        break;

        case BLE_GAP_EVT_KEY_PRESSED:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_KEY_PRESSED\n");
        break;

        case BLE_GAP_EVT_AUTH_KEY_REQUEST:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_AUTH_KEY_REQUEST\n");
        break;

        case BLE_GAP_EVT_LESC_DHKEY_REQUEST:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_LESC_DHKEY_REQUEST\n");
        break;

        case BLE_GAP_EVT_AUTH_STATUS :
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_AUTH_STATUS\n");
        break;

        case BLE_GAP_EVT_CONN_SEC_UPDATE:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_CONN_SEC_UPDATE\n");
        break;

        case BLE_GAP_EVT_RSSI_CHANGED:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_RSSI_CHANGED\n");
            int8_t connectionRSSI;
            uint8_t channelrssi_filter_counter;
            uint32_t err_code = sd_ble_gap_rssi_get(p_ble_evt->evt.gatts_evt.conn_handle, &connectionRSSI, &channelrssi_filter_counter);
            APP_ERROR_CHECK(err_code);
            rssi_filter_buff[rssi_filter_counter++] = connectionRSSI;
            if (rssi_filter_counter < MAX_RSSI_BUFF_SIZE)
                return;
            int mode = calcMode(rssi_filter_buff, rssi_filter_counter);
            rssi_filter_counter = 0;
            NRF_LOG_RAW_INFO("connectionRSSI = %i\n", mode);
            if (mode <= RSSI_THRESHOLD) {
                NRF_LOG_RAW_INFO("Disconnecting from slave, too far away\n");
                err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            }
        break;

        case BLE_GAP_EVT_ADV_REPORT: {
            //advertising report. Get remote rssi value
            const ble_gap_evt_adv_report_t *p_adv_report = &p_gap_evt->params.adv_report;
            uint8_t *data = p_adv_report->data.p_data;
            uint16_t len = p_adv_report->data.len;

            if (p_adv_report->type.status != BLE_GAP_ADV_DATA_STATUS_INCOMPLETE_MORE_DATA) {
                adv_payload_t *adv = (adv_payload_t *) data;
                adv_data_t *ad = (adv_data_t *) adv->payload;
                if (p_adv_report->type.scan_response == 0) {
                    if (ad->adv_type == 0x09) { 
                        //complete local name
                        int ret = memcmp(ad->data, m_target_periph_name, ad->adv_len - 1);
                        if (ret == 0) {
                            //device name founded
                            ble_gap_scan_params_t scan_params;
                            ble_gap_conn_params_t conn_params;

                            memset(&scan_params, 0, sizeof(ble_gap_scan_params_t));
                            scan_params.interval = SCAN_INTERVAL;
                            scan_params.window = SCAN_WINDOW;
                            scan_params.timeout = SCAN_DURATION;

                            memset(&conn_params, 0, sizeof(ble_gap_conn_params_t));
                            conn_params.min_conn_interval = MIN_CONNECTION_INTERVAL;
                            conn_params.max_conn_interval = MAX_CONNECTION_INTERVAL;
                            conn_params.slave_latency = SLAVE_LATENCY;
                            conn_params.conn_sup_timeout = SUPERVISION_TIMEOUT;
                            
                            rssi_filter_buff[rssi_filter_counter++] = p_adv_report->rssi;
                            if (rssi_filter_counter < MAX_RSSI_BUFF_SIZE)
                                return;
                            int8_t mode = calcMode(rssi_filter_buff, MAX_RSSI_BUFF_SIZE);
                            rssi_filter_counter = 0;
                            NRF_LOG_RAW_INFO("rssi mode = %i\n", mode);
                            if (p_adv_report->rssi <= RSSI_THRESHOLD)
                                return;

                            uint32_t ret = sd_ble_gap_connect((ble_gap_addr_t const *)&p_adv_report->peer_addr, 
                                (ble_gap_scan_params_t const *)&scan_params, 
                                (ble_gap_conn_params_t const *)&conn_params, APP_BLE_CONN_CFG_TAG);
                        }
                    }
                }
                else if (p_adv_report->type.scan_response == 1) {
                    //check uuid on scan response
                    ad = (adv_data_t *) data;
                    if (ad->adv_type == 0x07) {
                        //complete list of 128-bit uuid
                        //check uuid
                    }
                }
            }
            //NRF_LOG_RAW_INFO("BLE_GAP_EVT_ADV_REPORT\n");
        }
        break;

        case BLE_GAP_EVT_SEC_REQUEST:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_SEC_REQUEST\n");
        break;
        
        case BLE_GAP_EVT_SCAN_REQ_REPORT:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_SCAN_REQ_REPORT\n");
        break;

        case BLE_GAP_EVT_DATA_LENGTH_UPDATE_REQUEST:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_DATA_LENGTH_UPDATE_REQUEST\n");
        break;

        case BLE_GAP_EVT_DATA_LENGTH_UPDATE:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_DATA_LENGTH_UPDATE\n");
        break;

        case BLE_GAP_EVT_QOS_CHANNEL_SURVEY_REPORT:
            NRF_LOG_RAW_INFO("BLE_GAP_EVT_QOS_CHANNEL_SURVEY_REPORT\n");
        break;

        default:
            break;
    }
}

/**@brief LED Button client initialization.
 */
static void lbs_c_init(void)
{
    ble_lbs_c_init_t lbs_c_init_obj;

    lbs_c_init_obj.evt_handler = lbs_c_evt_handler;

    ret_code_t err_code = ble_lbs_c_init(&m_ble_lbs_c, &lbs_c_init_obj);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupts.
 */
static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

/**@brief Function for handling events from the button handler module.
 *
 * @param[in] pin_no        The pin that the event applies to.
 * @param[in] button_action The button action (press/release).
 */
static void button_event_handler(uint8_t pin_no, uint8_t button_action)
{
    ret_code_t err_code;

    switch (pin_no)
    {
        case LEDBUTTON_BUTTON_PIN:
            NRF_LOG_RAW_INFO("LEDBUTTON_BUTTON_PIN\n");
            /*
            err_code = ble_lbs_led_status_send(&m_ble_lbs_c, button_action);
            if (err_code != NRF_SUCCESS &&
                err_code != BLE_ERROR_INVALID_CONN_HANDLE &&
                err_code != NRF_ERROR_INVALID_STATE)
            {
                APP_ERROR_CHECK(err_code);
            }
            if (err_code == NRF_SUCCESS)
            {
                //NRF_LOG_RAW_INFO("LBS write LED state %d", button_action);
            }
            */
            break;

        default:
            APP_ERROR_HANDLER(pin_no);
            break;
    }
}

/**@brief Function for handling Scaning events.
 *
 * @param[in]   p_scan_evt   Scanning event.
 */
static void scan_evt_handler(scan_evt_t const * p_scan_evt)
{
    ret_code_t err_code;

    switch(p_scan_evt->scan_evt_id)
    {
        case NRF_BLE_SCAN_EVT_CONNECTING_ERROR:
            NRF_LOG_RAW_INFO("NRF_BLES_SCAN_EVT_CONNECTING_ERROR\n");
            err_code = p_scan_evt->params.connecting_err.err_code;
            APP_ERROR_CHECK(err_code);
            break;
        case NRF_BLE_SCAN_EVT_FILTER_MATCH:
            //NRF_LOG_RAW_INFO("NRF_BLE_SCAN_EVT_FILTER_MATCH\n");
        break;

        case NRF_BLE_SCAN_EVT_WHITELIST_REQUEST:
            NRF_LOG_RAW_INFO("NRF_BLE_SCAN_EVT_WHITELIST_REQUEST\n");
        break;

        case NRF_BLE_SCAN_EVT_WHITELIST_ADV_REPORT:
            NRF_LOG_RAW_INFO("NRF_BLE_SCAN_EVT_WHITELIST_ADV_REPORT\n");
        break;

        case NRF_BLE_SCAN_EVT_NOT_FOUND:
            //NRF_LOG_RAW_INFO("NRF_BLE_SCAN_EVT_NOT_FOUND\n");
        break;

        case NRF_BLE_SCAN_EVT_SCAN_TIMEOUT:
            NRF_LOG_RAW_INFO("NRF_BLE_SCAN_EVT_SCAN_TIMEOUT\n");
        break;

        case NRF_BLE_SCAN_EVT_SCAN_REQ_REPORT:
            NRF_LOG_RAW_INFO("NRF_BLE_SCAN_EVT_SCAN_REQ_REPORT\n");
        break;

        case NRF_BLE_SCAN_EVT_CONNECTED:
            NRF_LOG_RAW_INFO("NRF_BLE_SCAN_EVT_CONNECTED\n");
        break;

        default:
            NRF_LOG_RAW_INFO("NRF_BLE_SCAN_EVT_DEFAULT\n");
        break;
    }
}

/**@brief Function for initializing the button handler module.
 */
static void buttons_init(void)
{
    ret_code_t err_code;

    //The array must be static because a pointer to it will be saved in the button handler module.
    static app_button_cfg_t buttons[] = {
        {LEDBUTTON_BUTTON_PIN, false, BUTTON_PULL, button_event_handler}
    };

    err_code = app_button_init(buttons, ARRAY_SIZE(buttons), BUTTON_DETECTION_DELAY);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling database discovery events.
 *
 * @details This function is callback function to handle events from the database discovery module.
 *          Depending on the UUIDs that are discovered, this function should forward the events
 *          to their respective services.
 *
 * @param[in] p_event  Pointer to the database discovery event.
 */
static void db_disc_handler(ble_db_discovery_evt_t * p_evt)
{
    ble_lbs_on_db_disc_evt(&m_ble_lbs_c, p_evt);
}

/**@brief Database discovery initialization.
 */
static void db_discovery_init(void)
{
    ret_code_t err_code = ble_db_discovery_init(db_disc_handler);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the log.
 */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/**@brief Function for initializing the timer.
 */
static void timer_init(void)
{
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the Power manager. */
static void power_management_init(void)
{
    ret_code_t err_code;
    err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}

static void scan_init(void)
{
    ret_code_t          err_code;
    nrf_ble_scan_init_t init_scan;

    memset(&init_scan, 0, sizeof(init_scan));

    init_scan.connect_if_match = false;
    init_scan.conn_cfg_tag     = APP_BLE_CONN_CFG_TAG;

    err_code = nrf_ble_scan_init(&m_scan, &init_scan, scan_evt_handler);
    APP_ERROR_CHECK(err_code);

    // Setting filters for scanning.
    err_code = nrf_ble_scan_filters_enable(&m_scan, NRF_BLE_SCAN_NAME_FILTER, false);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_scan_filter_set(&m_scan, SCAN_NAME_FILTER, m_target_periph_name);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the GATT module.
 */
static void gatt_init(void)
{
    ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
    APP_ERROR_CHECK(err_code);
}

void timer_led_event_handler(nrf_timer_event_t event_type, void* p_context)
{
    switch (event_type) {
        case NRF_TIMER_EVENT_COMPARE0:
            bsp_board_led_invert(BSP_BOARD_LED_0);
            ledStatus = ~ledStatus;
            ble_lbs_led_status_send(&m_ble_lbs_c, ledStatus);
        break;

        default:
            //Do nothing.
            break;
    }
}

void config_led_timer (void) {
    // Turn on the LED to signal scanning.
    //bsp_board_led_on(CENTRAL_SCANNING_LED);
    uint32_t time_ticks;
    uint32_t err_code = NRF_SUCCESS;

    //Configure all leds on board.
    bsp_board_init(BSP_INIT_LEDS);

    //Configure TIMER_LED for generating simple light effect - leds on board will invert his state one after the other.
    nrf_drv_timer_config_t timer_cfg = NRF_DRV_TIMER_DEFAULT_CONFIG;
    err_code = nrf_drv_timer_init(&TIMER_LED, &timer_cfg, timer_led_event_handler);

    APP_ERROR_CHECK(err_code);
    time_ticks = nrf_drv_timer_ms_to_ticks(&TIMER_LED, BLINK_TIME_INTERVAL_MS);
    nrf_drv_timer_extended_compare(&TIMER_LED, NRF_TIMER_CC_CHANNEL0, time_ticks, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);
}

int8_t calcMode (int8_t *rssi, int len) {
  int8_t maxValue = 0, maxCount = 0, i, j;
   for (i = 0; i < len; ++i) {
      int8_t count = 0;
      
      for (j = 0; j < len; ++j) {
         if (rssi[j] == rssi[i])
         ++count;
      }
      
      if (count > maxCount) {
         maxCount = count;
         maxValue = rssi[i];
      }
   }
   return maxValue;
}

int main(void)
{
    // Initialize.
    log_init();
    timer_init();
    leds_init();
    buttons_init();
    power_management_init();
    ble_stack_init();
    scan_init();
    gatt_init();
    db_discovery_init();
    lbs_c_init();
    config_led_timer();
    
    // Start execution.
    NRF_LOG_RAW_INFO("Blinky CENTRAL example started.\n");
    scan_start();

    bsp_board_led_off(BSP_BOARD_LED_0);

    while (1) {
        nrf_pwr_mgmt_run();
    }
}
