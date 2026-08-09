#include "wlh_rtt_internal.h"

#include <string.h>

#include "wifi.pb.h"

static wlh_rtt_interface_t *interface_from_device(
    struct rt_wlan_device *device
) {
    return device != RT_NULL ? device->user_data : RT_NULL;
}

void wlh_rtt_report_event(
    wlh_rtt_interface_t *interface,
    rt_wlan_dev_event_t event,
    void *data,
    int length
) {
    struct rt_wlan_buff buffer;
    struct rt_wlan_buff *buffer_ptr = RT_NULL;
    if (data != RT_NULL && length > 0) {
        buffer.data = data;
        buffer.len = length;
        buffer_ptr = &buffer;
    }
    rt_wlan_dev_indicate_event_handle(&interface->device, event, buffer_ptr);
}

rt_wlan_security_t wlh_rtt_security_from_wire(uint32_t security) {
    switch (security) {
    case wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_OPEN:
        return SECURITY_OPEN;
    case wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_WPA_PSK:
        return SECURITY_WPA_AES_PSK;
    case wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_WPA2_PSK:
        return SECURITY_WPA2_AES_PSK;
    case wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_WPA_WPA2_PSK:
        return SECURITY_WPA2_MIXED_PSK;
    default:
        return SECURITY_UNKNOWN;
    }
}

int wlh_rtt_security_to_wire(rt_wlan_security_t security, uint32_t *wire) {
    if (wire == RT_NULL)
        return -RT_EINVAL;
    switch (security) {
    case SECURITY_OPEN:
        *wire = wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_OPEN;
        return RT_EOK;
    case SECURITY_WPA_AES_PSK:
    case SECURITY_WPA_TKIP_PSK:
        *wire = wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_WPA_PSK;
        return RT_EOK;
    case SECURITY_WPA2_AES_PSK:
    case SECURITY_WPA2_TKIP_PSK:
        *wire = wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_WPA2_PSK;
        return RT_EOK;
    case SECURITY_WPA2_MIXED_PSK:
        *wire = wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_WPA_WPA2_PSK;
        return RT_EOK;
    default:
        return -RT_ENOSYS;
    }
}

static rt_err_t wlan_init(struct rt_wlan_device *device) {
    return interface_from_device(device) != RT_NULL ? RT_EOK : -RT_EINVAL;
}

static rt_err_t wlan_mode(
    struct rt_wlan_device *device, rt_wlan_mode_t mode
) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    if (interface == RT_NULL)
        return -RT_EINVAL;
    if ((interface->id == WLH_RTT_INTERFACE_STA && mode != RT_WLAN_STATION) ||
        (interface->id == WLH_RTT_INTERFACE_AP && mode != RT_WLAN_AP))
        return -RT_ENOSYS;
    return RT_EOK;
}

static rt_err_t wlan_scan(
    struct rt_wlan_device *device, struct rt_scan_info *scan_info
) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    wlh_wifi_scan_params_t params;
    uint32_t generation;
    wlh_rtt_request_completion_t *completion;
    if (interface == RT_NULL || interface->id != WLH_RTT_INTERFACE_STA)
        return -RT_EINVAL;
    generation = wlh_rtt_request_begin(
        interface->host, interface, WLH_RTT_REQUEST_SCAN, &completion
    );
    if (generation == 0u)
        return -RT_EBUSY;
    rt_memset(&params, 0, sizeof(params));
    params.scan_id = generation;
    params.max_results = RT_WLAN_SCAN_CACHE_NUM;
    if (scan_info != RT_NULL && scan_info->ssid.len != 0u) {
        params.ssid = scan_info->ssid.val;
        params.ssid_size = scan_info->ssid.len;
    }
    if (wlh_host_wifi_scan(
            &interface->host->core,
            &params,
            wlh_rtt_request_complete,
            completion
        ) != WLH_HOST_OK) {
        wlh_rtt_request_abort(completion);
        return -RT_ERROR;
    }
    return RT_EOK;
}

static rt_err_t wlan_scan_stop(struct rt_wlan_device *device) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    if (interface == RT_NULL || !interface->request.active ||
        interface->request.kind != WLH_RTT_REQUEST_SCAN)
        return -RT_EINVAL;
    /* TODO(core-scan-cancel): Host Core has no public scan-cancel request.
     * The bounded completion slots and scan_id generation safely isolate late
     * results. Remove this local-only cancellation after Core exposes cancel. */
    wlh_rtt_request_finish(interface, WLH_RTT_REQUEST_SCAN);
    wlh_rtt_report_event(interface, RT_WLAN_DEV_EVT_SCAN_DONE, RT_NULL, 0);
    return RT_EOK;
}

static rt_err_t wlan_join(
    struct rt_wlan_device *device, struct rt_sta_info *station
) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    wlh_wifi_connect_params_t params;
    uint32_t security;
    wlh_rtt_request_completion_t *completion;
    if (interface == RT_NULL || station == RT_NULL || station->ssid.len == 0u ||
        station->ssid.len > 32u || station->key.len > 63u ||
        wlh_rtt_security_to_wire(station->security, &security) != RT_EOK)
        return -RT_EINVAL;
    if (security == wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_OPEN &&
        station->key.len != 0u)
        return -RT_EINVAL;
    if (security != wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_OPEN &&
        (station->key.len < 8u || station->key.len > 63u))
        return -RT_EINVAL;
    if (wlh_rtt_request_begin(
            interface->host,
            interface,
            WLH_RTT_REQUEST_CONNECT,
            &completion
        ) == 0u)
        return -RT_EBUSY;
    rt_memset(&params, 0, sizeof(params));
    params.ssid = station->ssid.val;
    params.ssid_size = station->ssid.len;
    params.credential = station->key.val;
    params.credential_size = station->key.len;
    params.security = security;
    params.timeout_ms = WLH_RTT_HOST_RPC_TIMEOUT_MS;
    if (wlh_host_wifi_connect(
            &interface->host->core,
            &params,
            wlh_rtt_request_complete,
            completion
        ) != WLH_HOST_OK) {
        wlh_rtt_request_abort(completion);
        return -RT_ERROR;
    }
    return RT_EOK;
}

static rt_err_t wlan_disconnect(struct rt_wlan_device *device) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    wlh_rtt_request_completion_t *completion;
    if (interface == RT_NULL)
        return -RT_EINVAL;
    if (wlh_rtt_request_begin(
            interface->host,
            interface,
            WLH_RTT_REQUEST_DISCONNECT,
            &completion
        ) == 0u)
        return -RT_EBUSY;
    if (wlh_host_wifi_disconnect(
            &interface->host->core, wlh_rtt_request_complete, completion
        ) != WLH_HOST_OK) {
        wlh_rtt_request_abort(completion);
        return -RT_ERROR;
    }
    return RT_EOK;
}

static rt_err_t wlan_softap(
    struct rt_wlan_device *device, struct rt_ap_info *ap
) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    wlh_wifi_start_ap_params_t params;
    uint32_t security;
    wlh_rtt_request_completion_t *completion;
    wlh_rtt_validation_result_t validation;
    if (interface == RT_NULL || ap == RT_NULL)
        return -RT_EINVAL;
    if (wlh_rtt_security_to_wire(ap->security, &security) != RT_EOK)
        return -RT_ENOSYS;
    validation = wlh_rtt_softap_validate(
        ap->ssid.len,
        ap->key.len,
        security,
        ap->channel,
        ap->hidden,
        wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_OPEN,
        wlh_protocol_v1_WifiSecurity_WIFI_SECURITY_WPA2_PSK
    );
    if (validation == WLH_RTT_UNSUPPORTED)
        return -RT_ENOSYS;
    if (validation != WLH_RTT_VALID)
        return -RT_EINVAL;
    if (wlh_rtt_request_begin(
            interface->host,
            interface,
            WLH_RTT_REQUEST_AP_START,
            &completion
        ) == 0u)
        return -RT_EBUSY;
    rt_memset(&params, 0, sizeof(params));
    params.ssid = ap->ssid.val;
    params.ssid_size = ap->ssid.len;
    params.credential = ap->key.val;
    params.credential_size = ap->key.len;
    params.security = security;
    params.channel = ap->channel;
    params.max_clients = WLH_RTT_HOST_AP_MAX_CLIENTS;
    if (wlh_host_wifi_start_ap(
            &interface->host->core,
            &params,
            wlh_rtt_request_complete,
            completion
        ) != WLH_HOST_OK) {
        wlh_rtt_request_abort(completion);
        return -RT_ERROR;
    }
    return RT_EOK;
}

static rt_err_t wlan_ap_stop(struct rt_wlan_device *device) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    wlh_rtt_request_completion_t *completion;
    if (interface == RT_NULL)
        return -RT_EINVAL;
    if (wlh_rtt_request_begin(
            interface->host,
            interface,
            WLH_RTT_REQUEST_AP_STOP,
            &completion
        ) == 0u)
        return -RT_EBUSY;
    if (wlh_host_wifi_stop_ap(
            &interface->host->core, wlh_rtt_request_complete, completion
        ) != WLH_HOST_OK) {
        wlh_rtt_request_abort(completion);
        return -RT_ERROR;
    }
    return RT_EOK;
}

static int wlan_get_rssi(struct rt_wlan_device *device) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    return interface != RT_NULL && interface->active ? interface->info.rssi : 0;
}

static int wlan_get_info(
    struct rt_wlan_device *device, struct rt_wlan_info *info
) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    if (interface == RT_NULL || info == RT_NULL || !interface->active)
        return -RT_EIO;
    *info = interface->info;
    return RT_EOK;
}

static rt_err_t wlan_get_mac(
    struct rt_wlan_device *device, rt_uint8_t mac[6]
) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    if (interface == RT_NULL || mac == RT_NULL)
        return -RT_EINVAL;
    rt_memcpy(mac, interface->mac, sizeof(interface->mac));
    return RT_EOK;
}

static int wlan_send(struct rt_wlan_device *device, void *buffer, int length) {
    wlh_rtt_interface_t *interface = interface_from_device(device);
    wlh_host_result_t result;
    if (interface == RT_NULL || buffer == RT_NULL || length < 1 ||
        length > WLH_RTT_ETHERNET_MAX_FRAME || !interface->active ||
        interface->host->core.state != WLH_HOST_STATE_READY) {
        if (interface != RT_NULL)
            interface->stats.tx_dropped++;
        return -RT_EIO;
    }
    result = interface->id == WLH_RTT_INTERFACE_STA
                 ? wlh_host_ethernet_sta_send(
                       &interface->host->core, buffer, (size_t)length
                   )
                 : wlh_host_ethernet_ap_send(
                       &interface->host->core, buffer, (size_t)length
                   );
    if (result != WLH_HOST_OK) {
        interface->stats.tx_dropped++;
        return -RT_EIO;
    }
    interface->stats.tx_frames++;
    return RT_EOK;
}

static const struct rt_wlan_dev_ops station_ops = {
    .wlan_init = wlan_init,
    .wlan_mode = wlan_mode,
    .wlan_scan = wlan_scan,
    .wlan_join = wlan_join,
    .wlan_disconnect = wlan_disconnect,
    .wlan_scan_stop = wlan_scan_stop,
    .wlan_get_rssi = wlan_get_rssi,
    .wlan_get_info = wlan_get_info,
    .wlan_get_mac = wlan_get_mac,
    .wlan_send = wlan_send,
};

static const struct rt_wlan_dev_ops ap_ops = {
    .wlan_init = wlan_init,
    .wlan_mode = wlan_mode,
    .wlan_softap = wlan_softap,
    .wlan_ap_stop = wlan_ap_stop,
    .wlan_ap_get_info = wlan_get_info,
    .wlan_get_mac = wlan_get_mac,
    .wlan_send = wlan_send,
};

int wlh_rtt_wlan_register(wlh_rtt_host_impl_t *host) {
    wlh_rtt_interface_t *station = &host->interfaces[WLH_RTT_INTERFACE_STA];
    wlh_rtt_interface_t *ap = &host->interfaces[WLH_RTT_INTERFACE_AP];
    station->host = host;
    station->id = WLH_RTT_INTERFACE_STA;
    ap->host = host;
    ap->id = WLH_RTT_INTERFACE_AP;
#ifdef WLH_RTT_HOST_USING_STA
    if (rt_wlan_dev_register(
            &station->device,
            host->config.sta_device_name,
            &station_ops,
            RT_WLAN_FLAG_STA_ONLY,
            station
        ) != RT_EOK)
        return -RT_ERROR;
    station->registered = true;
#endif
#ifdef WLH_RTT_HOST_USING_SOFTAP
    if (rt_wlan_dev_register(
            &ap->device,
            host->config.ap_device_name,
            &ap_ops,
            RT_WLAN_FLAG_AP_ONLY,
            ap
        ) != RT_EOK)
        return -RT_ERROR;
    ap->registered = true;
#endif
    return RT_EOK;
}

void wlh_rtt_wlan_reset(wlh_rtt_host_impl_t *host) {
    unsigned id;
    for (id = 0u; id < WLH_RTT_INTERFACE_COUNT; ++id) {
        wlh_rtt_interface_t *interface = &host->interfaces[id];
        bool was_active = interface->active;
        wlh_rtt_request_kind_t pending = interface->request.kind;
        bool had_pending = interface->request.active;
        interface->active = false;
        interface->stats.active = false;
        rt_memset(&interface->request, 0, sizeof(interface->request));
        if (had_pending) {
            rt_wlan_dev_event_t event = RT_WLAN_DEV_EVT_DISCONNECT;
            if (pending == WLH_RTT_REQUEST_SCAN)
                event = RT_WLAN_DEV_EVT_SCAN_DONE;
            else if (pending == WLH_RTT_REQUEST_CONNECT)
                event = RT_WLAN_DEV_EVT_CONNECT_FAIL;
            else if (pending == WLH_RTT_REQUEST_AP_START ||
                     pending == WLH_RTT_REQUEST_AP_STOP)
                event = RT_WLAN_DEV_EVT_AP_STOP;
            wlh_rtt_report_event(interface, event, RT_NULL, 0);
        }
        if (was_active)
            wlh_rtt_report_event(
                interface,
                id == WLH_RTT_INTERFACE_STA ? RT_WLAN_DEV_EVT_DISCONNECT
                                             : RT_WLAN_DEV_EVT_AP_STOP,
                RT_NULL,
                0
            );
    }
    wlh_rtt_ap_client_clear(
        host->ap_client, WLH_RTT_HOST_AP_MAX_CLIENTS, &host->ap_clients
    );
}
