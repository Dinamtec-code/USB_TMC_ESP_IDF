#ifndef IFACE_MSG_SLOT_H_
#define IFACE_MSG_SLOT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/queue.h"

#define RX_STREAM_BUFFER_SIZE 1024
    // --- Definición de la Interfaz ---

    // Handle opaco: El usuario solo conoce el puntero.
    typedef struct IFACE_MSG *iface_msg_handle_t;
    typedef struct IFACE_MSG iface_msg_struct_t;

    typedef enum
    {
        IFACE_MSG_FREE = 0,
        IFACE_MSG_RX_ACTIVE,
        IFACE_MSG_READY,
        IFACE_MSG_PROCESSING
    } iface_msg_state_t;

    typedef enum
    {
        MSG_ERR_NONE = 0,
        MSG_ERR_OVERFLOW = (1 << 0),
        MSG_ERR_INTERRUPT = (1 << 1),
        MSG_ERR_UNTERMIN = (1 << 2)
    } iface_msg_error_flags_t;

    // --- Métodos de la Interfaz (API Pública) ---

    // Inicialización de la instancia (se llama a esto cuando se obtiene un slot libre)
    // Resetea el estado y datos internos de ESTE mensaje específico.
    void iface_msg_reset(iface_msg_handle_t handle);

    // Manejo del estado
    void iface_msg_set_state(iface_msg_handle_t msg, iface_msg_state_t new_state);
    iface_msg_state_t iface_msg_get_state(iface_msg_handle_t msg);

    // Manejo de errores (flags)
    void iface_msg_set_error_flag(iface_msg_handle_t msg, iface_msg_error_flags_t flag);
    void iface_msg_clear_error_flag(iface_msg_handle_t msg, iface_msg_error_flags_t flag);
    bool iface_msg_is_error(iface_msg_handle_t msg, iface_msg_error_flags_t flag);

    StreamBufferHandle_t iface_msg_get_stream(iface_msg_handle_t handle);

    // Escritura de datos en el stream del mensaje
    size_t iface_msg_write(iface_msg_handle_t msg, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /*IFACE_MSG_SLOT_H_*/