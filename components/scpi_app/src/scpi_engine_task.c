
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"

#include <stdlib.h>
#include <stdint.h>

#include "scpi/scpi.h"
#include "scpi_engine_task.h"
#include "usb_tmc_fsm_process.h"

StreamBufferHandle_t rx_stream_handle;

char tx_buffer[1024];
int tx_length;
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

// La tarea de usuario de FreeRTOS
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