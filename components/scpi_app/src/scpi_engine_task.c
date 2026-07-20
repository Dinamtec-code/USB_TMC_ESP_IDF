
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"

#include <stdlib.h>
#include <stdint.h>

#include "scpi/scpi.h"
#include "scpi_engine_task.h"
#include "usb_tmc_process.h"
#include "scpi_iface_drv.h"

static uint8_t rx_data[512];

char tx_buffer[1024];
int tx_length;

// steam buffers
#define RX_STREAM_BUFFER_SIZE 1024
#define TX_STREAM_BUFFER_SIZE 512
static StreamBufferHandle_t rx_stream_handle;
static StreamBufferHandle_t tx_stream_handle;

// Memoria estática para los stream
static StaticStreamBuffer_t rxStreamBufferStruct;
static StaticStreamBuffer_t txStreamBufferStruct;
static uint8_t rx_stream_memory[RX_STREAM_BUFFER_SIZE + 1];
static uint8_t tx_stream_memory[TX_STREAM_BUFFER_SIZE + 1];
static const size_t xTrigger = 1;

// Queues
#define ITEM_SIZE 1
#define IN_QUEUE_SIZE 10
#define OUT_QUEUE_SIZE 10
static QueueHandle_t in_events_queue_handle;
static QueueHandle_t out_events_queue_handle;

// Memoria estática para las colas
static StaticQueue_t in_events_QueueStruct;
static StaticQueue_t out_events_QueueStruct;
static uint8_t in_events_queue_memory[IN_QUEUE_SIZE * ITEM_SIZE];
static uint8_t out_events_queue_memory[OUT_QUEUE_SIZE * ITEM_SIZE];
void scpi_stream_register(StreamBufferHandle_t stream)
{
    rx_stream_handle = xStreamBufferCreateStatic(RX_STREAM_BUFFER_SIZE,
                                                 xTrigger,
                                                 rx_stream_memory,
                                                 &rxStreamBufferStruct);
    tx_stream_handle = xStreamBufferCreateStatic(TX_STREAM_BUFFER_SIZE,
                                                 xTrigger,
                                                 tx_stream_memory,
                                                 &txStreamBufferStruct);
}

bool scpi_event_queues_register(void)
{
    in_events_queue_handle = xQueueCreateStatic(IN_QUEUE_SIZE,
                                                ITEM_SIZE,
                                                in_events_queue_memory,
                                                &in_events_QueueStruct);
    out_events_queue_handle = xQueueCreateStatic(OUT_QUEUE_SIZE,
                                                 ITEM_SIZE,
                                                 out_events_queue_memory,
                                                 &out_events_QueueStruct);

    return (in_events_queue_handle != NULL && out_events_queue_handle != NULL);
}

bool driver_iface_init(iface_handle_t iface)
{
    if (!iface)
        return false;

    // 1. Configurar RX si no está listo
    if (!(iface->status & IFACE_RX_READY))
    {
        if (iface->set_rx_stream && iface->set_rx_stream(rx_stream_handle))
        {
            iface->status |= IFACE_RX_READY;
        }
        else
        {
            iface->status |= IFACE_ERROR;
            return false;
        }
    }

    // 2. Configurar TX si no está listo
    if (!(iface->status & IFACE_TX_READY))
    {
        if (iface->set_tx_stream && iface->set_tx_stream(tx_stream_handle))
        {
            iface->status |= IFACE_TX_READY;
        }
        else
        {
            iface->status |= IFACE_ERROR;
            return false;
        }
    }

    // 3. Inicializar Hardware si no está listo
    if (!(iface->status & IFACE_HW_READY))
    {
        if (iface->peripheric_init && iface->peripheric_init())
        {
            iface->status |= IFACE_HW_READY;
        }
        else
        {
            // No marcamos ERROR inmediatamente si el HW puede aparecer luego (hot-plug)
            // O marcamos ERROR si es crítico.
            return false;
        }
    }

    return (iface->status == IFACE_DONE);
}

StreamBufferHandle_t get_rx_stream(void)
{
    return rx_stream_handle;
}

StreamBufferHandle_t get_tx_stream(void)
{
    return tx_stream_handle;
}

// Callback que llama libscpi cuando genera un string de respuesta
size_t SCPI_Write(void *context, const char *data, size_t len)
{
    if (len < sizeof(tx_buffer))
    {
        memcpy(tx_buffer, data, len);
        tx_length = len;
        // Inyectamos el evento a la máquina de estados
        usb_tmc_fsm_process(EV_SCPI_REPLY, NULL, 0);
    }
    return len;
}

void set_event(scpi_status_t status)
{
}

void leer_eventos()
{
}

void scpi_engine_task(void *pvParameters)
{
    // Creamos el StreamBuffer (ej. 1024 bytes)
    rx_stream_handle = xStreamBufferCreate(1024, 1);
    if (rx_stream_handle == NULL)
    {
        ESP_LOGI("SCPI", "Buffer no iniciado");
    }
    else
    {
        ESP_LOGI("SCPI", "Buffer iniciado correctamente");
    }

    char local_buf[256];

    while (1)
    {
        // Dormimos hasta recibir el EV_RX_END
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Sacamos todo del StreamBuffer a medida
        size_t bytes_read;
        do
        {
            bytes_read = xStreamBufferReceive(rx_stream_handle, local_buf, sizeof(local_buf), 0);
            if (bytes_read > 0)
            {
                // Aquí podrías revisar si el último byte es '\n', si no lo es, agregarlo.
                if (local_buf[bytes_read - 1] != '\n' && bytes_read < sizeof(local_buf))
                {
                    local_buf[bytes_read] = '\n';
                    bytes_read++;
                }

                // Inyectamos a libscpi (esta función demora lo que tenga que demorar)
                // SCPI_Input(&scpi_context, local_buf, bytes_read);
            }
        } while (bytes_read > 0);

        // // Si la respuesta NO fue un Query, la FSM quedó en IDLE. Le decimos al bus que lea.
        // if (current_state == STATE_IDLE)
        // {
        //     //            tud_usbtmc_start_bus_read();
        // }
    }
}