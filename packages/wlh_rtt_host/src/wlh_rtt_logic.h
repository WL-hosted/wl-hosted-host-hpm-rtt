#ifndef WLH_RTT_LOGIC_H
#define WLH_RTT_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wlh_rtt_stats.h"

typedef enum wlh_rtt_validation_result {
    WLH_RTT_VALID = 0,
    WLH_RTT_INVALID = -1,
    WLH_RTT_UNSUPPORTED = -2
} wlh_rtt_validation_result_t;

typedef enum wlh_rtt_interface_route {
    WLH_RTT_ROUTE_INVALID = -1,
    WLH_RTT_ROUTE_STA = 0,
    WLH_RTT_ROUTE_AP = 1
} wlh_rtt_interface_route_t;

wlh_rtt_validation_result_t wlh_rtt_softap_validate(
    size_t ssid_length,
    size_t credential_length,
    uint32_t security,
    uint32_t channel,
    bool hidden,
    uint32_t open_security,
    uint32_t wpa2_security
);
uint32_t wlh_rtt_next_generation(uint32_t current);
wlh_rtt_interface_route_t wlh_rtt_route_wire_interface(
    uint32_t wire_interface,
    uint32_t wire_sta,
    uint32_t wire_ap
);
uint64_t wlh_rtt_pool_initial_mask(unsigned count);
int wlh_rtt_pool_take(uint64_t *free_mask, unsigned count);
void wlh_rtt_pool_put(uint64_t *free_mask, unsigned index);
bool wlh_rtt_ap_client_update(
    wlh_rtt_ap_client_stats_t *table,
    size_t capacity,
    const uint8_t mac[6],
    bool joined,
    int16_t rssi_dbm,
    uint32_t association_id,
    uint32_t ieee80211_reason,
    uint32_t *active_count
);
void wlh_rtt_ap_client_clear(
    wlh_rtt_ap_client_stats_t *table,
    size_t capacity,
    uint32_t *active_count
);

#endif
