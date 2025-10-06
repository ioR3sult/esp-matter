#include "console_state.h"

static console_state_t g_state;

console_state_t * console_state(void) {
    return &g_state;
}
