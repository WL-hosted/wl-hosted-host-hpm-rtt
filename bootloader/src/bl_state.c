#include "bl_state.h"

#include <flashdb.h>
#include <hpm_common.h>
#include <hpm_ppor_drv.h>

static struct fdb_kvdb state_db;
static bool state_ready;

#define STATE_KEY_FAIL_COUNT "bl.fail_count"
#define STATE_KEY_FORCE_BL "bl.force_bootloader"
#define BL_FAIL_LIMIT 5U

static bool get_u32(const char *key, uint32_t *value) {
    size_t got = fdb_kv_get_blob(
        &state_db, key, fdb_blob_make(NULL, value, sizeof(*value))
    );
    return got == sizeof(*value);
}

static bool set_u32(const char *key, uint32_t value) {
    return fdb_kv_set_blob(
               &state_db, key, fdb_blob_make(NULL, &value, sizeof(value))
           ) == FDB_NO_ERR;
}

bool bl_state_init(void) {
    if (fdb_kvdb_init(&state_db, "bl_state", "flashdb", NULL, NULL) !=
        FDB_NO_ERR)
        return false;
    state_ready = true;
    return true;
}

bool bl_state_update_for_reset(uint32_t reset_status, uint32_t *fail_count) {
    if (!state_ready)
        return false;
    uint32_t count = 0;
    if ((reset_status & ppor_reset_wdog0) != 0U) {
        (void)get_u32(STATE_KEY_FAIL_COUNT, &count);
        if (count < UINT32_MAX)
            ++count;
    }
    if (!set_u32(STATE_KEY_FAIL_COUNT, count))
        return false;
    if (count >= BL_FAIL_LIMIT && !set_u32(STATE_KEY_FORCE_BL, 1U))
        return false;
    *fail_count = count;
    return true;
}

bool bl_state_should_stay(void) {
    uint32_t force = 0;
    if (!state_ready)
        return false;
    bool stay = get_u32(STATE_KEY_FORCE_BL, &force) && force != 0U;
    if (stay) {
        /* 一次性进入 BL：本次会话按标志留在 UF2 模式，读到后立即清除，
         * 保证下次上电/复位不再因该标志滞留，直接进入 App。 */
        (void)set_u32(STATE_KEY_FORCE_BL, 0U);
    }
    return stay;
}

void bl_state_clear_after_update(void) {
    if (state_ready) {
        (void)set_u32(STATE_KEY_FAIL_COUNT, 0U);
        (void)set_u32(STATE_KEY_FORCE_BL, 0U);
    }
}
