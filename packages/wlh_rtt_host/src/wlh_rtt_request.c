#include "wlh_rtt_internal.h"

#include <string.h>

uint32_t wlh_rtt_request_begin(
    wlh_rtt_host_impl_t *host,
    wlh_rtt_interface_t *interface,
    wlh_rtt_request_kind_t kind,
    wlh_rtt_request_completion_t **completion
) {
    uint32_t generation = 0u;
    unsigned index;
    if (completion == RT_NULL)
        return 0u;
    *completion = RT_NULL;
    rt_mutex_take(host->lock, RT_WAITING_FOREVER);
    if (!interface->request.active) {
        for (index = 0u; index < WLH_RTT_REQUEST_COMPLETION_SLOTS; ++index) {
            wlh_rtt_request_completion_t *slot =
                &interface->completions[index];
            if (slot->in_use)
                continue;
            generation = wlh_rtt_next_generation(host->next_generation);
            host->next_generation = generation;
            interface->request.kind = kind;
            interface->request.generation = generation;
            interface->request.active = true;
            slot->interface = interface;
            slot->kind = kind;
            slot->generation = generation;
            slot->in_use = true;
            *completion = slot;
            break;
        }
    }
    rt_mutex_release(host->lock);
    return generation;
}

void wlh_rtt_request_abort(wlh_rtt_request_completion_t *completion) {
    wlh_rtt_interface_t *interface;
    wlh_rtt_host_impl_t *host;
    if (completion == RT_NULL || !completion->in_use)
        return;
    interface = completion->interface;
    host = interface->host;
    rt_mutex_take(host->lock, RT_WAITING_FOREVER);
    if (interface->request.active &&
        interface->request.kind == completion->kind &&
        interface->request.generation == completion->generation)
        rt_memset(&interface->request, 0, sizeof(interface->request));
    completion->in_use = false;
    rt_mutex_release(host->lock);
}

void wlh_rtt_request_finish(
    wlh_rtt_interface_t *interface, wlh_rtt_request_kind_t kind
) {
    wlh_rtt_host_impl_t *host = interface->host;
    rt_mutex_take(host->lock, RT_WAITING_FOREVER);
    if (interface->request.active && interface->request.kind == kind)
        rt_memset(&interface->request, 0, sizeof(interface->request));
    rt_mutex_release(host->lock);
}

void wlh_rtt_request_complete(
    void *context,
    wlh_host_result_t result,
    uint16_t status_domain,
    int16_t status_code,
    const uint8_t *payload,
    size_t payload_size
) {
    wlh_rtt_request_completion_t *completion = context;
    wlh_rtt_interface_t *interface;
    wlh_rtt_request_kind_t kind;
    bool matched;
    (void)status_domain;
    (void)status_code;
    (void)payload;
    (void)payload_size;
    if (completion == RT_NULL || !completion->in_use)
        return;
    interface = completion->interface;
    kind = completion->kind;
    rt_mutex_take(interface->host->lock, RT_WAITING_FOREVER);
    matched = interface->request.active &&
              interface->request.kind == completion->kind &&
              interface->request.generation == completion->generation;
    if (result != WLH_HOST_OK && matched)
        rt_memset(&interface->request, 0, sizeof(interface->request));
    completion->in_use = false;
    rt_mutex_release(interface->host->lock);
    if (result == WLH_HOST_OK || !matched)
        return;
    if (kind == WLH_RTT_REQUEST_SCAN)
        wlh_rtt_report_event(interface, RT_WLAN_DEV_EVT_SCAN_DONE, RT_NULL, 0);
    else if (kind == WLH_RTT_REQUEST_CONNECT)
        wlh_rtt_report_event(
            interface, RT_WLAN_DEV_EVT_CONNECT_FAIL, RT_NULL, 0
        );
    else if (kind == WLH_RTT_REQUEST_DISCONNECT)
        wlh_rtt_report_event(
            interface, RT_WLAN_DEV_EVT_DISCONNECT, RT_NULL, 0
        );
    else if (kind == WLH_RTT_REQUEST_AP_START)
        wlh_rtt_report_event(interface, RT_WLAN_DEV_EVT_AP_STOP, RT_NULL, 0);
    else if (kind == WLH_RTT_REQUEST_AP_STOP)
        wlh_rtt_report_event(interface, RT_WLAN_DEV_EVT_AP_STOP, RT_NULL, 0);
}
