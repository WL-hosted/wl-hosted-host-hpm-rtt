#ifndef WLH_RTT_HOST_H
#define WLH_RTT_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "wlh_rtt_board.h"
#include "wlh_rtt_stats.h"
#include "wlh_rtt_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WLH_RTT_HOST_STORAGE_WORDS 2048u

typedef struct wlh_rtt_host {
    uintptr_t opaque[WLH_RTT_HOST_STORAGE_WORDS];
} wlh_rtt_host_t;

typedef struct wlh_rtt_host_config {
    const char *sta_device_name;
    const char *ap_device_name;
    const wlh_rtt_transport_ops_t *transport_ops;
    void *transport_context;
    const wlh_rtt_board_ops_t *board_ops;
    void *board_context;
} wlh_rtt_host_config_t;

typedef enum wlh_rtt_link_event {
    WLH_RTT_LINK_AVAILABLE = 1,
    WLH_RTT_LINK_LOST = 2
} wlh_rtt_link_event_t;

int wlh_rtt_host_init(
    wlh_rtt_host_t *host, const wlh_rtt_host_config_t *config
);
int wlh_rtt_host_start(wlh_rtt_host_t *host);
int wlh_rtt_host_stop(wlh_rtt_host_t *host);
int wlh_rtt_host_recover(wlh_rtt_host_t *host);
int wlh_rtt_host_get_stats(wlh_rtt_host_t *host, wlh_rtt_stats_t *stats);
int wlh_rtt_host_input(
    wlh_rtt_host_t *host, const void *frame, size_t length
);
void wlh_rtt_host_link_event(
    wlh_rtt_host_t *host, wlh_rtt_link_event_t event
);

wlh_rtt_host_t *wlh_rtt_host_default(void);
int wlh_rtt_host_default_start(void);

#ifdef __cplusplus
}
#endif

#endif
