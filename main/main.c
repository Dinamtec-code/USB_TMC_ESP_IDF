#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"

#include "usb_tmc_init.h"
#include "usb_tmc_cb.h"

#include "driver/ledc.h"
#include "driver/gpio.h" // Necesario en v6.0 para gpio_num_t
#include "esp_err.h"

#include "scpi_engine.h"

static const char *TAG = "APP_MAIN";

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE // OBLIGATORIO en ESP32-S3
#define LEDC_OUTPUT_IO (48)
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define LEDC_FREQUENCY (25000)

static void example_ledc_init(void)
{
    // 1. Configuración del temporario
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. Configuración del canal
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .gpio_num = LEDC_OUTPUT_IO,
        .duty = 0,
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}
// ---------------------------------------------------------
// Punto de entrada principal
// ---------------------------------------------------------

void app_main(void)
{

    ESP_LOGI(TAG, "Arrancando Firmware de Instrumentación TMC...");

    // esp_task_wdt_deinit();

    // 1. Inicializar almacenamiento no volátil (NVS) para configuraciones persistentes [cite: 306]
    esp_err_t ret1 = nvs_flash_init();
    if (ret1 == ESP_ERR_NVS_NO_FREE_PAGES || ret1 == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret1 = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret1);

    // tmc_hal_init();
    // iface_init();

    example_ledc_init();

    // Ejemplo: 50% Duty Cycle (512 de 1023)
    uint32_t duty = 256;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

    xTaskCreatePinnedToCore(scpi_engine_task, "Task_COMM", 8024, NULL, 4, NULL, 1);

    vTaskDelete(NULL);
}
