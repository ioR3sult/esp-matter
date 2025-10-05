#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    volatile bool notify_enabled;
    volatile uint16_t conn_handle;
    volatile uint16_t tx_val_handle;
} console_state_t;

console_state_t * console_state(void);

#ifdef __cplusplus
}
#endif
