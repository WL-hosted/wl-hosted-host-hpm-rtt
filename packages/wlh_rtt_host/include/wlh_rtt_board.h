#ifndef WLH_RTT_BOARD_H
#define WLH_RTT_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wlh_rtt_board_ops {
    void (*fatal_recovery)(void *context, int reason);
} wlh_rtt_board_ops_t;

#ifdef __cplusplus
}
#endif

#endif
