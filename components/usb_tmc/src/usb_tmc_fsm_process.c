#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "tusb.h"
#include "class/usbtmc/usbtmc_device.h"
#include "scpi/scpi.h"
#include "esp_log.h"

#include "usb_tmc_cb.h"
#include "usb_tmc_fsm_process.h"

const static char *TAG = "tmc_fsm_task";

static volatile usb_tmc_state_t current_state = STATE_IDLE;
static volatile usb_tmc_status_t usb_tmc_status_reg = STB_NONE;

// --- RECURSOS DE FREERTOS ---
StreamBufferHandle_t rx_stream;
TaskHandle_t scpi_task_handle;

// Buffers para pasar datos en los eventos (o transmisión directa)
static char tx_buffer[TX_BUFFER_LENGTH];
static size_t tx_length = 0;

usb_tmc_status_t usb_tmc_get_stb(void)
{
    return usb_tmc_status_reg;
}

void usb_tmc_fsm_process(usb_tmc_event_t event, void *data, size_t len)
{
    switch (current_state)
    {

    case STATE_IDLE:
        if (event == EV_RX_START)
        {
            // Acción: Limpiar stream buffer preparándose para el mensaje
            //            xStreamBufferReset(rx_stream);
            current_state = STATE_RECEIVING;
        }
        else if (event == EV_TX_REQ)
        {
            // ACCIÓN MEALY (Norma -420): Piden leer pero no hay nada.
            ESP_LOGW(TAG, "Error -420: Query Unterminated");
            // SCPI_ErrorPush(&scpi_context, SCPI_ERROR_QUERY_UNTERMINATED);
            tud_usbtmc_transmit_dev_msg_data(NULL, 0, true, false); // NAK/Empty
        }
        else if (event == EV_SCPI_REPLY)
        {
            usb_tmc_status_reg |= STB_MAV_BIT; // ¡MAV se enciende!
            current_state = STATE_REPLY_READY;
        }
        break;

    case STATE_RECEIVING:
        if (event == EV_RX_CHUNK)
        {
            // Acción: Guardar fragmento sin bloquear
            //            xStreamBufferSend(rx_stream, data, len, 0);
        }
        else if (event == EV_RX_END)
        {
            // Acción: Guardar último fragmento y despertar a la tarea SCPI
            if (len > 0)
                //                xStreamBufferSend(rx_stream, data, len, 0);
                //            xTaskNotifyGive(scpi_task_handle);
                // Si hubo un error SCPI (ej. Query Interrupted o simplemente un error previo),
                // aquí se puede setear el ESB bit.
                // usb_tmc_status_reg |= STB_ESB_BIT;
                current_state = STATE_IDLE; // Vuelve a IDLE a esperar que SCPI procese
        }
        else if (event == EV_SCPI_REPLY)
        {
            current_state = STATE_REPLY_READY;
        }
        break;

    case STATE_REPLY_READY:
        if (event == EV_RX_START)
        {
            // ACCIÓN MEALY (Norma -410): Nuevo comando antes de leer la respuesta anterior.
            ESP_LOGW(TAG, "Error -410: Query Interrupted");
            // SCPI_ErrorPush(&scpi_context, SCPI_ERROR_QUERY_INTERRUPTED);

            tx_length = 0; // Descartamos la respuesta no leída
                           //            xStreamBufferReset(rx_stream);
            current_state = STATE_RECEIVING;
        }
        else if (event == EV_TX_REQ)
        {
            // Acción: El host pide su respuesta, se la despachamos.
            tud_usbtmc_transmit_dev_msg_data(tx_buffer, tx_length, true, false);
            // Mantenemos el estado hasta que se complete el envío
        }
        else if (event == EV_TX_DONE)
        {
            // Acción: Limpiar y volver a escuchar
            tx_length = 0;
            usb_tmc_status_reg &= ~STB_MAV_BIT; // ¡MAV se apaga!
            current_state = STATE_IDLE;
            current_state = STATE_IDLE;
            tud_usbtmc_start_bus_read();
        }
        break;

    default:
        ESP_LOGI(TAG, "Estado usb tmb desconocido...");
    }
}