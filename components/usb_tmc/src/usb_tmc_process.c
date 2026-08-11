#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "tusb.h"
#include "class/usbtmc/usbtmc_device.h"
#include "tinyusb.h"

#include "scpi/scpi.h"
#include "scpi_iface_drv.h"

#include "usb_tmc_cb.h"
#include "usb_tmc_process.h"

const static char *TAG = "tmc_fsm_task";
#define TX_BUFFER_SIZE 256
/************************************************************************
 * Driver
 *
 ************************************************************************/
static drv_context_t usb_drv_ctx = {0};

static volatile usb_tmc_state_t tmc_state = STATE_TMC_IDLE;
static volatile usb_tmc_status_t usb_tmc_status_reg = STB_NONE;

static size_t tx_length = 0;
iface_msg_handle_t actual_msg = NULL;
iface_msg_handle_t last_msg = NULL;

uint8_t tx_buffer[TX_BUFFER_SIZE] = {0};

/**
 * @brief Se inicia el driver .
 * @return iface_struct_t la estructura estatica con los datos y callbacks para la comunicacion driver app
 */
static bool usb_tmc_init(StreamBufferHandle_t tx_stream, QueueHandle_t rx_msg_queue)
{
    if (tx_stream == NULL || rx_msg_queue == NULL)
    {
        return false;
    }
    usb_drv_ctx.tx_stream = tx_stream;
    usb_drv_ctx.rx_msg_queue = rx_msg_queue;
    // TODO: inicializar hardware si es necesario
    //tmc_hal_init();
    usb_drv_ctx.running = true;

    return true;
}

static void usb_tmc_deinit()
{
    usb_drv_ctx.tx_stream = NULL;
    usb_drv_ctx.rx_msg_queue = NULL;
    // TODO: desactivar el hardware necesario
    usb_drv_ctx.running = false;
}

static bool usb_tmc_inform_event(iface_event_t event)
{
    switch (event)
    {
    case EV_SCPI_MSG_DONE: // al agregar la cola de mensajes con estados, el driver puede seguir recibiendo aun cuando el parces no pcocese el mensaje anterior y este estado ya no se controla.
        break;
    case EV_SCPI_PROCESS_DONE:
        usb_tmc_fsm_process(EV_TMC_SCPI_DONE, NULL, 0);
        break;
    default:
        return false;
    }
    return true;
}

static iface_struct_t usb_tmc_iface = {
    .context = {0},
    .id = DRV_IFACE_USBTMC,
    .name = "USB-TMC",
    /* initialized method */
    .init = usb_tmc_init, // inicialización del hardware
    .deinit = usb_tmc_deinit,
    /* scpi service method (implementados en la definicion de la interfaz) */
    //.get_free_slot,// como el slot es un mensaje se pueden usar sus metodos para editarlo sin pasar por la interfaz
    //.get_next_slot,
    //.send_msg,
    .inform_parser_events = usb_tmc_inform_event}; // la app le avisa al driver que termino de procesar los datos

/**
 * @brief Envial al a la app la estructura del driver .
 * @return iface_handler_t puntero a la estructura estatica con los datos y callbacks para la comunicacion driver app
 */
iface_handler_t usb_tmc_get_iface(void)
{
    return (iface_handler_t)(&usb_tmc_iface);
}

usb_tmc_status_t usb_tmc_get_stb(void)
{
    return usb_tmc_status_reg;
}

static inline void clear_tx_buffer()
{
    memset(tx_buffer, 0, TX_BUFFER_SIZE);
    tx_length = 0;
}

static inline void abort_tx(void)
{
    // Limpiamos los buffers de FreeRTOS de forma atómica para la tarea
    if (usb_drv_ctx.tx_stream != NULL)
        xStreamBufferReset(usb_drv_ctx.tx_stream);
    // Limpiamos la memoria local
    clear_tx_buffer();
}

void start_new_reception()
{
    actual_msg = usb_tmc_iface.get_free_slot();
    if (actual_msg != NULL)
    {
        iface_msg_set_state(actual_msg, IFACE_MSG_RX_ACTIVE);
        ESP_LOGW(TAG, "New massage");
        tmc_state = STATE_TMC_RECEIVING;
    }
}

// Funciones de conveniencia para la máquina de estados
static inline void iface_msg_mark_ready(iface_msg_handle_t msg)
{
    iface_msg_set_state(msg, IFACE_MSG_READY);
    xQueueSend(usb_drv_ctx.rx_msg_queue, (void *)&msg, (TickType_t)0);
}

static inline void iface_msg_mark_free(iface_msg_handle_t msg)
{
    iface_msg_set_state(msg, IFACE_MSG_FREE);
}

void usb_tmc_fsm_process(usb_tmc_event_t event, void *data, size_t len)
{
    switch (tmc_state)
    {
    case STATE_TMC_IDLE:
        if (event == EV_TMC_RX_START)
        {
            start_new_reception();
        }
        else if (event == EV_TMC_TX_REQ)
        {
            actual_msg = usb_tmc_iface.get_free_slot();
            iface_msg_set_error_flag(actual_msg, MSG_ERR_UNTERMIN); // Publicamos el error -420
            xQueueSend(usb_drv_ctx.rx_msg_queue, (void *)&actual_msg, (TickType_t)0);

            ESP_LOGW(TAG, "Error -420: Query Unterminated");
            clear_tx_buffer();
            tud_usbtmc_transmit_dev_msg_data(NULL, 0, true, false); // NAK/Empty
        }
        break;
    case STATE_TMC_RECEIVING:
        iface_msg_write(actual_msg, data, len);
        if (event == EV_TMC_RX_END)
        {
            // Guardamos el slot que se enviara para ser procesado
            last_msg = actual_msg;
            // informamos que el mensaje esta listo para ser procesado
            iface_msg_mark_ready(actual_msg);

            // aquí se puede setear el ESB bit.
            // usb_tmc_status_reg |= STB_ESB_BIT;

            ESP_LOGI(TAG, "Recepción Completa");
            tmc_state = STATE_TMC_PROCESSING;

            // Le decimos a TinyUSB que estamos listos para recibir comandos nuevos
            tud_usbtmc_start_bus_read();
        }
        else if (event == EV_TMC_RX_CHUNK)
        {
            // Acción: Guardar fragmento sin bloquear
        }
        break;
    case STATE_TMC_PROCESSING:
        if (event == EV_TMC_SCPI_DONE)
        {
            tx_length = xStreamBufferBytesAvailable(usb_drv_ctx.tx_stream);

            if (tx_length > 0)
            {
                // Avisamos al registro 488.2 que hay un mensaje disponible
                usb_tmc_status_reg |= STB_MAV_BIT;
                tmc_state = STATE_TMC_REPLY_READY;
            }
            else
            {
                iface_msg_mark_free(last_msg);
                tmc_state = STATE_TMC_IDLE;
            }
        }
        else if (event == EV_TMC_RX_START)
        {
            iface_msg_set_error_flag(last_msg, MSG_ERR_INTERRUPT);
            ESP_LOGW(TAG, "Error -410: Query Interrupted (Durante procesado)");
            abort_tx();
            start_new_reception();
        }
        break;
    case STATE_TMC_REPLY_READY:
        if (event == EV_TMC_TX_REQ)
        {
            tx_length = xStreamBufferReceive(usb_drv_ctx.tx_stream, (void *)tx_buffer, TX_BUFFER_SIZE, 0);
            tx_length = tu_min32(tx_length, len); // Respetamos lo que el Host nos pidió leer

            // Verificamos si este es el ÚLTIMO fragmento del mensaje
            bool eof = (xStreamBufferBytesAvailable(usb_drv_ctx.tx_stream) == 0);

            // Transmitimos. Si eof == true, TinyUSB asertará el bit EOM en el header
            tud_usbtmc_transmit_dev_msg_data((const void *)tx_buffer, tx_length, eof, false);
        }
        else if (event == EV_TMC_TX_DONE)
        {
            if (xStreamBufferBytesAvailable(usb_drv_ctx.tx_stream) == 0)
            {
                clear_tx_buffer();
                usb_tmc_status_reg &= ~STB_MAV_BIT; // ¡MAV se apaga al terminar!

                iface_msg_mark_free(last_msg);
                tmc_state = STATE_TMC_IDLE;
            }
        }
        else if (event == EV_TMC_RX_START)
        {
            iface_msg_set_error_flag(last_msg, MSG_ERR_INTERRUPT);
            ESP_LOGW(TAG, "Error -410: Query Interrupted (Durante procesado)");
            abort_tx();
            start_new_reception();
        }
        break;

    default:
        ESP_LOGI(TAG, "Estado usb tmb desconocido...");
    }
}
