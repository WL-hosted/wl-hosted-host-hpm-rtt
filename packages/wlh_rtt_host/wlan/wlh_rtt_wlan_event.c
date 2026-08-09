#include "wlh_rtt_internal.h"

#include <string.h>

#include "wifi.pb.h"
#include <lwip/netif.h>
#include <netdev.h>
#include <pb_decode.h>

static void update_interface_mac(
    wlh_rtt_interface_t *interface, const uint8_t mac[6]
) {
    struct netdev *netdev;
    struct netif *netif;
    rt_memcpy(interface->mac, mac, sizeof(interface->mac));
    netdev = interface->device.netdev;
    if (netdev == RT_NULL)
        return;
    netdev->hwaddr_len = sizeof(interface->mac);
    rt_memcpy(netdev->hwaddr, mac, sizeof(interface->mac));
    netif = netdev->user_data;
    if (netif != RT_NULL) {
        netif->hwaddr_len = sizeof(interface->mac);
        rt_memcpy(netif->hwaddr, mac, sizeof(interface->mac));
    }
}

static void link_to_info(
    struct rt_wlan_info *info, const wlh_protocol_v1_WifiLinkInfo *link
) {
    rt_memset(info, 0, sizeof(*info));
    info->security = wlh_rtt_security_from_wire(link->security);
    info->band = link->band ==
                         wlh_protocol_v1_WifiBand_WIFI_BAND_2_4_GHZ
                     ? RT_802_11_BAND_2_4GHZ
                     : RT_802_11_BAND_5GHZ;
    info->channel = (rt_int16_t)link->channel;
    info->rssi = (rt_int16_t)link->rssi_dbm;
    info->ssid.len = (rt_uint8_t)link->ssid.size;
    rt_memcpy(info->ssid.val, link->ssid.bytes, link->ssid.size);
    if (link->bssid.size == sizeof(info->bssid))
        rt_memcpy(info->bssid, link->bssid.bytes, sizeof(info->bssid));
}

static void report_scan_result(
    wlh_rtt_host_impl_t *host, const uint8_t *payload, size_t payload_size
) {
    wlh_protocol_v1_WifiScanResultEvent decoded =
        wlh_protocol_v1_WifiScanResultEvent_init_zero;
    wlh_rtt_interface_t *interface =
        &host->interfaces[WLH_RTT_INTERFACE_STA];
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_size);
    pb_size_t index;
    if (!pb_decode(
            &stream, wlh_protocol_v1_WifiScanResultEvent_fields, &decoded
        ) ||
        !interface->request.active ||
        interface->request.kind != WLH_RTT_REQUEST_SCAN ||
        decoded.scan_id != interface->request.generation)
        return;
    for (index = 0; index < decoded.networks_count; ++index) {
        const wlh_protocol_v1_WifiNetwork *network = &decoded.networks[index];
        struct rt_wlan_info info;
        rt_memset(&info, 0, sizeof(info));
        info.security = wlh_rtt_security_from_wire(network->security);
        info.band = network->band ==
                            wlh_protocol_v1_WifiBand_WIFI_BAND_2_4_GHZ
                        ? RT_802_11_BAND_2_4GHZ
                        : RT_802_11_BAND_5GHZ;
        info.channel = (rt_int16_t)network->channel;
        info.rssi = (rt_int16_t)network->rssi_dbm;
        info.ssid.len = (rt_uint8_t)network->ssid.size;
        rt_memcpy(info.ssid.val, network->ssid.bytes, network->ssid.size);
        if (network->bssid.size == sizeof(info.bssid))
            rt_memcpy(info.bssid, network->bssid.bytes, sizeof(info.bssid));
        wlh_rtt_report_event(
            interface, RT_WLAN_DEV_EVT_SCAN_REPORT, &info, sizeof(info)
        );
    }
}

static void report_scan_complete(
    wlh_rtt_host_impl_t *host, const uint8_t *payload, size_t payload_size
) {
    wlh_protocol_v1_WifiScanCompletedEvent decoded =
        wlh_protocol_v1_WifiScanCompletedEvent_init_zero;
    wlh_rtt_interface_t *interface =
        &host->interfaces[WLH_RTT_INTERFACE_STA];
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_size);
    if (!pb_decode(
            &stream, wlh_protocol_v1_WifiScanCompletedEvent_fields, &decoded
        ) ||
        !interface->request.active ||
        interface->request.kind != WLH_RTT_REQUEST_SCAN ||
        decoded.scan_id != interface->request.generation)
        return;
    wlh_rtt_request_finish(interface, WLH_RTT_REQUEST_SCAN);
    wlh_rtt_report_event(interface, RT_WLAN_DEV_EVT_SCAN_DONE, RT_NULL, 0);
}

static void report_connected(
    wlh_rtt_host_impl_t *host, const uint8_t *payload, size_t payload_size
) {
    wlh_protocol_v1_WifiConnectedEvent decoded =
        wlh_protocol_v1_WifiConnectedEvent_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_size);
    wlh_rtt_interface_t *interface;
    wlh_rtt_interface_route_t route;
    if (!pb_decode(
            &stream, wlh_protocol_v1_WifiConnectedEvent_fields, &decoded
        ) ||
        !decoded.has_link)
        return;
    route = wlh_rtt_route_wire_interface(
        decoded.link.interface,
        wlh_protocol_v1_WifiInterface_WIFI_INTERFACE_STA,
        wlh_protocol_v1_WifiInterface_WIFI_INTERFACE_AP
    );
    if (route == WLH_RTT_ROUTE_INVALID)
        return;
    interface = &host->interfaces[(unsigned)route];
    link_to_info(&interface->info, &decoded.link);
    if (decoded.link.mac.size == sizeof(interface->mac))
        update_interface_mac(interface, decoded.link.mac.bytes);
    interface->active = true;
    interface->stats.active = true;
    wlh_rtt_request_finish(
        interface,
        interface->id == WLH_RTT_INTERFACE_STA ? WLH_RTT_REQUEST_CONNECT
                                                : WLH_RTT_REQUEST_AP_START
    );
    wlh_rtt_report_event(
        interface,
        interface->id == WLH_RTT_INTERFACE_STA ? RT_WLAN_DEV_EVT_CONNECT
                                                : RT_WLAN_DEV_EVT_AP_START,
        &interface->info,
        sizeof(interface->info)
    );
}

static void report_disconnected(
    wlh_rtt_host_impl_t *host, const uint8_t *payload, size_t payload_size
) {
    wlh_protocol_v1_WifiDisconnectedEvent decoded =
        wlh_protocol_v1_WifiDisconnectedEvent_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_size);
    wlh_rtt_interface_t *interface;
    wlh_rtt_interface_route_t route;
    if (!pb_decode(
            &stream, wlh_protocol_v1_WifiDisconnectedEvent_fields, &decoded
        ))
        return;
    route = wlh_rtt_route_wire_interface(
        decoded.interface,
        wlh_protocol_v1_WifiInterface_WIFI_INTERFACE_STA,
        wlh_protocol_v1_WifiInterface_WIFI_INTERFACE_AP
    );
    if (route == WLH_RTT_ROUTE_INVALID)
        return;
    interface = &host->interfaces[(unsigned)route];
    interface->active = false;
    interface->stats.active = false;
    wlh_rtt_request_finish(
        interface,
        interface->id == WLH_RTT_INTERFACE_STA ? WLH_RTT_REQUEST_DISCONNECT
                                                : WLH_RTT_REQUEST_AP_STOP
    );
    if (interface->id == WLH_RTT_INTERFACE_AP)
        host->ap_clients = 0u;
    wlh_rtt_report_event(
        interface,
        interface->id == WLH_RTT_INTERFACE_STA ? RT_WLAN_DEV_EVT_DISCONNECT
                                                : RT_WLAN_DEV_EVT_AP_STOP,
        RT_NULL,
        0
    );
}

static void report_ap_client(
    wlh_rtt_host_impl_t *host,
    const wlh_host_event_t *event,
    bool joined
) {
    struct rt_wlan_info info;
    uint8_t mac[6];
    int16_t rssi = 0;
    uint32_t association_id = 0u;
    uint32_t ieee80211_reason = 0u;
    pb_istream_t stream =
        pb_istream_from_buffer(event->payload, event->payload_size);
    rt_memset(&info, 0, sizeof(info));
    if (joined) {
        wlh_protocol_v1_WifiApClientJoinedEvent decoded =
            wlh_protocol_v1_WifiApClientJoinedEvent_init_zero;
        if (!pb_decode(
                &stream,
                wlh_protocol_v1_WifiApClientJoinedEvent_fields,
                &decoded
            ) ||
            !decoded.has_client || decoded.client.mac.size != 6u)
            return;
        rt_memcpy(mac, decoded.client.mac.bytes, sizeof(mac));
        rssi = (int16_t)decoded.client.rssi_dbm;
        association_id = decoded.client.association_id;
    } else {
        wlh_protocol_v1_WifiApClientLeftEvent decoded =
            wlh_protocol_v1_WifiApClientLeftEvent_init_zero;
        if (!pb_decode(
                &stream,
                wlh_protocol_v1_WifiApClientLeftEvent_fields,
                &decoded
            ) ||
            decoded.mac.size != 6u)
            return;
        rt_memcpy(mac, decoded.mac.bytes, sizeof(mac));
        association_id = decoded.association_id;
        ieee80211_reason = decoded.ieee80211_reason;
    }
    (void)wlh_rtt_ap_client_update(
        host->ap_client,
        WLH_RTT_HOST_AP_MAX_CLIENTS,
        mac,
        joined,
        rssi,
        association_id,
        ieee80211_reason,
        &host->ap_clients
    );
    rt_memcpy(info.bssid, mac, sizeof(mac));
    info.rssi = rssi;
    wlh_rtt_report_event(
        &host->interfaces[WLH_RTT_INTERFACE_AP],
        joined ? RT_WLAN_DEV_EVT_AP_ASSOCIATED
               : RT_WLAN_DEV_EVT_AP_DISASSOCIATED,
        &info,
        sizeof(info)
    );
}

void wlh_rtt_wlan_event(
    wlh_rtt_host_impl_t *host, const wlh_host_event_t *event
) {
    switch (event->kind) {
    case WLH_HOST_EVENT_WIFI_SCAN_RESULT:
        report_scan_result(host, event->payload, event->payload_size);
        break;
    case WLH_HOST_EVENT_WIFI_SCAN_COMPLETED:
        report_scan_complete(host, event->payload, event->payload_size);
        break;
    case WLH_HOST_EVENT_WIFI_CONNECTED:
        report_connected(host, event->payload, event->payload_size);
        break;
    case WLH_HOST_EVENT_WIFI_DISCONNECTED:
        report_disconnected(host, event->payload, event->payload_size);
        break;
    case WLH_HOST_EVENT_WIFI_AP_CLIENT_JOINED:
        report_ap_client(host, event, true);
        break;
    case WLH_HOST_EVENT_WIFI_AP_CLIENT_LEFT:
        report_ap_client(host, event, false);
        break;
    case WLH_HOST_EVENT_ETHERNET_STA_RX:
        wlh_rtt_wlan_rx_enqueue(
            host, WLH_RTT_INTERFACE_STA, event->payload, event->payload_size
        );
        break;
    case WLH_HOST_EVENT_ETHERNET_AP_RX:
        wlh_rtt_wlan_rx_enqueue(
            host, WLH_RTT_INTERFACE_AP, event->payload, event->payload_size
        );
        break;
    default:
        break;
    }
}
