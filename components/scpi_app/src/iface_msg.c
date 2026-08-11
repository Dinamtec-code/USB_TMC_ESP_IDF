#include "iface_msg.h"
#include <string.h>
#include "freertos/stream_buffer.h"
#include "freertos/queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


portMUX_TYPE iface_spinlock = portMUX_INITIALIZER_UNLOCKED;

// 1. Definición Completa de la Estructura (Oculta)
// Esto define la "forma" de un mensaje, pero NO donde vive.
struct IFACE_MSG
{
    iface_msg_state_t state;
    iface_msg_error_flags_t error_flags;
    StreamBufferHandle_t stream;
    StaticStreamBuffer_t stream_buffer_Struct;
    uint8_t stream_memory[RX_STREAM_BUFFER_SIZE + 1];
};

// 2. Implementación de los Métodos de la Interfaz

void iface_msg_reset(iface_msg_handle_t handle)
{
    if (!handle)
        return;

    handle->state = IFACE_MSG_FREE;
    handle->error_flags = MSG_ERR_NONE;
    if (handle->stream != NULL)
    {
        xStreamBufferReset(handle->stream);
    }
}

void iface_msg_set_state(iface_msg_handle_t handle, iface_msg_state_t new_state)
{
    if (!handle)
        return;

    portENTER_CRITICAL(&iface_spinlock);
    handle->state = new_state;
    portEXIT_CRITICAL(&iface_spinlock);
}

iface_msg_state_t iface_msg_get_state(iface_msg_handle_t handle)
{
    if (!handle)
        return 0;
    iface_msg_state_t val;
    portENTER_CRITICAL(&iface_spinlock);
    val = handle->state;
    portEXIT_CRITICAL(&iface_spinlock);
    return val;
}

// Manejo de errores (flags)
void iface_msg_set_error_flag(iface_msg_handle_t msg, iface_msg_error_flags_t flag)
{
    msg->error_flags |= flag;
}

void iface_msg_clear_error_flag(iface_msg_handle_t msg, iface_msg_error_flags_t flag)
{
    msg->error_flags &= (~flag);
}

bool iface_msg_is_error(iface_msg_handle_t msg, iface_msg_error_flags_t flag)
{
    if (!msg)
        return false;
    // Lectura de un uint32_t alineado → atómica en ESP32
    return (msg->error_flags & flag) != 0;
}

StreamBufferHandle_t iface_msg_get_stream(iface_msg_handle_t handle)
{
    if (!handle)
        return NULL;
    return handle->stream;
}

size_t iface_msg_write(iface_msg_handle_t msg, const uint8_t *data, size_t len)
{
    if (msg == NULL || data == NULL)
        return 0;
    if (msg->stream == NULL)
        return 0;

    size_t written = xStreamBufferSend(msg->stream, data, len, 0);

    // Si no se pudo escribir todo, marcamos overflow
    if (written < len)
    {
        // Se activa el flag de error (bit dentro del estado)
        iface_msg_set_error_flag(msg, MSG_ERR_OVERFLOW);
    }
    return written;
}