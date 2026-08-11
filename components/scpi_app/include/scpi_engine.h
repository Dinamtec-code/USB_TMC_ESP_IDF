#ifndef SCPI_ENGINE_H_
#define SCPI_ENGINE_H_

void iface_init(void);
void scpi_engine_init(void);
void scpi_engine_task(void *pvParameters);

#endif /* SCPI_ENGINE_TASK_H_ */