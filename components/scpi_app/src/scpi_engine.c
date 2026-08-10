
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"

#include <stdlib.h>
#include <stdint.h>

#include "scpi/scpi.h"
#include "scpi_engine.h"
// #include "usb_tmc_process.h"
#include "scpi_iface_drv.h"

static scpi_t scpi_context;

/*******************************************************************************
 * Buffers de comunicación
 *
 ******************************************************************************/
#define RX_STREAM_BUFFER_SIZE 1024
#define TX_STREAM_BUFFER_SIZE 512

static StaticStreamBuffer_t rxStreamBufferStruct;
static StaticStreamBuffer_t txStreamBufferStruct;
static uint8_t rx_stream_memory[RX_STREAM_BUFFER_SIZE + 1];
static uint8_t tx_stream_memory[TX_STREAM_BUFFER_SIZE + 1];

/* steam buffers */
static StreamBufferHandle_t rx_stream_handle;
static StreamBufferHandle_t tx_stream_handle;

static const size_t xTrigger = 1;

/*******************************************************************************
 * Queues de errores de driver
 *
 ******************************************************************************/
#define ITEM_SIZE sizeof(iface_event_t)
#define ERROR_QUEUE_SIZE 10

static QueueHandle_t error_events_queue_handle;

/* Memoria estática para las colas */
static StaticQueue_t error_events_QueueStruct;
static uint8_t error_events_queue_memory[ERROR_QUEUE_SIZE * ITEM_SIZE];

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

bool driver_event_queues_register(void)
{
    error_events_queue_handle = xQueueCreateStatic(ERROR_QUEUE_SIZE,
                                                   ITEM_SIZE,
                                                   error_events_queue_memory,
                                                   &error_events_QueueStruct);

    return (error_events_queue_handle != NULL);
}

bool driver_iface_init(iface_handle_t iface)
{
    if (!iface)
        return false;

    // 1. Configurar RX si no está listo
    if (!(iface->status & SCPI_IFACE_RX_READY))
    {
        if (iface->set_rx_stream && iface->set_rx_stream(rx_stream_handle))
        {
            iface->status |= SCPI_IFACE_RX_READY;
        }
        else
        {
            iface->status |= SCPI_IFACE_ERROR;
            return false;
        }
    }

    // 2. Configurar TX si no está listo
    if (!(iface->status & SCPI_IFACE_TX_READY))
    {
        if (iface->set_tx_stream && iface->set_tx_stream(tx_stream_handle))
        {
            iface->status |= SCPI_IFACE_TX_READY;
        }
        else
        {
            iface->status |= SCPI_IFACE_ERROR;
            return false;
        }
    }

    // 3. Inicializar Hardware si no está listo
    if (!(iface->status & SCPI_IFACE_HW_READY))
    {
        if (iface->peripheric_init && iface->peripheric_init(rx_stream_handle, tx_stream_handle, error_events_queue_handle))
        {
            iface->status |= SCPI_IFACE_HW_READY;
        }
        else
        {
            // No marcamos ERROR inmediatamente si el HW puede aparecer luego (hot-plug)
            // O marcamos ERROR si es crítico.
            return false;
        }
    }

    return (iface->status == (SCPI_IFACE_RX_READY | SCPI_IFACE_TX_READY | SCPI_IFACE_HW_READY));
}

StreamBufferHandle_t get_rx_stream(void)
{
    return rx_stream_handle;
}

StreamBufferHandle_t get_tx_stream(void)
{
    return tx_stream_handle;
}

iface_handle_t *scpi_comm_iface[3] = {NULL, NULL, NULL};

void iface_init(void)
{
    scpi_iface[0] = usb_tmc_get_iface();
}

// Callback que llama libscpi cuando genera un string de respuesta
size_t SCPI_Write(void *context, const char *data, size_t len)
{
    if (len < TX_STREAM_BUFFER_SIZE)
    {
        memcpy(tx_buffer, data, len);
        // Inyectamos el evento a la máquina de estados
        // avisar que ya hay datos
        xStreamBufferSend(tx_stream_handle, data, len, 0);
    }
    return len;
}

void set_event(scpi_status_t status)
{
}

static char local_buf[256];
void scpi_engine_task(void *pvParameters)
{

    if (rx_stream_handle == NULL)
    {
        ESP_LOGI("SCPI", "Buffer no iniciado");
    }
    else
    {
        ESP_LOGI("SCPI", "Buffer iniciado correctamente");
    }

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
                /* la revision del último byte no es parter del la interfaz y esta tarea no tiene por que preocuparse por eso. La interfaz debe garantizar que todo lo que el usiario envia llegue al parser tal cual lo envió. La interfaz solo puede garantizar que el último byte que llega al parser es el último byte que envió el usuario. Si el usuario no envía '\n', el parser no lo recibirá y no podrá procesar la cadena y en todo caso, si el parser nunca informa de un mensaje/comando, puede avisar de un timeout y publicando el error correspondiente si el estandar SCPI 1999 lo especifica y si no seguirá esperando.
                                // Aquí podrías revisar si el último byte es '\n', si no lo es, agregarlo.
                                if (local_buf[bytes_read - 1] != '\n' && bytes_read < sizeof(local_buf))
                                {
                                    local_buf[bytes_read] = '\n';
                                    bytes_read++;
                                }
                */
                // Inyectamos a libscpi (esta función demora lo que tenga que demorar)
                SCPI_Input(&scpi_context, local_buf, bytes_read);
            }
        } while (bytes_read > 0);

        // // Si la respuesta NO fue un Query, la FSM quedó en IDLE. Le decimos al bus que lea.
        // if (current_state == STATE_IDLE)
        // {
        //     //            tud_usbtmc_start_bus_read();
        // }
    }
}

/* ==========================================================================
 * SCPI
 * ========================================================================== */
float kp[4] = {1.0f, 2.0f, 3.0f, 4.0f};
float ki[4] = {5.0f, 6.0f, 7.0f, 8.0f};
float kd[4] = {9.0f, 10.0f, 11.0f, 12.0f};

float temp[2] = {25.0f, 26.0f};
float set_point[2] = {35.0f, 36.0f};

float duty[2] = {0.5f, 0.5f};

bool output_status[2] = {0, 0};

/* ==========================================================================
 * Contexto global de libscpi y buffers asociados
 * ========================================================================== */

#define SCPI_INPUT_BUFFER_LENGTH 256
static char scpi_input_buffer[SCPI_INPUT_BUFFER_LENGTH];

#define SCPI_ERROR_QUEUE_SIZE 10
static scpi_error_t scpi_error_queue[SCPI_ERROR_QUEUE_SIZE];

static scpi_result_t my_CoreRst(scpi_t *context);

/* ==========================================================================
 * Funciones auxiliares de los comandos del instrumento
 * ========================================================================== */

static float get_temperature(void)
{
    return temp[0];
}

static float get_setpoint(uint8_t channel)
{
    return set_point[channel - 1];
}

static void set_setpoint(uint8_t channel, float val)
{
    set_point[channel - 1] = val;
}

static float get_kp(void)
{
    return kp[0];
}

static void set_kp(float val)
{
    kp[0] = val;
}

static float get_ki(void)
{
    return ki[0];
}

static void set_ki(float val)
{
    ki[0] = val;
}

static float get_kd(void)
{
    return kd[0];
}

static void set_kd(float val)
{
    kd[0] = val;
}

static float get_duty(void)
{
    return duty[0];
}

static bool get_output(uint8_t channel)
{
    return output_status[channel - 1];
}

static void set_output(uint8_t channel, bool on)
{
    output_status[channel - 1] = on;
}

/* ==========================================================================
 * Callbacks de comandos SCPI
 * ========================================================================== */

static scpi_result_t cmd_meas_temp(scpi_t *context)
{

    SCPI_ResultFloat(context, get_temperature());
    return SCPI_RES_OK;
}

static scpi_result_t cmd_temp_sp_q(scpi_t *context)
{
    int32_t numbers[1]; // Arreglo para capturar los sufijos del comando

    // 1. Obtener el número de canal del nodo raíz (ej. SOURce1 -> 1)
    // El último parámetro es el valor por defecto si el usuario envía "SOUR:TEMP..." sin número.
    SCPI_CommandNumbers(context, numbers, 1, 1);
    int32_t channel = numbers[0];

    // 2. Validar que el canal exista en tu hardware
    if (channel < 1 || channel > 2)
    {
        SCPI_ErrorPush(context, SCPI_ERROR_INVALID_SUFFIX);
        return SCPI_RES_ERR;
    }

    SCPI_ResultFloat(context, get_setpoint(channel));
    return SCPI_RES_OK;
}

scpi_result_t cmd_temp_sp(scpi_t *context)
{
    int32_t numbers[1]; // Arreglo para capturar los sufijos del comando
    float setpoint;

    // 1. Obtener el número de canal del nodo raíz (ej. SOURce1 -> 1)
    // El último parámetro es el valor por defecto si el usuario envía "SOUR:TEMP..." sin número.
    SCPI_CommandNumbers(context, numbers, 1, 1);
    int32_t channel = numbers[0];

    // 2. Validar que el canal exista en tu hardware
    if (channel < 1 || channel > 2)
    {
        SCPI_ErrorPush(context, SCPI_ERROR_INVALID_SUFFIX);
        return SCPI_RES_ERR;
    }

    // 3. Extraer el parámetro enviado por el usuario
    if (!SCPI_ParamFloat(context, &setpoint, TRUE))
    {
        return SCPI_RES_ERR;
    }

    set_setpoint(channel, setpoint);

    return SCPI_RES_OK;
}

static scpi_result_t cmd_pid_kp_q(scpi_t *context)
{
    SCPI_ResultFloat(context, get_kp());
    return SCPI_RES_OK;
}

static scpi_result_t cmd_pid_kp(scpi_t *context)
{
    float val;
    if (SCPI_ParamFloat(context, &val, true))
    {
        set_kp(val);
        return SCPI_RES_OK;
    }
    return SCPI_RES_ERR;
}

static scpi_result_t cmd_pid_ki_q(scpi_t *context)
{
    SCPI_ResultFloat(context, get_ki());
    return SCPI_RES_OK;
}

static scpi_result_t cmd_pid_ki(scpi_t *context)
{
    float val;
    if (SCPI_ParamFloat(context, &val, true))
    {
        set_ki(val);
        return SCPI_RES_OK;
    }
    return SCPI_RES_ERR;
}

static scpi_result_t cmd_pid_kd_q(scpi_t *context)
{
    SCPI_ResultFloat(context, get_kd());
    return SCPI_RES_OK;
}

static scpi_result_t cmd_pid_kd(scpi_t *context)
{
    float val;
    if (SCPI_ParamFloat(context, &val, true))
    {
        set_kd(val);
        return SCPI_RES_OK;
    }
    return SCPI_RES_ERR;
}

static scpi_result_t cmd_pid_duty_q(scpi_t *context)
{
    SCPI_ResultFloat(context, get_duty());
    return SCPI_RES_OK;
}

static scpi_result_t cmd_sour_outp(scpi_t *context)
{
    uint64_t channel;
    scpi_bool_t on;
    if (!SCPI_ParamUInt64(context, &channel, true))
        return SCPI_RES_ERR;
    if (channel > 1)
        return SCPI_RES_ERR;
    if (!SCPI_ParamBool(context, &on, true))
        return SCPI_RES_ERR;
    set_output((uint8_t)channel, on);
    return SCPI_RES_OK;
}

static scpi_result_t cmd_sour_outp_q(scpi_t *context)
{
    uint64_t channel;
    if (!SCPI_ParamUInt64(context, &channel, true))
        return SCPI_RES_ERR;
    if (channel > 1)
        return SCPI_RES_ERR;
    SCPI_ResultBool(context, get_output((uint8_t)channel));
    return SCPI_RES_OK;
}

/**
 * Reimplement IEEE488.2 *TST?
 *
 * Result should be 0 if everything is ok
 * Result should be 1 if something goes wrong
 *
 * Return SCPI_RES_OK
 */
static scpi_result_t My_CoreTstQ(scpi_t *context)
{

    SCPI_ResultInt32(context, 0);

    return SCPI_RES_OK;
}

/* ==========================================================================
 * Árbol de comandos del instrumento
 * ========================================================================== */

static const scpi_command_t scpi_commands[] = {
    /* IEEE Mandated Commands (SCPI std V1999.0 4.1.1) */
    {
        .pattern = "*CLS",
        .callback = SCPI_CoreCls,
    },
    {
        .pattern = "*ESE",
        .callback = SCPI_CoreEse,
    },
    {
        .pattern = "*ESE?",
        .callback = SCPI_CoreEseQ,
    },
    {
        .pattern = "*ESR?",
        .callback = SCPI_CoreEsrQ,
    },
    {
        .pattern = "*IDN?",
        .callback = SCPI_CoreIdnQ,
    },
    {
        .pattern = "*OPC",
        .callback = SCPI_CoreOpc,
    },
    {
        .pattern = "*OPC?",
        .callback = SCPI_CoreOpcQ,
    },
    {
        .pattern = "*RST",
        .callback = my_CoreRst,
        //        .callback = SCPI_CoreRst,
    },
    {
        .pattern = "*SRE",
        .callback = SCPI_CoreSre,
    },
    {
        .pattern = "*SRE?",
        .callback = SCPI_CoreSreQ,
    },
    {
        .pattern = "*STB?",
        .callback = SCPI_CoreStbQ,
    },
    {
        .pattern = "*TST?",
        .callback = My_CoreTstQ,
    },
    {
        .pattern = "*WAI",
        .callback = SCPI_CoreWai,
    },

    /* Required SCPI commands (SCPI std V1999.0 4.2.1) */
    {
        .pattern = "SYSTem:ERRor[:NEXT]?",
        .callback = SCPI_SystemErrorNextQ,
    },
    {
        .pattern = "SYSTem:ERRor:COUNt?",
        .callback = SCPI_SystemErrorCountQ,
    },
    {
        .pattern = "SYSTem:VERSion?",
        .callback = SCPI_SystemVersionQ,
    },
    /* {.pattern = "STATus:OPERation?", .callback = scpi_stub_callback,}, */
    /* {.pattern = "STATus:OPERation:EVENt?", .callback = scpi_stub_callback,}, */
    /* {.pattern = "STATus:OPERation:CONDition?", .callback = scpi_stub_callback,}, */
    /* {.pattern = "STATus:OPERation:ENABle", .callback = scpi_stub_callback,}, */
    /* {.pattern = "STATus:OPERation:ENABle?", .callback = scpi_stub_callback,}, */
    {
        .pattern = "STATus:QUEStionable[:EVENt]?",
        .callback = SCPI_StatusQuestionableEventQ,
    },
    /* {.pattern = "STATus:QUEStionable:CONDition?", .callback = scpi_stub_callback,}, */
    {
        .pattern = "STATus:QUEStionable:ENABle",
        .callback = SCPI_StatusQuestionableEnable,
    },
    {
        .pattern = "STATus:QUEStionable:ENABle?",
        .callback = SCPI_StatusQuestionableEnableQ,
    },

    {
        .pattern = "STATus:PRESet",
        .callback = SCPI_StatusPreset,
    },
    /* Medición */
    {.pattern = "MEASure:TEMPerature#?", .callback = cmd_meas_temp, .tag = 0},

    /* Setpoint */
    {.pattern = "SOURce#:TEMPerature:SETPOint?", .callback = cmd_temp_sp_q, .tag = 0},
    {.pattern = "SOURce#:TEMPerature:SETPOint", .callback = cmd_temp_sp, .tag = 0},

    /* PID */
    {.pattern = "SOURce#:CONTrol:PID:KP?", .callback = cmd_pid_kp_q, .tag = 0},
    {.pattern = "SOURce#:CONTrol:PID:KP", .callback = cmd_pid_kp, .tag = 0},
    {.pattern = "SOURce#:CONTrol:PID:KI?", .callback = cmd_pid_ki_q, .tag = 0},
    {.pattern = "SOURce#:CONTrol:PID:KI", .callback = cmd_pid_ki, .tag = 0},
    {.pattern = "SOURce#:CONTrol:PID:KD?", .callback = cmd_pid_kd_q, .tag = 0},
    {.pattern = "SOURce#:CONTrol:PID:KD", .callback = cmd_pid_kd, .tag = 0},
    {.pattern = "SOURce#:CONTrol:PID:OUTPut?", .callback = cmd_pid_duty_q, .tag = 0},

    /* Salidas */
    {.pattern = "SOURce#:OUTPut", .callback = cmd_sour_outp, .tag = 0},
    {.pattern = "SOURce#:OUTPut?", .callback = cmd_sour_outp_q, .tag = 0},

    SCPI_CMD_LIST_END};

/* ==========================================================================
 * Callbacks de la interfaz de libscpi
 * ========================================================================== */

static scpi_result_t my_CoreRst(scpi_t *context)
{
    (void)context;
    SCPI_CoreRst(context); // Reset del parser y registros IEEE 488.2
    set_point[0] = 0.0f;
    set_point[1] = 0.0f;

    kp[0] = 1.0f;
    kp[1] = 2.0f;
    ki[0] = 5.0f;
    ki[1] = 6.0f;
    kd[0] = 9.0f;
    kd[1] = 10.0f;

    temp[0] = 25.0f;
    temp[1] = 26.0f;
    set_point[0] = 35.0f;
    set_point[1] = 36.0f;

    duty[0] = 0.5f;
    duty[1] = 0.5f;

    output_status[0] = 0;
    output_status[1] = 0;
    // config_apply_reset_values(); // Restauración de los parámetros del instrumento
    return SCPI_RES_OK;
}

static size_t scpi_write_cb(scpi_t *ctx, const char *data, size_t len)
{
    (void)ctx;
    xStreamBufferSend(tx_stream_handle, data, len, 0);
    return len;
}

static int scpi_error_cb(scpi_t *ctx, int_fast16_t err)
{
    (void)ctx;
    (void)err;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_reset_cb(scpi_t *ctx)
{
    (void)ctx;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_control_cb(scpi_t *ctx, scpi_ctrl_name_t ctrl, scpi_reg_val_t val)
{
    (void)ctx;
    (void)val;
    (void)ctrl;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_flush_cb(scpi_t *ctx)
{
    (void)ctx;
    return SCPI_RES_OK;
}

static scpi_interface_t scpi_interface = {
    .error = scpi_error_cb,
    .write = scpi_write_cb,
    .control = scpi_control_cb,
    .flush = scpi_flush_cb,
    .reset = scpi_reset_cb,
};

/* ==========================================================================
 * API pública del parser SCPI
 * ========================================================================== */

void scpi_engine_init(void)
{
    SCPI_Init(&scpi_context,
              scpi_commands,
              &scpi_interface,
              scpi_units_def,
              "DF",
              "CONTROL DE TEMPERATURA",
              "CT2CH",
              "v1.1",
              scpi_input_buffer, SCPI_INPUT_BUFFER_LENGTH,
              scpi_error_queue, SCPI_ERROR_QUEUE_SIZE);
}

void scpi_init(void)
{
}
