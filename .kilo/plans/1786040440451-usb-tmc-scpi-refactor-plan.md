# Plan de Refactor: Resolución de Dependencias y Errores de Compilación USB-TMC/SCPI

## Resumen del estado actual

**BUILD STATUS: CRÍTICO — 4 errores de compilación, 1 dependencia circular, 30+ elementos de código muerto**

Se han aplicado cambios parciales pero **el build aún falla** debido a macros/tipos indefinidos y código muerto. El ciclo de dependencias está parcialmente roto pero persiste. Este plan documenta todos los problemas identificados en todos los archivos del proyecto (24 archivos revisados).

---

## Análisis de Todos los Archivos del Proyecto

### Árbol de archivos del proyecto (24 archivos)

```
usb_tmc_app/
├── CMakeLists.txt                              (14 líneas) [raíz]
├── sdkconfig                                   (3938 líneas)
├── dependencies.lock                           (47 líneas)
├── main/
│   ├── main.c                                  (52 líneas) [ACTUALMENTE ROTO]
│   └── CMakeLists.txt                          (11 líneas)
├── components/
│   ├── usb_tmc/
│   │   ├── CMakeLists.txt                      (10 líneas) [DEPENDENCIA CIRCULAR]
│   │   ├── include/
│   │   │   ├── usb_tmc_process.h              (52 líneas) [USA scpi_status_t SIN INCLUDE]
│   │   │   ├── usb_tmc_init.h                 (26 líneas) [TYPO __cpluslpus]
│   │   │   ├── usb_tmc_drv.h                  (4 líneas) [STUB VACÍO - MUERTO]
│   │   │   ├── usb_tmc_config.h               (90 líneas) [CONFIG TINYUSB - OK]
│   │   │   └── usb_tmc_cb.h                   (33 líneas) [DECLARA CALLBACKS - OK]
│   │   └── src/
│   │       ├── usb_tmc_process.c              (197 líneas) [ERRORES DE COMPILACIÓN]
│   │       ├── usb_tmc_init.c                 (100 líneas) [4 VARS+4 MACROS MUERTAS]
│   │       ├── usb_tmc_cb.c                   (174 líneas) [2 VARS GLOBALES MUERTAS]
│   │       └── usb_descriptors.c              (167 líneas) [OK - SIN PROBLEMAS]
│   ├── scpi_app/
│   │   ├── CMakeLists.txt                      (7 líneas) [DEPENDENCIA CIRCULAR]
│   │   ├── include/
│   │   │   ├── scpi_engine.h                  (14 líneas) [FALTA DECLARAR scpi_engine_init()]
│   │   │   └── scpi_iface_drv.h               (46 líneas) [OK]
│   │   └── src/
│   │       └── scpi_engine.c                  (659 líneas) [INTEGRACIÓN INCOMPLETA]
│   └── libscpi/
│       ├── CMakeLists.txt                      (27 líneas) [OK]
│       ├── inc/scpi/                           (11 headers)
│       │   ├── scpi.h (agregador), types.h, parser.h, ieee488.h, constants.h,
│       │   ├── config.h, utils.h, error.h, expression.h, units.h, minimal.h, cc.h
│       ├── src/                                (9 sources)
│       │   ├── error.c, fifo.c, ieee488.c, minimal.c, parser.c,
│       │   ├── units.c, utils.c, lexer.c, expression.c
│       └── test/                               (4 tests - NO COMPILADOS)
│           ├── test_parser.c, test_scpi_utils.c, test_lexer_parser.c, test_fifo.c
```

---

## Problemas Críticos (Bloquean compilación)

### Problema 1: `scpi_interface_t` líquido en usb_tmc_process.c (ERROR DE SINTAXIS)
**Archivo:** `components/usb_tmc/src/usb_tmc_process.c`  
**Líneas:** 16-17  
```c
scpi_interface_t
```
- Línea suelta sin cuerpo ni punto y coma. Causará error de compilación inmediato.
- **Causa:** Fragmento incompleto de código que fue dejado accidentalmente al refactorizar.

### Problema 2: Macros `STATUS_*` indefinidas en usb_tmc_process.c
**Archivo:** `components/usb_tmc/src/usb_tmc_process.c`  
**Líneas:** 51, 67, 107, 114, 117, 122, 147

| Macro usada | Valor esperado | Definición correcta en scpi_engine.h |
|---|---|---|
| `STATUS_REPLY_READY` | 0x02 | `STATUS_SCPI_ENG_REPLY_READY` |
| `STATUS_NONE` | 0x00 | `STATUS_SCPI_ENG_NONE` |
| `STATUS_SCPI_REPLY_READY` | 0x02 | `STATUS_SCPI_ENG_REPLY_READY` |
| `STATUS_SCPI_NONE` | 0x00 | `STATUS_SCPI_ENG_NONE` |
| `STATUS_SCPI_REPLY_DONE` | 0x04 | `STATUS_SCPI_ENG_REPLY_DONE` |

**Causa:** El enum usa prefijo `STATUS_SCPI_ENG_*` pero el código usa nombres `STATUS_*` y `STATUS_SCPI_*` que no existen.

### Problema 3: Dependencia circular entre usb_tmc y scpi_app
**Archivos:**
- `components/usb_tmc/CMakeLists.txt` línea 8: `REQUIRES tinyusb esp_tinyusb esp_system libscpi scpi_app`
- `components/scpi_app/CMakeLists.txt` línea 5: `REQUIRES usb_tmc libscpi`

**Workaround actual:** `CMakeLists.txt` raíz línea 9: `include_directories("components/usb_tmc/include")` — fuerza visibilidad global de headers.

---

## Problemas de Conexión / Integración

### Problema 4: `scpi_engine_init()` no declarada en scpi_engine.h
**Archivo:** `components/scpi_app/include/scpi_engine.h` (14 líneas)

El enum `scpi_status_t` y `scpi_engine_task()` están declarados, pero **falta la declaración de `scpi_engine_init()`** que está definida en `scpi_engine.c` línea 645.

### Problema 5: `main.c` no inicializa ni crea tarea SCPI
**Archivo:** `main/main.c` (52 líneas)

```c
// main.c línea 39 solo llama:
tmc_hal_init();
// NUNCA LLAMA:
scpi_engine_init();
// NUNCA CREA:
xTaskCreate(scpi_engine_task, ...);
```

### Problema 6: `usb_tmc_process.h` no incluye `scpi_engine.h`
**Archivo:** `components/usb_tmc/include/usb_tmc_process.h` línea 44

Declara `void usb_tmc_set_scpi_status(scpi_status_t status);` pero **no incluye** `scpi_engine.h` donde está definido `scpi_status_t`. Funciona actualmente solo por accidente (el .c incluye primero `scpi_iface_drv.h` que incluye `scpi_engine.h`).

---

## Código Muerto (Dead Code) — 30 elementos identificados

### En `usb_tmc/src/usb_tmc_process.c`:
1. Línea 6: `#include "scpi/scpi.h"` — incluido pero contexto SCPI no usado
2. Líneas 16-17: `scpi_interface_t` suelto — **error de compilación**
3. Líneas 85-92: `usb_tmc_set_rx_stream_buffer()` / `usb_tmc_set_tx_stream_buffer()` — nunca declaradas en header, nunca llamadas
4. Línea 106: Comentario con referencia a `usb_tmc_iface` (no existe)
5. Línea 52: Comentario con nombre obsoleto `EV_SCPI_REPLY`

### En `usb_tmc/src/usb_tmc_init.c`:
6. Líneas 16-19: Macros `IEEE4882_STB_*` — nunca usadas
7. Líneas 25-28: Variables `queryState`, `queryDelayStart`, `bulkInStarted`, `idnQuery` — nunca usadas

### En `usb_tmc/src/usb_tmc_cb.c`:
8. Línea 12: `static volatile uint8_t status;` — nunca usada
9. Línea 51: `bool qidn1 = false;` — nunca usada

### En `usb_tmc/include/usb_tmc_drv.h`:
10. Archivo entero (4 líneas) — stub vacío, nunca incluido

### En `usb_tmc/include/usb_tmc_init.h`:
11. Línea 13: `void usbtmc_app_task(void *pvParameters);` — nunca definida ni llamada
12. Línea 7: `#ifdef __cpluslpus` (typo, debería ser `__cplusplus`)

### En `scpi_app/src/scpi_engine.c`:
13. Línea 49: `scpi_stream_register()` — nunca llamada
14. Línea 61: `scpi_event_queues_register()` — nunca llamada  
15. Línea 75: `driver_iface_init()` — nunca llamada
16. Línea 126: `get_rx_stream()` — nunca llamada
17. Línea 131: `get_tx_stream()` — nunca llamada
18. Líneas 136-148: `SCPI_Write()` — firma incorrecta (`void*` vs `scpi_t*`), nunca referenciada
19. Líneas 150-152: `set_event()` — función vacía, nunca llamada
20. Línea 657-659: `scpi_init()` — función vacía, nunca llamada
21. Línea 12: `// #include "usb_tmc_process.h"` — comentado
22. Línea 144: `// usb_tmc_fsm_process(EV_SCPI_REPLY, NULL, 0);` — nombre obsoleto, comentado
23. Líneas 192-196: Bloque comentado sobre `tud_usbtmc_start_bus_read()`

### En `main/main.c`:
24. Líneas 14-17: Bloque `extern` comentado
25. Línea 28: `// esp_task_wdt_deinit();` comentado
26. Líneas 41-49: Bloque `xTaskCreatePinnedToCore(usbtmc_app_task, ...)` comentado

### En `usb_tmc_process.c`:
27. Línea 32: `TaskHandle_t scpi_task_handle;` — declarada pero nunca asignada

### En `usb_tmc_process.c` - `generate_response()`:
28. Línea 42: Parámetro `msg` nunca usado (unused parameter)
29. Línea 51: `usb_tmc_set_scpi_status(STATUS_REPLY_READY)` — macro indefinida
30. Línea 67: `static scpi_status_t current_scpi_status = STATUS_NONE;` — macro indefinida

### En `scpi_engine.c`:
31. Líneas 21-35: Streams/buffers declarados pero solo parcialmente usados
32. Línea 218: `static scpi_t scpi_context;` redeclarado (conflicto conceptual con línea 19)

---

## Matriz de Dependencias de Componentes

| Componente | REQUIRES | WHOLE_ARCHIVE | Estado |
|---|---|---|---|
| **Raíz** (CMakeLists.txt) | — | — | Activo |
| **main** | `usb_tmc`, `nvs_flash` | No | Activo |
| **usb_tmc** | `tinyusb`, `esp_tinyusb`, `esp_system`, `libscpi`, `scpi_app` | Sí | **Circular** |
| **scpi_app** | `usb_tmc`, `libscpi` | Sí | **Circular** |
| **libscpi** | — | — | Activo |

### Ciclo de dependencias:
```
usb_tmc ──REQUIRES──> scpi_app
scpi_app ──REQUIRES──> usb_tmc
```

### Solución propuesta:
- **Eliminar** `usb_tmc` de `REQUIRES` en `scpi_app/CMakeLists.txt`
- **Invertir** la dependencia: `scpi_app` proporciona la interfaz abstracta, `usb_tmc` la implementa
- **Eliminar** el workaround `include_directories("components/usb_tmc/include")` de la raíz `CMakeLists.txt`

---

## Tareas de Implementación (Ordenadas)

### Tarea 1: Definir macros de estado en scpi_engine.h

**Archivo:** `components/scpi_app/include/scpi_engine.h`

**Acción:** Agregar macros de compatibilidad y declaración de `scpi_engine_init()`:

```c
// Macros de compatibilidad para código legacy
#define STATUS_NONE              STATUS_SCPI_ENG_NONE
#define STATUS_REPLY_READY       STATUS_SCPI_ENG_REPLY_READY
#define STATUS_REPLY_PROCESS     STATUS_SCPI_ENG_REPLY_PROCESS
#define STATUS_REPLY_DONE        STATUS_SCPI_ENG_REPLY_DONE

// Macros alineadas con patrón STATUS_SCPI_*
#define STATUS_SCPI_NONE         STATUS_SCPI_ENG_NONE
#define STATUS_SCPI_REPLY_READY  STATUS_SCPI_ENG_REPLY_READY
#define STATUS_SCPI_REPLY_PROCESS STATUS_SCPI_ENG_REPLY_PROCESS
#define STATUS_SCPI_REPLY_DONE   STATUS_SCPI_ENG_REPLY_DONE

// Declaración pública (actualmente solo está en .c)
void scpi_engine_init(void);
void scpi_engine_task(void *pvParameters);
```

### Tarea 2: Limpiar código muerto y error de sintaxis en usb_tmc_process.c

**Archivo:** `components/usb_tmc/src/usb_tmc_process.c`

**Acción:**
- Eliminar líneas 16-17: `scpi_interface_t` suelto (error de compilación)
- Eliminar línea 6: `#include "scpi/scpi.h"` (no se usa el contexto libscpi)
- Eliminar o declarar en header: `usb_tmc_set_rx_stream_buffer()`, `usb_tmc_set_tx_stream_buffer()` (líneas 85-92)
- Limpiar comentario línea 106 que referencia `usb_tmc_iface`
- Limpiar `TaskHandle_t scpi_task_handle;` si no se usa

### Tarea 3: Romper dependencia circular

**Archivo:** `components/scpi_app/CMakeLists.txt`

**Acción:** Cambiar:
```cmake
# ANTES:
REQUIRES usb_tmc libscpi

# DESPUÉS:
REQUIRES libscpi
```

**Archivo:** `CMakeLists.txt` (raíz)

**Acción:** Eliminar la línea workaround:
```cmake
# Eliminar:
include_directories("components/usb_tmc/include")
```

### Tarea 4: Agregar include de scpi_engine.h en usb_tmc_process.h

**Archivo:** `components/usb_tmc/include/usb_tmc_process.h`

**Acción:** Agregar al inicio:
```c
#include "scpi_engine.h"  // Para scpi_status_t
```

### Tarea 5: Conectar SCPI al flujo de main.c

**Archivo:** `main/main.c`

**Acción:** Después de `tmc_hal_init()`, agregar:
```c
scpi_engine_init();
xTaskCreatePinnedToCore(scpi_engine_task, "SCPI_Task", 4096, NULL, 5, NULL, 0);
```

Y agregar `#include "scpi_engine.h"` al inicio.

### Tarea 6: Implementar set_event() con notificación de tarea

**Archivo:** `components/scpi_app/src/scpi_engine.c`

**Acción:** Reemplazar la función vacía:
```c
void set_event(scpi_status_t status)
{
    if (status == STATUS_SCPI_ENG_REPLY_READY && scpi_task_handle)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(scpi_task_handle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken)
        {
            portYIELD_FROM_ISR();
        }
    }
}
```

Necesitará extern `TaskHandle_t scpi_task_handle;` — pero esa variable está en `usb_tmc_process.c` (que scpi_app no puede importar). **Problema:** `set_event()` debe notificar a quién? A la tarea SCPI (`scpi_engine_task`) o a la FSM de usb_tmc?

### Tarea 7: Limpiar código muerto adicional

**Acciones pendientes:**
- `usb_tmc_cb.c` líneas 12, 51: Eliminar variables globales muertas
- `usb_tmc_init.c` líneas 16-19, 25-28: Eliminar macros y variables muertas
- `usb_tmc_drv.h`: Eliminar archivo (o implementarlo)
- `usb_tmc_init.h` línea 7: Corregir typo `__cpluslpus` → `__cplusplus`
- `usb_tmc_init.h` línea 13: Eliminar `usbtmc_app_task` (no existe)
- `main.c` líneas 14-17, 28, 41-49: Limpiar código comentado
- `scpi_engine.c` líneas 136-148, 150-152, 657-659: Eliminar funciones muertas
- `usb_tmc_process.c` línea 42: Parámetro `msg` unused en `generate_response()`

---

## Matriz de Símbolos Referenciados

### `scpi_status_t` — Definición única en `scpi_engine.h:10`
- Referenciado en: `usb_tmc_process.h:44`, `usb_tmc_process.c:67,94`, `scpi_iface_drv.h:34`, `scpi_engine.c:150`

### `STATUS_*` macros — Definidos en `scpi_engine.h:6-9` como `STATUS_SCPI_ENG_*`
- Usados como `STATUS_*`, `STATUS_SCPI_*` (no definidos) en: `usb_tmc_process.c:51,67,107,114,117,122,147`

### `EV_*` eventos — Definidos en `usb_tmc_process.h:34-40` como `EV_TMC_*`
- Referenciados correctamente en `usb_tmc_process.c` (7 usos) y `usb_tmc_cb.c` (5 usos)
- Nombres obsoletos en comentarios: `usb_tmc_process.c:52`, `scpi_engine.c:144`

### `usb_tmc_process.h` — Incluido en:
- `usb_tmc_process.c:11` ✓
- `usb_tmc_cb.c:9` ✓  
- `usb_tmc_init.c:11` ✓
- `scpi_engine.c:12` (comentado)

---

## Validación

1. `idf.py build` — eliminar todos los errores de compilación
2. Verificar no hay dependencia circular: `scpi_app` no debe requerir `usb_tmc`
3. Verificar macros de estado consistentes entre componentes
4. Verificar que `scpi_engine_init()` está declarada en header y definida en .c
5. Verificar que `main.c` llama a `scpi_engine_init()` y crea la tarea SCPI
6. Verificar que `set_event()` coordina correctamente con la FSM de `usb_tmc`

---

## Riesgos

- **Arquitectura de notificación:** `set_event()` necesita saber a quién notificar. Si `scpi_app` no puede depender de `usb_tmc`, se necesita un callback de registro o una cola de eventos.
- **Integración real SCPI:** `generate_response()` en `usb_tmc_process.c` es un simulacro. No usa el parser libscpi real. Para una integración completa, la FSM debe inyectar datos al stream buffer de `scpi_engine_task`.
- **Doble `tx_buffer`:** Existen buffers `tx_buffer` en ambos `scpi_engine.c` (no static) y `usb_tmc_process.c` (static). Podría causar confusión.
- **Eliminación de include directory global:** El workaround `include_directories` en raíz puede estar ahí por otras dependencias. Verificar antes de eliminar.
