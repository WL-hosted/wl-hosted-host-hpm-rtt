#include "wlh_rtt_logic.h"

#include <string.h>

wlh_rtt_validation_result_t wlh_rtt_softap_validate(
    size_t ssid_length,
    size_t credential_length,
    uint32_t security,
    uint32_t channel,
    bool hidden,
    uint32_t open_security,
    uint32_t wpa2_security
) {
    if (ssid_length == 0u || ssid_length > 32u || channel < 1u ||
        channel > 14u)
        return WLH_RTT_INVALID;
    if (hidden || (security != open_security && security != wpa2_security))
        return WLH_RTT_UNSUPPORTED;
    if (security == open_security)
        return credential_length == 0u ? WLH_RTT_VALID : WLH_RTT_INVALID;
    return credential_length >= 8u && credential_length <= 63u
               ? WLH_RTT_VALID
               : WLH_RTT_INVALID;
}

uint32_t wlh_rtt_next_generation(uint32_t current) {
    current++;
    return current == 0u ? 1u : current;
}

wlh_rtt_interface_route_t wlh_rtt_route_wire_interface(
    uint32_t wire_interface,
    uint32_t wire_sta,
    uint32_t wire_ap
) {
    if (wire_interface == wire_sta)
        return WLH_RTT_ROUTE_STA;
    if (wire_interface == wire_ap)
        return WLH_RTT_ROUTE_AP;
    return WLH_RTT_ROUTE_INVALID;
}

uint64_t wlh_rtt_pool_initial_mask(unsigned count) {
    if (count == 0u || count > 64u)
        return 0u;
    return count == 64u ? UINT64_MAX : (UINT64_C(1) << count) - 1u;
}

int wlh_rtt_pool_take(uint64_t *free_mask, unsigned count) {
    unsigned index;
    if (free_mask == NULL || count > 64u)
        return -1;
    for (index = 0u; index < count; ++index) {
        uint64_t bit = UINT64_C(1) << index;
        if ((*free_mask & bit) != 0u) {
            *free_mask &= ~bit;
            return (int)index;
        }
    }
    return -1;
}

void wlh_rtt_pool_put(uint64_t *free_mask, unsigned index) {
    if (free_mask != NULL && index < 64u)
        *free_mask |= UINT64_C(1) << index;
}

bool wlh_rtt_ap_client_update(
    wlh_rtt_ap_client_stats_t *table,
    size_t capacity,
    const uint8_t mac[6],
    bool joined,
    int16_t rssi_dbm,
    uint32_t association_id,
    uint32_t ieee80211_reason,
    uint32_t *active_count
) {
    wlh_rtt_ap_client_stats_t *client = NULL;
    size_t index;
    if (table == NULL || mac == NULL || active_count == NULL)
        return false;
    for (index = 0u; index < capacity; ++index) {
        if (table[index].active &&
            memcmp(table[index].mac, mac, sizeof(table[index].mac)) == 0) {
            client = &table[index];
            break;
        }
        if (joined && client == NULL && !table[index].active)
            client = &table[index];
    }
    if (client == NULL)
        return false;
    {
        bool was_active = client->active;
        memcpy(client->mac, mac, sizeof(client->mac));
        client->active = joined;
        client->rssi_dbm = rssi_dbm;
        client->association_id = association_id;
        client->ieee80211_reason = ieee80211_reason;
        if (joined && !was_active)
            (*active_count)++;
        else if (!joined && was_active && *active_count != 0u)
            (*active_count)--;
    }
    return true;
}

void wlh_rtt_ap_client_clear(
    wlh_rtt_ap_client_stats_t *table,
    size_t capacity,
    uint32_t *active_count
) {
    if (table != NULL)
        memset(table, 0, capacity * sizeof(*table));
    if (active_count != NULL)
        *active_count = 0u;
}
