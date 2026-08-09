#ifndef WLH_RTT_STATS_H
#define WLH_RTT_STATS_H

#include <stdbool.h>
#include <stdint.h>

#include "wlh_rtt_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wlh_rtt_interface_stats {
    bool active;
    uint32_t tx_frames;
    uint32_t tx_dropped;
    uint32_t rx_frames;
    uint32_t rx_pool_empty;
    uint32_t rx_queue_full;
    uint32_t rx_delivery_failed;
} wlh_rtt_interface_stats_t;

#define WLH_RTT_STATS_MAX_AP_CLIENTS 16u

typedef struct wlh_rtt_ap_client_stats {
    bool active;
    uint8_t mac[6];
    int16_t rssi_dbm;
    uint32_t association_id;
    uint32_t ieee80211_reason;
} wlh_rtt_ap_client_stats_t;

typedef struct wlh_rtt_stats {
    uint32_t host_state;
    uint32_t session_id;
    uint32_t recoveries;
    uint32_t ap_clients;
    wlh_rtt_transport_stats_t transport;
    wlh_rtt_interface_stats_t sta;
    wlh_rtt_interface_stats_t ap;
    wlh_rtt_ap_client_stats_t ap_client[WLH_RTT_STATS_MAX_AP_CLIENTS];
} wlh_rtt_stats_t;

#ifdef __cplusplus
}
#endif

#endif
