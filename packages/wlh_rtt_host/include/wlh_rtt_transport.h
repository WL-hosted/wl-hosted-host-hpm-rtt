#ifndef WLH_RTT_TRANSPORT_H
#define WLH_RTT_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wlh_rtt_tx_done_fn)(
    void *context, uint8_t *frame, size_t length, int status
);

typedef struct wlh_rtt_transport_stats {
    uint32_t tx_frames;
    uint32_t tx_errors;
    uint32_t tx_queue_full;
    uint32_t rx_frames;
    uint32_t rx_errors;
    uint32_t rx_invalid_length;
    uint32_t bus_resets;
} wlh_rtt_transport_stats_t;

typedef struct wlh_rtt_transport_ops {
    int (*start)(void *context);
    int (*stop)(void *context);
    int (*submit_tx)(
        void *context,
        uint8_t *frame,
        size_t length,
        wlh_rtt_tx_done_fn done,
        void *done_context
    );
    int (*soft_reset)(void *context);
    int (*get_stats)(void *context, wlh_rtt_transport_stats_t *stats);
} wlh_rtt_transport_ops_t;

struct wlh_rtt_host;

int wlh_rtt_sdio_transport_create(
    struct wlh_rtt_host *host,
    const wlh_rtt_transport_ops_t **ops,
    void **context
);

#ifdef __cplusplus
}
#endif

#endif
