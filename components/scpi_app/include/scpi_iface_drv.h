#ifndef SCPI_IFACE_DVR_H_
#define SCPI_IFACE_DVR_H_

#include "freertos/stream_buffer.h"
#include "freertos/queue.h"
#include "freertos/FreeRTOS.h"
#include "scpi_engine.h"
#include "iface_msg.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/************************************************************
 * Mensajes estuctura para la transferencais de los mensajes
 ************************************************************/
#define IFACE_MSG_POOL_SIZE 10 // Número máximo de mensajes simultáneos
#define IFACE_MSG_H_SIZE sizeof(iface_msg_handle_t)
#define TX_STREAM_SIZE 4096

    typedef enum
    {
        DRV_IFACE_USBTMC = 0, /**< Interfaz USB TMC */
        DRV_IFACE_USART,      /**< Interfaz serial USART (UART sobre RS-232/TTL) */
        DRV_IFACE_TCP,        /**< Interfaz de red TCP/IP */
        DRV_IFACE_MAX         /**< Valor centinela para verificación de límites */
    } iface_id_t;

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

    typedef struct
    {
        StreamBufferHandle_t tx_stream; // Inyectado por la App
        QueueHandle_t rx_msg_queue;     // Inyectado por la App (Cola de punteros a mensajes)
        bool running;
    } drv_context_t;

    /**
     * @brief Inicializa el driver.
     * @param tx_buffer_handle El buffer estático donde la App escribirá para enviar.
     * @return pdTRUE si exitoso.
     */
    typedef bool (*scpi_drv_init_t)(StreamBufferHandle_t tx_stream, QueueHandle_t rx_queue);

    /**
     * @brief Destruye el driver.
     */
    typedef void (*scpi_drv_deinit_t)(void);

    typedef iface_msg_handle_t (*get_slot_t)(void);
    typedef iface_msg_handle_t (*get_next_t)(void);
    typedef bool (*inform_parser_events_t)(iface_event_t event);

    typedef struct SCPI_IFACE *iface_handler_t;
    typedef struct SCPI_IFACE iface_struct_t;

    void scpi_drv_register_iface(iface_handler_t iface);
    void scpi_drv_unregister_iface(iface_handler_t *iface);
    iface_handler_t comm_get_iface(iface_id_t id);
    StreamBufferHandle_t iface_tx_stream_init(void);
    QueueHandle_t iface_msg_queue_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SCPI_IFACE_DVR_H_*/