#ifndef SCPI_IFACE_DVR_H_
#define SCPI_IFACE_DVR_H_

#include "freertos/stream_buffer.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        IFACE_NONE = 0x00,
        IFACE_RX_READY = 0x01,
        IFACE_TX_READY = 0x02,
        IFACE_HW_READY = 0x04,
        IFACE_DONE = 0x07, // (RX | TX | HW)
        IFACE_ERROR = 0x80
    } iface_status_t;

    // En la estructura de la interfaz (visible al consumidor pero status protegido conceptualmente)
    typedef struct
    {
        volatile iface_status_t status; // 'volatile' por si hay ISRs actualizando estado
        iface_status_t (*get_status)(void);
        bool (*set_rx_stream)(StreamBufferHandle_t h);
        bool (*set_tx_stream)(StreamBufferHandle_t h);
        bool (*peripheric_init)(void);
        bool (*set_in_events_queue)(QueueHandle_t q);
        bool (*set_out_events_queue)(QueueHandle_t q);
    } iface_struct_t;

    typedef iface_struct_t *iface_handle_t;

#ifdef __cplusplus
}
#endif

#endif /* SCPI_IFACE_DVR_H_*/