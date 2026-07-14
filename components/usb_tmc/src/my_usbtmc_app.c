#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#include "tusb.h"
#include "tinyusb.h"

#include "my_usbtmc_app.h"
#include "esp_log.h"

static const char *TAG_USB = "APP_USB";

#define IEEE4882_STB_QUESTIONABLE (0x08u)
#define IEEE4882_STB_MAV (0x10u)
#define IEEE4882_STB_SER (0x20u)
#define IEEE4882_STB_SRQ (0x40u)

#define BOARD_TUD_RHPORT 0

// 0=not query, 1=queried, 2=delay,set(MAV), 3=delay 4=ready?
// (to simulate delay)
static volatile uint16_t queryState = 0;
static volatile uint32_t queryDelayStart;
static volatile uint32_t bulkInStarted;
static volatile uint32_t idnQuery;

void tmc_hal_init(void)
{
  ESP_LOGI(TAG_USB, "Inicializando USB-TMC con la estructura v6.0.2...");

  const tinyusb_config_t tusb_cfg = {
      .port = TINYUSB_PORT_FULL_SPEED_0, // Corregido: constante del enum

      .task = {
          .size = 4096, // Corregido: el campo se llama 'size'
          .priority = 5,
          .xCoreID = 1, // Obligatorio en tu versión
      },

      .descriptor = {
          .device = usb_desc_get_dev(),
          .qualifier = NULL,
          .string = usb_desc_get_string_desc(),
          .string_count = usb_desc_get_string_desc_count(),
          .full_speed_config = usb_desc_get_cfg(),
          .high_speed_config = NULL,
      },

      .phy = {
          .skip_setup = false,
          .self_powered = false,
          .vbus_monitor_io = -1,
      },

      .event_cb = NULL,
      .event_arg = NULL,
  };

  esp_err_t err = tinyusb_driver_install(&tusb_cfg);
  ESP_LOGI(TAG_USB, "Driver install retornó: %d", err);
  ESP_ERROR_CHECK(err);
}

/* // Invoked when device is mounted
void tud_mount_cb(void)
{
  ESP_LOGI(TAG_USB, "USB Montado!");
} */

/* // Invoked when device is unmounted
void tud_umount_cb(void)
{
  ESP_LOGI(TAG_USB, "USB DESmontado!");
} */

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void)remote_wakeup_en;
  ESP_LOGI(TAG_USB, "USB Despertado");
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  ESP_LOGI(TAG_USB, "USB resumen");
}
