/**
 * @file main.c
 * @author PhongEE
 * @brief MPPT Charger Controller
 * @version 1.0
 * @date 14-03-2023
 *
 * @copyright Copyright (c) 2023
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_defs.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_gap_ble_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_gatts_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatt_common_api.h"

#include "esp_attr.h"
#include "esp_adc_cal.h"

#include "driver/adc.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/rmt.h"

#include "ina226.h"
#include "mppt.h"


#define I2C_MASTER_SCL_IO               (22)
#define I2C_MASTER_SDA_IO               (21)
#define I2C_MASTER_NUM                  (0)
#define I2C_MASTER_FREQ_HZ              (100000)
#define I2C_MASTER_TX_BUF_DISABLE       (0)
#define I2C_MASTER_RX_BUF_DISABLE       (0)
#define I2C_MASTER_TIMEOUT_MS           (1000)

const char *i2c_tag = "I2C TASK";
const char *charger_tag = "CHARGER TASK";
const char *log_data_tag = "LOG DATA TASK";

struct INA226_Device INA226_01;
struct INA226_Device INA226_02;

TaskHandle_t i2c_task_handle;
TaskHandle_t charger_task_handle;
// TaskHandle_t log_data_task_handle;
QueueHandle_t adc_charger_queue_handle;
// QueueHandle_t adc_log_data_queue_handle;
EventGroupHandle_t event_group;

esp_err_t err;

//-------------------------------------------------------------------------------------------------------

static esp_err_t setup_i2c_master_init(void);
static esp_err_t setupGPIO(void);
static esp_err_t setupPWM(void);

size_t i2c_master_write_ina226(int Address, const uint8_t *Buffer, size_t BytesToWrite);
size_t i2c_master_read_ina226(int Address, uint8_t *Buffer, size_t BufferMaxLen);

void i2c_task(void *pvParameter);
void charger_task(void *pvParameter);

void switch_source(int state)
{
    gpio_set_level(RELAY, (uint32_t)state);
    gpio_set_level(MPPT_EN, (uint32_t)state);
}

//-------------------------------------------------------------------------------------------------------

void app_main(void)
{
    err = nvs_flash_init();
    /* Initialize NVS — it is used to store PHY calibration data */
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    //--------------------------------------------------------------------------------------------------------------------------

    ESP_ERROR_CHECK(setupGPIO());
    ESP_LOGI("GPIO", "GPIOs initialized successfully");

    //--------------------------------------------------------------------------------------------------------------------------

    ESP_ERROR_CHECK(setup_i2c_master_init());
    ESP_LOGI("I2C", "I2C initialized successfully");

    //--------------------------------------------------------------------------------------------------------------------------

    // ESP_ERROR_CHECK(setupPWM());
    // ESP_LOGI("PWM", "PWM initialized successfully");

    //--------------------------------------------------------------------------------------------------------------------------
    
    /* Create 2 task for system */
    xTaskCreate(i2c_task, "i2c_task", 2048, NULL, 5, &i2c_task_handle);
    xTaskCreate(charger_task, "charger_task", 2048, NULL, 7, &charger_task_handle);
}


void i2c_task(void *pvParameter)
{

    adc_struct_t adc_send;
    TickType_t pre_tick = 0;

    /* Initialize INA226 with specific I2C addresses,  value of RShunt and Max Current, I2C Read and Write Function */
    if (INA226_Init(&INA226_01, 0b1000000, 50, 3, &i2c_master_write_ina226, &i2c_master_read_ina226) == false)
    {
        ESP_LOGE(i2c_tag, "INA226-01 FAIL TO INIT!");
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
    if (INA226_Init(&INA226_02, 0b1000100, 50, 3, &i2c_master_write_ina226, &i2c_master_read_ina226) == false)
    {
        ESP_LOGE(i2c_tag, "INA226-02 FAIL TO INIT!");
    }

    ESP_LOGI(i2c_tag, "INA226 initialize done");

    INA226_SetAveragingMode(&INA226_01, INA226_Averages_64);
    INA226_SetAveragingMode(&INA226_02, INA226_Averages_64);

    INA226_SetOperatingMode(&INA226_01, INA226_Mode_ShuntAndBus_Continuous);
    INA226_SetOperatingMode(&INA226_02, INA226_Mode_ShuntAndBus_Continuous);

    while (1)
    {
        if (xTaskGetTickCount() - pre_tick > (500 / portTICK_PERIOD_MS))
        {
            pre_tick = xTaskGetTickCount();

            adc_send.v_solar = 0;
            adc_send.i_solar = 0;
            adc_send.v_bat = 0;
            adc_send.i_bat = 0;

            /* Get Bus Voltage and Current of Solar Panel and Battery */
            for (uint8_t i = 0; i < 10; i++)
            {
                adc_send.v_solar += (double)INA226_GetBusVoltage(&INA226_01) / 1000.0;
                adc_send.i_solar += (double)INA226_GetCurrent(&INA226_01) / 1000000.0;
                adc_send.v_bat += (double)INA226_GetBusVoltage(&INA226_02) / 1000.0;
                adc_send.i_bat += (double)INA226_GetCurrent(&INA226_02) / 1000000.0;
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }

            adc_send.v_solar /= 10.0;
            if (adc_send.v_solar > SOLAR_VOLTAGE_OFFSET)
            {
                adc_send.v_solar -= SOLAR_VOLTAGE_OFFSET;
            }
            adc_send.i_solar /= 10.0;
            if (adc_send.i_solar > SOLAR_CURRENT_OFFSET)
            {
                adc_send.i_solar -= SOLAR_CURRENT_OFFSET;
            }
            adc_send.v_bat /= 10.0;
            if (adc_send.v_bat > BATTERY_VOLTAGE_OFFSET)
            {
                adc_send.v_bat -= BATTERY_VOLTAGE_OFFSET;
            }
            adc_send.i_bat /= 10.0;
            if (adc_send.i_bat > BATTERY_CURRENT_OFFSET)
            {
                adc_send.i_bat -= BATTERY_CURRENT_OFFSET;
            }

            adc_send.power_solar = adc_send.v_solar * adc_send.i_solar;
            adc_send.power_bat = adc_send.v_bat * adc_send.i_bat;

            /* Logging data to debug screen */
            ESP_LOGI(i2c_tag, "Voltage solar: %02.3f V", adc_send.v_solar);
            ESP_LOGI(i2c_tag, "Current solar: %02.3f A", adc_send.i_solar);
            ESP_LOGI(i2c_tag, "Voltage battery: %02.3f V", adc_send.v_bat);
            ESP_LOGI(i2c_tag, "Current battery: %02.3f A", adc_send.i_bat);
            ESP_LOGI(i2c_tag, "Power solar: %02.3f W", adc_send.power_solar);
            ESP_LOGI(i2c_tag, "Power battery: %02.3f W", adc_send.power_bat);
            /* Queue send data struct to Charge Task */
            xQueueSend(adc_charger_queue_handle, &adc_send, portMAX_DELAY);
        }
        else
        {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
}

void mppt_algorithm(charger_mppt_t *charger_mppt)
{
    charger_mppt->adc_val.power_solar = charger_mppt->adc_val.v_solar * charger_mppt->adc_val.i_solar;
    charger_mppt->adc_val.power_bat = charger_mppt->adc_val.v_bat * charger_mppt->adc_val.i_bat;
    charger_mppt->delta_P = charger_mppt->adc_val.power_solar - charger_mppt->pre_power_solar;
    charger_mppt->delta_D = charger_mppt->cur_duty - charger_mppt->pre_duty;
    charger_mppt->delta_V = charger_mppt->cur_voltage_solar - charger_mppt->pre_voltage_solar;
    charger_mppt->pre_duty = charger_mppt->cur_duty;
    if (charger_mppt->delta_P >= 0)
    {
        if (charger_mppt->delta_D >= 0)
        {
            charger_mppt->cur_duty++;
        }
        else
        {
            charger_mppt->cur_duty--;
        }
    }
    else
    {
        if (charger_mppt->delta_D >= 0)
        {
            charger_mppt->cur_duty--;
        }
        else
        {
            charger_mppt->cur_duty++;
        }
    }
    charger_mppt->pre_power_solar = charger_mppt->adc_val.power_solar;
    charger_mppt->pre_power_bat = charger_mppt->adc_val.power_bat;
}

void charger_task(void *pvParameter)
{
    ESP_LOGI(charger_tag, "Starting charger task");

    /* Variables Definition */
    BaseType_t queue_re;
    adc_struct_t adc_rcv;
    bool setup_charger = false;
    charger_mppt_t charger_mppt = {
        .min_duty = (DUTY_MIN * 1023) / 100,
        .max_duty = (DUTY_MAX * 1023) / 100,
        .i_float_rate_bat = 0.1,
        .v_cccv_rate_bat = 12.9,
        .i_cccv_rate_bat = 1.5,
        .pre_voltage_solar = 0};

    /* Create Queue for sending and receiving struct data */
    adc_charger_queue_handle = xQueueCreate(20, sizeof(adc_struct_t));
    
    /* Initialize PWM */
    ESP_ERROR_CHECK(setupPWM());
    ESP_LOGI("PWM", "PWM initialized successfully");

    while (1)
    {
        /* If QueueReceive data struct from I2C Task, return pdTrue */
        queue_re = xQueueReceive(adc_charger_queue_handle, &adc_rcv, 100 / portTICK_PERIOD_MS);
        if (queue_re == pdTRUE)
        {
            charger_mppt.adc_val = adc_rcv;

            /* Checking the connection of Solar Panel and Battery*/
            if (setup_charger == false)
            {
                /* Solar Panel and Battery are all connected */
                if (charger_mppt.adc_val.v_solar > 13.0 && charger_mppt.adc_val.v_bat > 11.0)
                {
                    ESP_LOGI(charger_tag, "Solar panel and battery connected. Voltage solar: %f, voltage battery: %f", charger_mppt.adc_val.v_solar, charger_mppt.adc_val.v_bat);
                    vTaskSuspend(i2c_task_handle);
                    charger_mppt.cur_duty = (int32_t)((charger_mppt.adc_val.v_bat / charger_mppt.adc_val.v_solar) * 1023);
                    if (charger_mppt.cur_duty > charger_mppt.max_duty)
                    {
                        charger_mppt.cur_duty = charger_mppt.max_duty;
                    }
                    else if (charger_mppt.cur_duty < charger_mppt.min_duty)
                    {
                        charger_mppt.cur_duty = charger_mppt.min_duty;
                    }
                    ledc_set_duty(LEDC_HS_MODE, LEDC_HS_CH0_CHANNEL, charger_mppt.cur_duty);
                    ledc_update_duty(LEDC_HS_MODE, LEDC_HS_CH0_CHANNEL);
                    ESP_LOGI(charger_tag, "Duty setup: %f, delay 3s", (float)charger_mppt.cur_duty * 100 / 1023);
                    vTaskDelay(3000 / portTICK_PERIOD_MS);
                    setup_charger = true;
                    switch_source(1);
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                    vTaskResume(i2c_task_handle);
                }
                else if (charger_mppt.adc_val.v_solar > 13.0 && charger_mppt.adc_val.v_bat < 10.0)
                {
                    ESP_LOGI(charger_tag, "Please connect to battery");
                    setup_charger = false;
                }
                else if (charger_mppt.adc_val.v_solar < 13.0 && charger_mppt.adc_val.v_bat > 10.0)
                {
                    ESP_LOGI(charger_tag, "Please connect to solar");
                    setup_charger = false;
                } else 
                {
                    ESP_LOGI(charger_tag, "Please connect to and solar panel and battery");
                    setup_charger = false;
                }
            }
            else if (setup_charger == true)
            {
                if (charger_mppt.adc_val.v_solar <= 13.0 || charger_mppt.adc_val.v_bat <= 11.0)
                {
                    setup_charger = false;
                }

                ESP_LOGI(charger_tag, "Setup done, start charging");
                if (charger_mppt.adc_val.v_bat >= charger_mppt.v_cccv_rate_bat) // charger_mppt.v_cccv_rate_bat = 12.9
                {
                    if (charger_mppt.adc_val.i_bat <= charger_mppt.i_float_rate_bat)
                    {
                        switch_source(0);
                    }
                    else
                    {
                        charger_mppt.cur_duty--;
                    }
                }
                else
                {
                    if (charger_mppt.adc_val.i_bat <= charger_mppt.i_cccv_rate_bat) // charger_mppt.i_cccv_rate_bat = 1.5
                    {
                        mppt_algorithm(&charger_mppt);
                    }
                    else
                    {
                        charger_mppt.cur_duty--;
                    }
                }

                if (charger_mppt.cur_duty > charger_mppt.max_duty)
                {
                    charger_mppt.cur_duty = charger_mppt.max_duty;
                }
                else if (charger_mppt.cur_duty < charger_mppt.min_duty)
                {
                    charger_mppt.cur_duty = charger_mppt.min_duty;
                }

                ledc_set_duty(LEDC_HS_MODE, LEDC_HS_CH0_CHANNEL, charger_mppt.cur_duty);
                ledc_update_duty(LEDC_HS_MODE, LEDC_HS_CH0_CHANNEL);
            }

            ESP_LOGI(charger_tag, "Voltage solar: %02.3f V", charger_mppt.adc_val.v_solar);
            ESP_LOGI(charger_tag, "Current solar: %02.3f A", charger_mppt.adc_val.i_solar);
            ESP_LOGI(charger_tag, "Voltage battery: %02.3f V", charger_mppt.adc_val.v_bat);
            ESP_LOGI(charger_tag, "Current battery: %02.3f A", charger_mppt.adc_val.i_bat);
            ESP_LOGI(charger_tag, "Power Solar: %02.3f W", charger_mppt.pre_power_solar);
            ESP_LOGI(charger_tag, "Power Battery: %02.3f W", charger_mppt.pre_power_bat);
            ESP_LOGI(charger_tag, "Delta of duty: %d", charger_mppt.delta_D);
            ESP_LOGI(charger_tag, "Delta of power: %02.3f W", charger_mppt.delta_P);
            ESP_LOGI(charger_tag, "Current_duty: %d", charger_mppt.cur_duty);

            if (charger_mppt.pre_power_bat < charger_mppt.pre_power_solar)
            {
                charger_mppt.performance = charger_mppt.pre_power_bat * 100.0 / charger_mppt.pre_power_solar;
                ESP_LOGI("MPPT", "Performance: %2.1f %%", charger_mppt.performance);
            }
            else
            {
                ESP_LOGI("MPPT", "Performance: %2.1f %%", 0.0);
            }
        }
    }
}

/**
 * @brief i2c master initialization
 */
static esp_err_t setup_i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    i2c_param_config(i2c_master_port, &conf);

    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

static esp_err_t setupGPIO(void)
{
    /* Reset GPIO PIN */
    gpio_reset_pin(RELAY);
    gpio_reset_pin(MPPT_EN);

    /* Set the GPIO DIR */
    gpio_set_direction(MPPT_EN, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY, GPIO_MODE_OUTPUT);

    /* Set GPIO LEV */
    gpio_set_level(MPPT_EN, 0);
    gpio_set_level(RELAY, 0);

    return ESP_OK;
}

static esp_err_t setupPWM(void)
{
    ledc_timer_config_t ledc_timer1 = {
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 50000,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer1));
    ledc_channel_config_t ledc_channel1 = {
        .channel = LEDC_CHANNEL_1,
        .duty = 1,
        .gpio_num = MPPT_PWM0A_OUT,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .hpoint = 0,
        .timer_sel = LEDC_TIMER_0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel1));

    return ESP_OK;
}

size_t i2c_master_write_ina226(int Address, const uint8_t *Buffer, size_t BytesToWrite)
{
    err = i2c_master_write_to_device(I2C_MASTER_NUM, Address, Buffer, BytesToWrite, I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);
    if (err == ESP_OK)
        return BytesToWrite;
    else
        return 0;
}

size_t i2c_master_read_ina226(int Address, uint8_t *Buffer, size_t BufferMaxLen)
{
    err = i2c_master_read_from_device(I2C_MASTER_NUM, Address, Buffer, BufferMaxLen, I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);
    if (err == ESP_OK)
        return BufferMaxLen;
    else
        return 0;
}
