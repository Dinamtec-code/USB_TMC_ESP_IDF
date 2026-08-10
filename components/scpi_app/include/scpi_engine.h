#ifndef SCPI_ENGINE_H_
#define SCPI_ENGINE_H_

typedef enum
{
    STATUS_SCPI_NONE = 0X00,
    STATUS_SCPI_REPLY_PROCESS = 0X01,
    STATUS_SCPI_REPLY_READY = 0X02,
    STATUS_SCPI_REPLY_DONE = 0X04,
    STATUS_SCPI_IDLE = 0X08,
    STATUS_SCPI_ERROR = 0X10
} scpi_status_t;

void scpi_engine_task(void *pvParameters);

#endif /* SCPI_ENGINE_TASK_H_ */