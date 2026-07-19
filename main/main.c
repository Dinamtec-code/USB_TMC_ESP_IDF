#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"

#include "usb_tmc_init.h"
#include "usb_tmc_cb.h"

static const char *TAG = "APP_MAIN";

/* // Declaramos externamente las funciones para que el compilador las vea
extern uint8_t const *tud_descriptor_device_cb(void);
extern uint8_t const *tud_descriptor_configuration_cb(uint8_t index);
extern uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid); */

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

    tmc_hal_init();

    /*    BaseType_t ret2 = xTaskCreatePinnedToCore(usbtmc_app_task, "Task_COMM", 1024 * 8, NULL, 6, NULL, 1);
       if (ret2 != pdPASS)
       {
           ESP_LOGI(TAG, "Error creando Task_COMM: %d", ret2);
       }
       else
       {
           ESP_LOGI(TAG, "Task_COMM creada exitosamente");
       } */

    vTaskDelete(NULL);
}
