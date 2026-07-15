#ifndef USB_TMC_FSM_PROCESS_H_
#define USB_TMC_FSM_PROCESS_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define TX_BUFFER_LENGTH 1024

    /* Status Byte Masks (IEEE 488.2) */
    typedef enum
    {
        STB_NONE = 0x00,
        STB_MAV_BIT = 0x10, // Message Available
        STB_ESB_BIT = 0x20, // Event Status Bit
        STB_RQS_BIT = 0x40  // Request Service
    } usb_tmc_status_t;

    /* ESTADOS DE LA FSM */
    typedef enum
    {
        STATE_IDLE = 0,
        STATE_RECEIVING,
        STATE_REPLY_READY
    } usb_tmc_state_t;

    /* EVENTOS DE LA FSM */
    typedef enum
    {
        EV_RX_START,  // Inicia recepción (Bulk OUT)
        EV_RX_CHUNK,  // Llega un pedazo de datos
        EV_RX_END,    // Llega el final del mensaje
        EV_TX_REQ,    // Host pide leer (Bulk IN)
        EV_TX_DONE,   // Host terminó de leer
        EV_SCPI_REPLY // libscpi generó respuesta
    } usb_tmc_event_t;

    /**
     * @brief Procesa un evento en la máquina de estados USBTMC.
     */
    void usb_tmc_fsm_process(usb_tmc_event_t event, void *data, size_t len);

    /**
     * @brief Obtiene el Status Byte actual (usado por el callback de TinyUSB).
     */
    usb_tmc_status_t usb_tmc_get_stb(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_TMC_FSM_PROCESS_H_ */