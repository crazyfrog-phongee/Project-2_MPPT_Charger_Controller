/**
 * @file mppt.h
 * @author PhongEE
 * @brief MPPT Charger Controller
 * @version 1.0
 * @date 14-03-2023
 *
 * @copyright Copyright (c) 2023
 *
 * 
 */


#include <stdio.h>
#include <string.h>
#include <math.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_err.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"


#define RELAY                       (GPIO_NUM_25)
#define MPPT_PWM0A_OUT              (GPIO_NUM_32)
#define MPPT_EN                     (GPIO_NUM_33)

#define DUTY_MIN                    40
#define DUTY_MAX                    90

#define SOLAR_VOLTAGE_OFFSET        (0.00)
#define SOLAR_CURRENT_OFFSET        (0)
#define BATTERY_VOLTAGE_OFFSET      (0)
#define BATTERY_CURRENT_OFFSET      (0.06)

#define MAX_BATTERY_VOLTAGE         (12.9)
#define MAX_BATTERY_CURRENT         (3.0)

#define LEDC_HS_MODE                LEDC_HIGH_SPEED_MODE
#define LEDC_HS_CH0_CHANNEL         LEDC_CHANNEL_1

typedef struct 
{
    double v_solar;
    double i_solar;
    double v_bat;
    double i_bat;
    double power_solar;
    double power_bat;
} adc_struct_t;

typedef struct
{
    adc_struct_t adc_val;
    int32_t min_duty;
    int32_t max_duty;
    double pre_power_solar;
    double pre_power_bat;
    double delta_P;
    int32_t pre_duty;
    int32_t cur_duty;
    int32_t delta_D;
    double delta_V;
    double pre_voltage_solar;
    double cur_voltage_solar;
    double i_float_rate_bat;
    double v_cccv_rate_bat;
    double i_cccv_rate_bat;
    float performance;
} charger_mppt_t;

typedef enum 
{
    BAT_NOT_CONNECTED = 0,
    BAT_CONNECTED,
    BAT_CHARGE,
    BAT_BULK,
    BAT_ABSORPTION,
    BAT_FLOAT,
    BAT_DISCHARGE
} bat_status_t;

typedef enum
{
    SOLAR_NOT_CONNECTED = 0,
    SOLAR_CONNECTED,
    SOLAR_CHARGE,
    SOLAR_DISCHARGE
} solar_status_t;

typedef struct 
{
    bat_status_t bat_status;
    double v_bat;
    double i_bat;
    double power_bat;
} bat_handle_t;

typedef struct 
{
    solar_status_t solar_status;
    double v_solar;
    double i_solar;
    double power_solar;
} solar_handle_t;

typedef struct 
{
    solar_handle_t solar_handle;
    bat_handle_t bat_handle;
    charger_mppt_t charger_handle;
} mppt_handle_struct_t;





