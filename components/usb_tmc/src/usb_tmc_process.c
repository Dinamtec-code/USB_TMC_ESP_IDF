#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "tusb.h"
#include "class/usbtmc/usbtmc_device.h"
#include "scpi/scpi.h"
#include "scpi_iface_drv.h"
#include "esp_log.h"

#include "usb_tmc_cb.h"
#include "usb_tmc_process.h"

#define TX_STREAM_BUFFER_SIZE 1024

const static char *TAG = "tmc_fsm_task";

/************************************************************************
 * Driver
 *
 ************************************************************************/
static volatile usb_tmc_state_t current_state = STATE_TMC_IDLE;
static volatile usb_tmc_status_t usb_tmc_status_reg = STB_NONE;

volatile bool abort_current_tx = false; // Bandera compartida

static uint8_t tx_buffer[TX_STREAM_BUFFER_SIZE];
static scpi_status_t current_scpi_status = STATUS_SCPI_NONE;

StreamBufferHandle_t rx_stream = NULL;
StreamBufferHandle_t tx_stream = NULL;
static QueueHandle_t drv_error_q = NULL;

static size_t tx_length = 0;
const size_t xTriggerLevel = 1;

static iface_status_t usb_tmc_get_status(void)
{
    return SCPI_IFACE_NONE;
}

static bool usb_tmc_set_rx_stream(StreamBufferHandle_t stream)
{
    if (stream == NULL)
    {
        return false;
    }
    rx_stream = stream;
    return true;
}

static bool usb_tmc_set_tx_stream(StreamBufferHandle_t stream)
{
    if (stream == NULL)
    {
        return false;
    }
    tx_stream = stream;
    return true;
}

static bool usb_tmc_set_error_event_queue(QueueHandle_t error_queue)
{
    if (error_queue == NULL)
    {
        return false;
    }

    drv_error_q = error_queue;
    return true;
}

static bool usb_tmc_peripheric_init(StreamBufferHandle_t rx_stm, StreamBufferHandle_t tx_stm, QueueHandle_t error_q)
{
    if (rx_stm == NULL || tx_stm == NULL || error_q == NULL)
    {
        return false;
    }
    tx_stream = tx_stm;
    rx_stream = rx_stm;
    drv_error_q = error_q;
    return true;
}

static bool usb_tmc_inform_event(iface_event_t event)
{
    switch (event)
    {
    case EV_SCPI_MSG_DONE:
        break;
    case EV_SCPI_PROCESS_DONE:
        usb_tmc_fsm_process(EV_TMC_SCPI_DONE, NULL, 0);
        break;
    default:
        return false;
    }
    return true;
}

static bool usb_tmc_get_abort_tx()
{
    return abort_current_tx;
}

// Función interna del driver para publicar errores hacia la App
void driver_publish_error(iface_event_t error_event)
{
    if (drv_error_q != NULL)
    {
        xQueueSend(drv_error_q, &error_event, 0);
    }
}

static iface_struct_t usb_tmc_iface = {
    .context = NULL,
    /* initialized method */
    .set_rx_stream = usb_tmc_set_rx_stream,                  // stream para enviarle los datos recibidos al parser SCPI
    .set_tx_stream = usb_tmc_set_tx_stream,                  // stream para recibir los datos que el parser SCPI genera como respuesta
    .set_events_error_queue = usb_tmc_set_error_event_queue, // queue para publicar errores hacia la el motor SCPI
    .peripheric_init = usb_tmc_peripheric_init,              // inicialización del hardware
    /* scpi service method */
    .inform_events = usb_tmc_inform_event};

iface_struct_t *usb_tmc_get_iface(void)
{
    return &usb_tmc_iface;
}

usb_tmc_status_t usb_tmc_get_stb(void)
{
    return usb_tmc_status_reg;
}

void usb_tmc_set_scpi_status(scpi_status_t status)
{
    current_scpi_status = status;
}

static inline void clear_tx_buffer()
{
    memset(tx_buffer, 0, TX_STREAM_BUFFER_SIZE);
    tx_length = 0;
}

static inline void abort_tx(void)
{
    // Limpiamos los buffers de FreeRTOS de forma atómica para la tarea
    if (tx_stream != NULL)
        xStreamBufferReset(tx_stream);
    if (rx_stream != NULL)
        xStreamBufferReset(rx_stream);

    // Limpiamos la memoria local
    clear_tx_buffer();
}

void usb_tmc_fsm_process(usb_tmc_event_t event, void *data, size_t len)
{
    external_fsm_update();
    switch (current_state)
    {
    case STATE_TMC_IDLE:
        if (event == EV_TMC_RX_START)
        {
            current_state = STATE_TMC_RECEIVING;
        }
        else if (event == EV_TMC_TX_REQ)
        {
            driver_publish_error(EV_SCPI_ERROR_420); // Publicamos el error -420
            ESP_LOGW(TAG, "Error -420: Query Unterminated");
            clear_tx_buffer();
            tud_usbtmc_transmit_dev_msg_data(NULL, 0, true, false); // NAK/Empty
        }
        break;
    case STATE_TMC_RECEIVING:
        if (event == EV_TMC_RX_CHUNK)
        {
            // Acción: Guardar fragmento sin bloquear
            xStreamBufferSend(rx_stream, data, len, 0);
        }
        else if (event == EV_TMC_RX_END)
        {
            // Acción: Guardar último fragmento y despertar a la tarea SCPI
            xStreamBufferSend(rx_stream, data, len, 0);
            // aquí se puede setear el ESB bit.
            // usb_tmc_status_reg |= STB_ESB_BIT;

            ESP_LOGI(TAG, "Recepción Completa");
            current_state = STATE_TMC_PROCESSING;
        }
        break;
    case STATE_TMC_PROCESSING:
        if (event == EV_TMC_SCPI_DONE)
        {
            tx_length = xStreamBufferBytesAvailable(tx_stream);

            if (tx_length > 0)
            {
                current_state = STATE_TMC_REPLY_READY; // Corregido
                // Avisamos al registro 488.2 que hay un mensaje disponible
                usb_tmc_status_reg |= STB_MAV_BIT;
            }
            else
            {
                current_state = STATE_TMC_IDLE;
            }
        }
        else if (event == EV_TMC_RX_START)
        {
            driver_publish_error(EV_SCPI_ERROR_410);
            ESP_LOGW(TAG, "Error -410: Query Interrupted (Durante procesado)");
            abort_tx();
            current_state = STATE_TMC_RECEIVING;
        }
        break;
    case STATE_TMC_REPLY_READY:
        if (event == EV_TMC_TX_REQ)
        {
            tx_length = xStreamBufferReceive(tx_stream, (void *)tx_buffer, TX_STREAM_BUFFER_SIZE, 0);
            tx_length = tu_min32(tx_length, len); // Respetamos lo que el Host nos pidió leer

            // Verificamos si este es el ÚLTIMO fragmento del mensaje
            bool eof = (xStreamBufferBytesAvailable(tx_stream) == 0);

            // Transmitimos. Si eof == true, TinyUSB asertará el bit EOM en el header
            tud_usbtmc_transmit_dev_msg_data((const void *)tx_buffer, tx_length, eof, false);
        }
        else if (event == EV_TMC_TX_DONE)
        {
            // TX_DONE se dispara cada vez que un paquete IN llega al host exitosamente.
            // Solo debemos salir de este estado si ya no queda nada por enviar.
            if (xStreamBufferBytesAvailable(tx_stream) == 0)
            {
                clear_tx_buffer();
                usb_tmc_status_reg &= ~STB_MAV_BIT; // ¡MAV se apaga al terminar!
                current_state = STATE_TMC_IDLE;

                // Le decimos a TinyUSB que estamos listos para recibir comandos nuevos
                tud_usbtmc_start_bus_read();
            }
            // Si quedan datos, nos quedamos en REPLY_READY. El host hará
            // un nuevo EV_TMC_TX_REQ para pedir la siguiente parte.
        }
        else if (event == EV_TMC_RX_START)
        {
            driver_publish_error(EV_SCPI_ERROR_410);
            ESP_LOGW(TAG, "Error -410: Query Interrupted (Respuesta sin leer)");
            abort_tx();
            current_state = STATE_TMC_RECEIVING;
        }
        break;

    default:
        ESP_LOGI(TAG, "Estado usb tmb desconocido...");
    }
}
