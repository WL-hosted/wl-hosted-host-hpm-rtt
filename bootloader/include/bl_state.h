#ifndef BL_STATE_H
#define BL_STATE_H

#include <stdbool.h>
#include <stdint.h>

bool bl_state_init(void);
bool bl_state_update_for_reset(uint32_t reset_status, uint32_t *fail_count);
bool bl_state_should_stay(void);
void bl_state_clear_after_update(void);

#endif
