#include "scpi_iface_drv.h"
#include "iface_msg.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/queue.h"

/* iface_spinlock is defined in iface_msg.c, declared extern in iface_msg.h */

/* Array estático de mensajes */
static iface_msg_struct_t iface_msg_pool[IFACE_MSG_POOL_SIZE];

// Cola de punteros usada para enviar mensajes listos a la App
static QueueHandle_t rx_msg_queue = NULL;
static StaticQueue_t static_queue;
static uint8_t queue_memory[IFACE_MSG_POOL_SIZE * IFACE_MSG_H_SIZE];

/* Stream de transmision */
StreamBufferHandle_t tx_stream;
StaticStreamBuffer_t tx_stream_buffer_struct;
uint8_t tx_stream_memory[TX_STREAM_SIZE + 1];

/* Metodos estaticos de la interfaz */
static iface_msg_handle_t iface_get_free_slot(void)
{
    for (int i = 0; i < IFACE_MSG_POOL_SIZE; i++)
    {
        iface_msg_handle_t msg = &iface_msg_pool[i];

        // Comprobamos de forma atómica que esté FREE y lo pasamos a RX_ACTIVE
        portENTER_CRITICAL(&iface_spinlock);
        if (msg->state == IFACE_MSG_FREE)
        {
            msg->state = IFACE_MSG_RX_ACTIVE;
            portEXIT_CRITICAL(&iface_spinlock);
            // Reseteamos el stream buffer antes de empezar a recibir
            xStreamBufferReset(msg->stream);
            return msg;
        }
        portEXIT_CRITICAL(&iface_spinlock);
    }
    return NULL; // No hay slots libres
}

static iface_msg_handle_t iface_get_next_slot(void)
{
    iface_msg_handle_t msg = NULL;
    if (xQueueReceive(rx_msg_queue, &msg, 0) == pdTRUE)
    {
        return msg;
    }
    return NULL;
}

static size_t iface_get_msg_available(void)
{
    return uxQueueMessagesWaiting(rx_msg_queue);
}

/*Tabla de interfaces disponibles*/
iface_handler_t iface_table[DRV_IFACE_MAX] = {NULL};

// Inicializar el pool (se llama una vez antes de arrancar el driver)
static bool iface_msg_pool_init(QueueHandle_t rx_q)
{
    if (rx_q == NULL)
        return false;
    rx_msg_queue = rx_q; // Guardamos la cola que usará el driver

    for (int i = 0; i < IFACE_MSG_POOL_SIZE; i++)
    {
        iface_msg_handle_t msg = &iface_msg_pool[i];

        // Crear el stream buffer estático para este mensaje
        msg->stream = xStreamBufferCreateStatic(
            RX_STREAM_BUFFER_SIZE,
            1, // tamaño del disparo
            msg->stream_memory,
            &msg->stream_buffer_Struct);
        if (msg->stream == NULL)
            return false;

        msg->state = IFACE_MSG_FREE;
        msg->error_flags = MSG_ERR_NONE;
    }
    return true;
}

StreamBufferHandle_t iface_tx_stream_init(void)
{
    tx_stream = xStreamBufferCreateStatic(
        TX_STREAM_SIZE,
        1, // tamaño del disparo
        tx_stream_memory,
        &tx_stream_buffer_struct);
    return tx_stream;
}

/*Inicializacion de la interfaz y registro*/

QueueHandle_t iface_msg_queue_init(void)
{
    QueueHandle_t qu = xQueueCreateStatic(
        IFACE_MSG_POOL_SIZE,
        IFACE_MSG_H_SIZE,
        queue_memory,
        &static_queue);

    iface_msg_pool_init(qu);

    return qu;
}

void scpi_drv_register_iface(iface_handler_t iface)
{
    if (iface && iface->id < DRV_IFACE_MAX)
    {
        iface_table[iface->id] = iface;
        iface->get_free_slot = iface_get_free_slot;
        iface->get_next_slot = iface_get_next_slot;
        iface->get_msg_available = iface_get_msg_available;
    }
}

void scpi_drv_unregister_iface(iface_handler_t iface)
{
    if (iface && iface->id < DRV_IFACE_MAX)
    {
        iface_table[iface->id] = NULL;
    }
}

iface_handler_t comm_get_iface(iface_id_t id)
{
    if (id < DRV_IFACE_MAX)
        return (iface_table[id]);
    return NULL;
}
