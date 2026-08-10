#ifndef SCPI_IFACE_DVR_H_
#define SCPI_IFACE_DVR_H_

#include "freertos/stream_buffer.h"
#include "scpi_engine.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        SCPI_IFACE_NONE = 0x00,
        SCPI_IFACE_RX_READY = 0x01,
        SCPI_IFACE_TX_READY = 0x02,
        SCPI_IFACE_HW_READY = 0x04,
        SCPI_IFACE_ABORT_TX = 0x08,
        SCPI_IFACE_ABORT_RX = 0x08,
        SCPI_IFACE_ERROR = 0x80
    } iface_status_t;

    typedef enum
    {
        /* events from SCPI engine to driver*/
        EV_SCPI_NONE = 0x00,
        EV_SCPI_MSG_DONE = 0x01,
        EV_SCPI_PROCESS_DONE = 0x02,
        /* events from driver to SCPI engine */
        EV_SCPI_ERROR_410 = 0x04,
        EV_SCPI_ERROR_420 = 0x08
    } iface_event_t;

    /* En la estructura de la interfaz*/
    typedef struct
    {
        void *context;
        iface_status_t status;
        /* initialized method */
        bool (*set_rx_stream)(StreamBufferHandle_t h);
        bool (*set_tx_stream)(StreamBufferHandle_t h);
        bool (*set_events_error_queue)(QueueHandle_t error_queue);
        bool (*peripheric_init)(StreamBufferHandle_t rx_stm, StreamBufferHandle_t tx_stm, QueueHandle_t error_q);
        /* scpi service method */
        bool (*inform_events)(iface_event_t event);
        iface_event_t (*get_iface_events)(void);
    } iface_struct_t;

    typedef iface_struct_t *iface_handle_t;

#ifdef __cplusplus
}
#endif

#endif /* SCPI_IFACE_DVR_H_*/