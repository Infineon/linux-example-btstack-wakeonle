/*
* Copyright 2023, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*/

/******************************************************************************
 * File Name: wakeon_le.c
 *
 * Description: This is the source file for Linux wakeon_le CE.
 *
 * Related Document: See README.md
 * 
 ******************************************************************************
* $ Copyright 2022-YEAR Cypress Semiconductor $
*******************************************************************************
*      INCLUDES
*******************************************************************************/
#include "wiced_bt_stack.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "wiced_memory.h"
#include "stdio.h"
#include "wiced_bt_dev.h"
#include "app_bt_utils.h"
#include "wakeon_le.h"
#include "wiced_hal_nvram.h"
#include "data_types.h"
#include "wiced_exp.h"
#include "platform_linux.h"
#include "linux/gpio.h"
#include "log.h"

#ifdef TAG
#undef TAG
#endif
#define TAG "[WAKEONLE]"

#define debug 0

/*******************************************************************************
*       MACROS
*******************************************************************************/
#define BT_STACK_HEAP_SIZE          (0xF000)

/*******************************************************************************
*       STRUCTURES AND ENUMERATIONS
*******************************************************************************/
wiced_bt_device_address_t bt_device_address = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

/*******************************************************************************
*       VARIABLE DEFINITIONS
*******************************************************************************/
wiced_bt_heap_t *p_default_heap   = NULL;
extern cybt_controller_gpio_config_t gpio_cfg;
BOOL32 inSleep = WICED_FALSE;
tBT_UUID uuid = {0};
uint16_t company_id = COMPANY_ID;
uint16_t company_id_mask = 0xFFFF;
uint32_t data_len = 0;
uint8_t pattern[LE_PCF_MANUFACTURE_DATA_PATTERN_LEN_MAX] = {0};
uint8_t pattern_mask[LE_PCF_MANUFACTURE_DATA_PATTERN_LEN_MAX] = {0};
uint8_t apcf_filter_idx =  WICED_LE_ADV_PCF_FILTER_INDEX_START;

/*******************************************************************************
*       FUNCTION DECLARATIONS
*******************************************************************************/
static void  app_init(void);
static void* app_alloc_buffer(int len);
static void  app_free_buffer(uint8_t *p_event_data);
static void  app_scan_result_cback(wiced_bt_ble_scan_results_t* p_scan_result, uint8_t* p_adv_data);

/* Callback function for Bluetooth stack management type events */
static wiced_bt_dev_status_t    app_bt_management_callback(wiced_bt_management_evt_t event, wiced_bt_management_evt_data_t *p_event_data);
static void bt_host_wake_assert_cback();
static void bt_sleep_cmpl_cback(tBTM_VSC_CMPL *p_params); 

/*******************************************************************************
*       FUNCTION DEFINITION
*******************************************************************************/
/*******************************************************************************
* Function Name: application_start
********************************************************************************
* Summary:
*   Set device configuration and start BT stack initialization. The actual
*   application initialization will happen when stack reports that BT device
*   is ready.
*
* Parameters: NONE
*
* Return: NONE
*
*******************************************************************************/
void application_start(void)
{
    wiced_result_t wiced_result;

    TRACE_LOG("************* WakeOn_LE Application Start ************************\n");
    wiced_exp_version();
    /* Register call back and configuration with stack */
    wiced_result = wiced_bt_stack_init (app_bt_management_callback, &wiced_bt_cfg_settings);

    /* Check if stack initialization was successful */
    if( WICED_BT_SUCCESS == wiced_result)
    {
        TRACE_LOG("Bluetooth Stack Initialization Successful \n");
        /* Create default heap */
        p_default_heap = wiced_bt_create_heap("default_heap", NULL, BT_STACK_HEAP_SIZE, NULL, WICED_TRUE);
        if (p_default_heap == NULL)
        {
            TRACE_ERR("create default heap error: size %d\n", BT_STACK_HEAP_SIZE);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
       TRACE_ERR("Bluetooth Stack Initialization Failed!! \n");
       exit(EXIT_FAILURE);
    }
}

/*******************************************************************************
* Function Name: app_bt_management_callback
********************************************************************************
* Summary:
*   This is a Bluetooth stack event handler function to receive management
*   events from the LE stack and process as per the application.
*
* Parameters:
*   wiced_bt_management_evt_t event             : LE event code of one byte
*                                                 length
*   wiced_bt_management_evt_data_t *p_event_data: Pointer to LE management
*                                                 event structures
*
* Return:
*  wiced_result_t: Error code from WICED_RESULT_LIST or BT_RESULT_LIST
*
*******************************************************************************/
static wiced_result_t app_bt_management_callback(wiced_bt_management_evt_t event,
                                          wiced_bt_management_evt_data_t *p_event_data)
{
    wiced_result_t result = WICED_BT_SUCCESS;
    wiced_bt_device_address_t bda = { 0 };
    const uint8_t *link_key;

    TRACE_LOG( "event 0x%x \n", event );
    switch (event)
    {
    case BTM_ENABLED_EVT:
        /* Bluetooth Controller and Host Stack Enabled */
        if (WICED_BT_SUCCESS == p_event_data->enabled.status)
        {
            wiced_bt_set_local_bdaddr((uint8_t *)bt_device_address, BLE_ADDR_PUBLIC);
            /* Bluetooth is enabled */               
            wiced_bt_dev_read_local_addr(bda);
            TRACE_LOG("Local Bluetooth Address:");
            print_bd_address(bda);

            /* Perform application-specific initialization */
            app_init();

        }
        else
        {
            TRACE_ERR( "Bluetooth Enabling Failed \n");
            result = WICED_BT_ERROR;
        }
        break;

    case BTM_DISABLED_EVT:
        TRACE_LOG( "Bluetooth Disabled \n" );
        break;

    default:
        TRACE_LOG("Unhandled Bluetooth Management Event: 0x%x %s\n", event, get_bt_event_name(event));
        break;
    }

    return result;
}

/*******************************************************************************
* Function Name: app_init
********************************************************************************
* Summary:
*   This function handles application level initialization tasks and is called
*   from the BT management callback once the LE stack enabled event
*   (BTM_ENABLED_EVT) is triggered This function is executed in the
*   BTM_ENABLED_EVT management callback.
*
* Parameters:
*   None
*
* Return:
*   None
*
*******************************************************************************/
static void app_init(void)
{
    wiced_result_t wiced_result = WICED_BT_SUCCESS;
    if(platform_gpio_write(gpio_cfg.wake_on_ble_cfg.dev_wake.p_gpiochip, gpio_cfg.wake_on_ble_cfg.dev_wake.line_num, GPIO_ASSERT(WICED_SLEEP_MODE_BT_WAKE_ACT_LOW), "DEV-WAKE") == WICED_FALSE)
    {
        TRACE_ERR("DEV-WAKE ASSERT Failed\n");
    }
}

/*******************************************************************************
* Function Name: app_set_sleep_mode
********************************************************************************
* Summary: dev-wake assert fisrt, then call set_sleep_mode
* 
* Parameters:
*   None
*
* Return:
*   BOOL32:
*         WICED_TRUE:  SUCCESS 
*         WICED_FALSE: ERROR HAPPENED
*
*******************************************************************************/
static BOOL32 app_set_sleep_mode(void)
{
    TRACE_LOG("DEV-WAKE ASSERT FIRST\n");
    /* dev wake assert first */
    if(platform_gpio_write(gpio_cfg.wake_on_ble_cfg.dev_wake.p_gpiochip, gpio_cfg.wake_on_ble_cfg.dev_wake.line_num, GPIO_ASSERT(WICED_SLEEP_MODE_BT_WAKE_ACT_LOW), "DEV-WAKE") == WICED_FALSE)
    {
        TRACE_ERR("DEV-WAKE ASSERT Failed\n");
        return WICED_FALSE;
    }
    /* set sleep mode with param */
    if(wiced_set_sleep_mode_with_param(BTM_SLEEP_MODE_UART, WICED_SLEEP_MODE_BT_WAKE_ACT_LOW, WICED_SLEEP_MODE_HOST_WAKE_ACT_LOW, WICED_TRUE, bt_sleep_cmpl_cback) == WICED_FALSE)
    {
        TRACE_ERR("set sleep mode with param Failed");
        return WICED_FALSE;
    }
    return WICED_TRUE;
}

/*******************************************************************************
* Function Name: app_clear_apcf_setting
********************************************************************************
* Summary: 
*   This Function clear all apcf filter param setting
* 
* Parameters:
*   None
*
* Return:
*   BOOL32:
*         WICED_TRUE:  SUCCESS 
*         WICED_FALSE: ERROR HAPPENED
*
*******************************************************************************/
static BOOL32 app_clear_apcf_setting(void)
{
    TRACE_LOG("\n");

    /* disable apcf first */
    if (wiced_set_apcf_enable(WICED_FALSE) == WICED_FALSE)
    {
        TRACE_ERR("set apcf disable Failed\n");
        return WICED_FALSE;
    }
    
    /* clear apcf filter setting */
    if (wiced_set_apcf_filter_param(WICED_LE_ADV_PCF_ACT_CLEAR, apcf_filter_idx, WICED_LE_ADV_PCF_FEA_NONE,
                          WICED_LE_ADV_PCF_FEA_NONE, WICED_LE_ADV_PCF_LOGIC_AND, WICED_LE_ADV_PCF_RSSI_HIGH_THRESHOLD, WICED_LE_ADV_PCF_DELIVERY_MODE_IMMEDIATE) == WICED_FALSE)
    {
        TRACE_ERR("set_apcf_filter_param Failed\n");
        return WICED_FALSE;
    }

    TRACE_LOG("success\n");
    return WICED_TRUE;
}

/*******************************************************************************
* Function Name: app_set_apcf_setting
********************************************************************************
* Summary: 
*   This Function set apcf data, apcf filter param and enable apcf
* 
* Parameters:
*   None
*
* Return:
*   BOOL32:
*         WICED_TRUE:  SUCCESS 
*         WICED_FALSE: ERROR HAPPENED
*
*******************************************************************************/
BOOL32 app_set_apcf_setting()
{
    TRACE_LOG("\n");
    /* set apcf data uuid */
    if (wiced_set_apcf_data_uuid(uuid, WICED_LE_ADV_PCF_ACT_ADD, apcf_filter_idx) == WICED_FALSE)
    {
        TRACE_ERR("set_apcf_data Failed\n");
        return WICED_FALSE;
    }

    /* set apcf filter param */
    if (wiced_set_apcf_filter_param(WICED_LE_ADV_PCF_ACT_ADD, apcf_filter_idx, WICED_LE_ADV_PCF_FEA_SRVC_UUID,
                          WICED_LE_ADV_PCF_FEA_SRVC_UUID, WICED_LE_ADV_PCF_LOGIC_AND, WICED_LE_ADV_PCF_RSSI_HIGH_THRESHOLD, WICED_LE_ADV_PCF_DELIVERY_MODE_IMMEDIATE) == WICED_FALSE)
    {
        TRACE_ERR("set_apcf_filter_param Failed\n");
        return WICED_FALSE;
    }

    /* enable apcf */
    if(wiced_set_apcf_enable(WICED_TRUE) == WICED_FALSE)
    {
        TRACE_ERR("set apcf enable Failed\n");
        return WICED_FALSE;
    }

    TRACE_LOG("success\n");
    return WICED_TRUE;
}

/*******************************************************************************
* Function Name: app_disable_wake_on_ble
********************************************************************************
* Summary:
*   This Function disable wake on ble and stop le scan
* 
* Parameters:
*   None
*
* Return:
*   None
*
*******************************************************************************/
void app_disable_wake_on_le()
{
    TRACE_LOG("\n");
    if (inSleep == WICED_FALSE)
    {
        TRACE_LOG("[%s]:Not in Sleep.\n", __FUNCTION__);
        return;
    }

    /* assert gpio DEV-WAKE */
    if (platform_gpio_write(gpio_cfg.wake_on_ble_cfg.dev_wake.p_gpiochip, gpio_cfg.wake_on_ble_cfg.dev_wake.line_num, GPIO_ASSERT(WICED_SLEEP_MODE_BT_WAKE_ACT_LOW), "DEV-WAKE") == WICED_FALSE)
    {
        TRACE_ERR("Assert DEV WAKE Failed\n");
        return;
    }
    TRACE_LOG("Disable Le Scan\n");
    /* disable le scan */
    /* wiced bt stack api */
    if (wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_NONE, WICED_TRUE, app_scan_result_cback) != 0)
    {
        TRACE_ERR("Disable Le Scan Failed\n");
        return;
    }
    TRACE_LOG("success\n");
}

/*******************************************************************************
* Function Name: app_enable_wake_on_ble_with_manu
********************************************************************************
* Summary:
*   Enalbe Wake On LE with uuid AND Manufacture Data
* 
* Parameters:
*   None
*
* Return:
*   None
*
*******************************************************************************/
void app_enable_wake_on_le_uuid_manu()
{
    TRACE_LOG("\n");
    wiced_result_t status = WICED_BT_SUCCESS;
    
    /* clear apcf setting first */
    if(app_clear_apcf_setting() == WICED_FALSE)
    {
        TRACE_ERR("app_clear_apcf_setting Failed\n");
        return;
    }

    /* set apcf data uuid */
    if (wiced_set_apcf_data_uuid(uuid, WICED_LE_ADV_PCF_ACT_ADD, apcf_filter_idx) == WICED_FALSE)
    {
        TRACE_ERR("set_apcf_data Failed\n");
        return;
    }
    memset(pattern_mask, 0xFF, sizeof(pattern_mask));
    /* set apcf data manufacture */
    if (wiced_set_apcf_data_manufacture(company_id, data_len, pattern, company_id_mask, pattern_mask, WICED_LE_ADV_PCF_ACT_ADD, apcf_filter_idx) == WICED_FALSE)
    {
        TRACE_ERR("set_apcf_data Failed\n");
        return;
    }

    /* set apcf filter param */
    if (wiced_set_apcf_filter_param(WICED_LE_ADV_PCF_ACT_ADD, apcf_filter_idx, WICED_LE_ADV_PCF_FEA_SRVC_UUID | WICED_LE_ADV_PCF_FEA_MANU_DATA,
                          WICED_LE_ADV_PCF_FEA_SRVC_UUID | WICED_LE_ADV_PCF_FEA_MANU_DATA, WICED_LE_ADV_PCF_LOGIC_AND, WICED_LE_ADV_PCF_RSSI_HIGH_THRESHOLD, WICED_LE_ADV_PCF_DELIVERY_MODE_IMMEDIATE) == WICED_FALSE)
    {
        TRACE_ERR("set_apcf_filter_param Failed\n");
        return;
    }

    /* enable apcf */
    if(wiced_set_apcf_enable(WICED_TRUE) == WICED_FALSE)
    {
        TRACE_ERR("set apcf enable Failed\n");
        return;
    }

    /* enable ble scan */
    /* wiced bt stack api */
    status = wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_LOW_DUTY, WICED_TRUE, app_scan_result_cback);
    if ((WICED_BT_PENDING != status ) && ( WICED_BT_BUSY != status))
    {
        TRACE_ERR("enable ble scan Failed\n");
        return;
    }

    /* set sleep mode */
    if (app_set_sleep_mode() == WICED_FALSE)
    {
        TRACE_ERR("set sleep mode Failed\n");
        return;
    }
    TRACE_LOG("success\n");
}

/*******************************************************************************
* Function Name: app_enable_wake_on_ble_uuid
********************************************************************************
* Summary:
*   Enalbe Wake On LE with uuid
* 
* Parameters:
*   None
*
* Return:
*   None
*
*******************************************************************************/
void app_enable_wake_on_le_uuid()
{
    TRACE_LOG("\n");
    wiced_result_t status = WICED_BT_SUCCESS;

    /* clear apcf first */
    if(app_clear_apcf_setting() == WICED_FALSE)
    {
        TRACE_ERR("app_clear_apcf_setting Failed\n");
        return;
    }

    /* set apcf data uuid */
    if (wiced_set_apcf_data_uuid(uuid, WICED_LE_ADV_PCF_ACT_ADD, apcf_filter_idx) == WICED_FALSE)
    {
        TRACE_ERR("set_apcf_data Failed\n");
        return;
    }

    /* set apcf filter param */
    if (wiced_set_apcf_filter_param(WICED_LE_ADV_PCF_ACT_ADD, apcf_filter_idx, WICED_LE_ADV_PCF_FEA_SRVC_UUID,
                          WICED_LE_ADV_PCF_FEA_SRVC_UUID, WICED_LE_ADV_PCF_LOGIC_AND, WICED_LE_ADV_PCF_RSSI_HIGH_THRESHOLD, WICED_LE_ADV_PCF_DELIVERY_MODE_IMMEDIATE) == WICED_FALSE)
    {
        TRACE_ERR("set_apcf_filter_param Failed\n");
        return;
    }

    /* enable apcf */
    if (wiced_set_apcf_enable(WICED_TRUE) == WICED_FALSE)
    {
        TRACE_ERR("set apcf enable Failed\n");
        return;
    }

    /* enable ble scan */
    /* wiced bt stack api */
    status = wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_LOW_DUTY, WICED_TRUE, app_scan_result_cback);
    if ((WICED_BT_PENDING != status ) && ( WICED_BT_BUSY != status))
    {
        TRACE_ERR("enable ble scan Failed, status:%d\n", status);
        return;
    }

    /* set sleep mode */
    if (app_set_sleep_mode() == WICED_FALSE)
    {
        TRACE_ERR("set sleep mode Failed\n");
        return;
    }
    TRACE_LOG("success\n");
}

/*******************************************************************************
* Function Name: app_scan_result_cback
********************************************************************************
* Summary:
*   This callback function handles the le scan results
*   if in WakeOnLE mode, will not trigeer it
*   if want to test APCF Function.
*   can use this function and enable APCF with UUID to see the ADV
*
* Parameters:
*   wiced_bt_ble_scan_results_t* p_scan_result:
*            scan result from stack
*   uint8_t* p_adv_data:
*            scan data 
* Return:
*   None
*
*******************************************************************************/
static void app_scan_result_cback(wiced_bt_ble_scan_results_t* p_scan_result, uint8_t* p_adv_data)
{
    if (p_scan_result)
    {
        TRACE_LOG("Got ADV from: %s\n", p_scan_result->remote_bd_addr);
	print_bd_address(p_scan_result->remote_bd_addr);
    } else {
        TRACE_LOG("Scan completed:\n");
    }
}
 
/*******************************************************************************
* Function Name: bt_sleep_cmpl_cback
********************************************************************************
* Summary:
*   Callback function for set sleep mode, when set sleep mode complete,    
*   need use dev-wake let Controll enter sleep mode.
*   and start a thread to monitor Host-Wake
*
* Parameters:
*   tBTM_VSC_CMPL *p_params:
*             param of VSC command data 
*
* Return:
*   None
*
*******************************************************************************/
static void bt_sleep_cmpl_cback(tBTM_VSC_CMPL *p_params) 
{
    if (p_params == NULL)
    {
        TRACE_ERR("p_params is NULL\n");
        return;
    }
    TRACE_LOG("opcode: %x, param_len:%d\n", p_params->opcode, p_params->param_len);
    uint8_t  status = 0;
    uint8_t  *p = p_params->p_param_buf, op_subcode, action = 0xff;
    STREAM_TO_UINT8(status, p);
    
    if (status == HCI_SUCCESS) 
    {
	TRACE_LOG("set sleep mode success \n");
 	TRACE_LOG("Ready Enter UART Sleep Mode\n");
        if (platform_gpio_write(gpio_cfg.wake_on_ble_cfg.dev_wake.p_gpiochip, gpio_cfg.wake_on_ble_cfg.dev_wake.line_num, GPIO_DEASSERT(WICED_SLEEP_MODE_BT_WAKE_ACT_LOW), "DEV-WAKE") == WICED_FALSE)
        {
	    TRACE_ERR("Deassert DEV WAKE Failed\n");
            return;
        }
	gpio_cfg.wake_on_ble_cfg.host_wake_args.gpio_event_cb = &bt_host_wake_assert_cback;
        gpio_cfg.wake_on_ble_cfg.host_wake_args.gpio_event_flag = GPIOEVENT_REQUEST_FALLING_EDGE;
	if (platform_gpio_poll(&(gpio_cfg.wake_on_ble_cfg.host_wake_args)) == WICED_FALSE)
        {
            TRACE_ERR("Monitor host-wake Failed\n");
            return;
        }
    } else {
        TRACE_ERR("Set Sleep Mode Param Failed, status:%d\n", status);
        return;
    }
    
    inSleep = WICED_TRUE;
}

/*******************************************************************************
* Function Name: bt_host_wake_assert_cback
********************************************************************************
* Summary:
*   Callback function when host-wake assert, assert Dev-Wake, let Controller 
*   leave sleep mode, and stop le-scan
*
* Parameters:
*   None
*
* Return:
*   None
*
*******************************************************************************/
static void bt_host_wake_assert_cback()
{
    TRACE_LOG("HOST WAKE ASSERT\n");
    if (platform_gpio_write(gpio_cfg.wake_on_ble_cfg.dev_wake.p_gpiochip, gpio_cfg.wake_on_ble_cfg.dev_wake.line_num, GPIO_ASSERT(WICED_SLEEP_MODE_BT_WAKE_ACT_LOW), "DEV-WAKE") == WICED_FALSE)
    {
	TRACE_ERR("assert DEV WAKE Failed\n");
        return;
    }
    TRACE_LOG("Disable Le scan\n");
    /* disable le scan */
    /* wiced bt stack api */
    if (wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_NONE, WICED_TRUE, app_scan_result_cback) != 0)
    {
        TRACE_ERR("disable ble scan Failed\n");
        return;
    }

    /* disable apcf first */
    if (wiced_set_apcf_enable(WICED_FALSE) == WICED_FALSE)
    {
        TRACE_ERR("set apcf disable Failed\n");
        return;
    }

    /* clear apcf filter setting */
    if (wiced_set_apcf_filter_param(WICED_LE_ADV_PCF_ACT_CLEAR, apcf_filter_idx, WICED_LE_ADV_PCF_FEA_NONE,
                          WICED_LE_ADV_PCF_FEA_NONE, WICED_LE_ADV_PCF_LOGIC_AND, WICED_LE_ADV_PCF_RSSI_HIGH_THRESHOLD, WICED_LE_ADV_PCF_DELIVERY_MODE_IMMEDIATE) == WICED_FALSE)
    {
        TRACE_ERR("set_apcf_filter_param Failed\n");
        return;
    }

    /* disable sleep mode */
    if(wiced_set_sleep_mode_with_param(BTM_SLEEP_MODE_NONE, WICED_SLEEP_MODE_BT_WAKE_ACT_LOW, WICED_SLEEP_MODE_HOST_WAKE_ACT_LOW, WICED_FALSE, NULL) == WICED_FALSE)
    {
        TRACE_ERR("set sleep mode with param Failed");
        return;
    }

    inSleep = WICED_FALSE;
}

/* END OF FILE [] */
