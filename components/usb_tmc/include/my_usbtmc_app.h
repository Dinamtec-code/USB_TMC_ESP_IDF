#ifndef USBTMC_APP_H_
#define USBTMC_APP_H_

#include "tinyusb.h"
#include "tusb.h"

#ifdef __cpluslpus
extern "C"
{
#endif

    void tmc_hal_init(void);
    void usbtmc_app_task(void *pvParameters);

    tusb_desc_device_t const *usb_desc_get_dev();
    uint8_t const *usb_desc_get_cfg();
    char const **usb_desc_get_string_desc();
    int usb_desc_get_string_desc_count();

#ifdef __cplusplus
}
#endif

#endif
