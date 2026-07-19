#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "tusb.h"
#include "class/usbtmc/usbtmc_device.h"
#include "scpi/scpi.h"
#include "esp_log.h"

#include "usb_tmc_cb.h"
#include "usb_tmc_process.h"

const static char *TAG = "tmc_fsm_task";

/************************************************************************
 * Simular la interfaz.
 * Luego esto pasara a una tarea de procesado de comandos
 ************************************************************************/
#define RX_STREAM_BUFFER_SIZE 1024
#define TX_STREAM_BUFFER_SIZE 512

const size_t xTriggerLevel = 1;
StaticStreamBuffer_t rxStreamBufferStruct;
StaticStreamBuffer_t txStreamBufferStruct;

StreamBufferHandle_t rx_stream;
StreamBufferHandle_t tx_stream;

TaskHandle_t scpi_task_handle;
static uint8_t rx_buff[RX_STREAM_BUFFER_SIZE + 1];
static uint8_t tx_buff[TX_STREAM_BUFFER_SIZE + 1];

static uint8_t rx_buff_scpi[RX_STREAM_BUFFER_SIZE];
static uint8_t tx_buff_scpi[TX_STREAM_BUFFER_SIZE];

static size_t tx_length = 0;

static char str_idn[] = {"ESP32-TMC-V1.0\n"};
void generate_response(const char *msg, size_t len, StreamBufferHandle_t rx_stream_buff)
{
    size_t rx_data_lenght = xStreamBufferReceive(rx_stream_buff, (void *)rx_buff_scpi, len, 0);
    if (rx_data_lenght > 0)
    {
        if (!strncmp((char *)rx_buff_scpi, "*idn?", 5) || !strncmp((char *)rx_buff_scpi, "*IDN?", 5))
        {
            tx_length = strlen(str_idn);
            xStreamBufferSend(tx_stream, (const void *)str_idn, tx_length, 0);
            usb_tmc_set_scpi_status(STATUS_REPLY_READY);
            //          usb_tmc_fsm_process(EV_SCPI_REPLY, NULL, 0);
        }
    }
    /*     if (len > 0)
        {
            if (!strncmp((char *)msg, "*idn?", 5) || !strncmp((char *)msg, "*IDN?", 5))
            {
                tx_length = strlen(str_idn);
                memcpy(tx_buffer, str_idn, strlen(str_idn));
                usb_tmc_fsm_process(EV_SCPI_REPLY, (void *)(tx_buffer), tx_length);
            }
            else
            {
                clear_buffers();
            }
        } */
}

/************************************************************************
 *
 * Driver
 *
 ************************************************************************/

static volatile usb_tmc_state_t current_state = STATE_IDLE;
static volatile usb_tmc_status_t usb_tmc_status_reg = STB_NONE;

static uint8_t tx_buffer[TX_STREAM_BUFFER_SIZE];
static scpi_status_t current_scpi_status = STATUS_NONE;
usb_tmc_status_t usb_tmc_get_stb(void)
{
    return usb_tmc_status_reg;
}

void driver_register(StreamBufferHandle_t *stream)
{
    rx_stream = xStreamBufferCreateStatic(RX_STREAM_BUFFER_SIZE,
                                          xTriggerLevel,
                                          rx_buff,
                                          &rxStreamBufferStruct);
    tx_stream = xStreamBufferCreateStatic(TX_STREAM_BUFFER_SIZE,
                                          xTriggerLevel,
                                          tx_buff,
                                          &txStreamBufferStruct);
}

void usb_tmc_set_scpi_status(scpi_status_t status)
{
    current_scpi_status = status;
}

static void clear_buffers()
{
    memset(tx_buffer, 0, TX_STREAM_BUFFER_SIZE);
}

void usb_tmc_fsm_process(usb_tmc_event_t event, void *data, size_t len)
{
    if (current_state == STATE_RECEIVING)
    {
        if (current_scpi_status == STATUS_REPLY_READY)
        {
            usb_tmc_status_reg |= STB_MAV_BIT; // ¡MAV se enciende!
            current_state = STATE_REPLY_READY;
            current_scpi_status = STATUS_NONE;
        }
        else if (current_scpi_status == STATUS_REPLY_DONE)
        {
            current_state = STATE_IDLE;
            current_scpi_status = STATUS_NONE;
        }
    }
    switch (current_state)
    {
    case STATE_IDLE:
        if (event == EV_RX_START)
        {
            // Si se está esperando solo se inici la recepción, pero cuidado de no volver a idle si se envio un comando al parse.
            // El parser es el que vuelve a idle.
            current_state = STATE_RECEIVING;
        }
        else if (event == EV_TX_REQ)
        {
            // tud_usbtmc_transmit_dev_msg_data((void *)tx_buffer, tx_length, true, false);
            //  ACCIÓN MEALY (Norma -420): Piden leer pero no hay nada.
            ESP_LOGW(TAG, "Error -420: Query Unterminated");
            // SCPI_ErrorPush(&scpi_context, SCPI_ERROR_QUERY_UNTERMINATED);
            tud_usbtmc_transmit_dev_msg_data(NULL, 0, true, false); // NAK/Empty
        }
        /* El siguiente evento debe prodicir un error
        else if (event == EV_SCPI_REPLY)
        {
            usb_tmc_status_reg |= STB_MAV_BIT; // ¡MAV se enciende!
            current_state = STATE_REPLY_READY;
        } */
        break;
    case STATE_RECEIVING:
        if (current_scpi_status == STATUS_NONE)
        {
            if (event == EV_RX_CHUNK)
            {
                // Acción: Guardar fragmento sin bloquear
                xStreamBufferSend(rx_stream, data, len, 0);
            }
            else if (event == EV_RX_END)
            {
                // Acción: Guardar último fragmento y despertar a la tarea SCPI
                xStreamBufferSend(rx_stream, data, len, 0);
                // xTaskNotifyGive(scpi_task_handle);
                generate_response(data, len, rx_stream);
                // Si hubo un error SCPI (ej. Query Interrupted o simplemente un error previo),
                // aquí se puede setear el ESB bit.
                // usb_tmc_status_reg |= STB_ESB_BIT;

                ESP_LOGI(TAG, "Recepción Completa");
            }
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
            size_t txlen = xStreamBufferReceive(tx_stream, (void *)tx_buffer, TX_STREAM_BUFFER_SIZE, 0);
            txlen = tu_min32(tx_length, len);
            tud_usbtmc_transmit_dev_msg_data((const void *)tx_buffer, txlen, true, false);
        }
        else if (event == EV_TX_DONE)
        {
            clear_buffers();
            usb_tmc_status_reg &= ~STB_MAV_BIT; // ¡MAV se apaga!
            current_state = STATE_IDLE;
            tud_usbtmc_start_bus_read();
        }
        break;

    default:
        ESP_LOGI(TAG, "Estado usb tmb desconocido...");
    }
}
