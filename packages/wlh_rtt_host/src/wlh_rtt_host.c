#include "wlh_rtt_internal.h"

#include <string.h>

#include <ulog.h>

#ifdef WLH_RTT_HOST_USING_IPERF2
#include "wlh_rtt_iperf2.h"
#endif

_Static_assert(
    sizeof(wlh_rtt_host_impl_t) <= sizeof(wlh_rtt_host_t),
    "increase WLH_RTT_HOST_STORAGE_WORDS"
);

static wlh_rtt_host_t default_host;

rt_weak const wlh_rtt_board_ops_t *wlh_rtt_board_get_ops(void **context) {
    if (context != RT_NULL)
        *context = RT_NULL;
    return RT_NULL;
}

wlh_rtt_host_impl_t *wlh_rtt_impl(wlh_rtt_host_t *host) {
    return (wlh_rtt_host_impl_t *)(void *)host;
}

const wlh_rtt_host_impl_t *wlh_rtt_impl_const(const wlh_rtt_host_t *host) {
    return (const wlh_rtt_host_impl_t *)(const void *)host;
}

int wlh_rtt_host_init(
    wlh_rtt_host_t *public_host, const wlh_rtt_host_config_t *config
) {
    wlh_rtt_host_impl_t *host;
    if (public_host == RT_NULL || config == RT_NULL ||
#ifdef WLH_RTT_HOST_USING_STA
        config->sta_device_name == RT_NULL ||
#endif
#ifdef WLH_RTT_HOST_USING_SOFTAP
        config->ap_device_name == RT_NULL ||
#endif
        config->transport_ops == RT_NULL ||
        config->transport_ops->start == RT_NULL ||
        config->transport_ops->stop == RT_NULL ||
        config->transport_ops->submit_tx == RT_NULL)
        return -RT_EINVAL;
    rt_memset(public_host, 0, sizeof(*public_host));
    host = wlh_rtt_impl(public_host);
    host->magic = WLH_RTT_MAGIC;
    host->config = *config;
    host->lock = rt_mutex_create("wlh-host", RT_IPC_FLAG_PRIO);
    if (host->lock == RT_NULL)
        return -RT_ENOMEM;
    host->initialized = true;
    if (wlh_rtt_wlan_rx_init(host) != RT_EOK ||
        wlh_rtt_wlan_register(host) != RT_EOK ||
        wlh_rtt_lifecycle_init(host) != RT_EOK) {
        host->initialized = false;
        wlh_rtt_lifecycle_deinit(host);
        wlh_rtt_wlan_rx_deinit(host);
        rt_mutex_delete(host->lock);
        host->lock = RT_NULL;
        return -RT_ERROR;
    }
    return RT_EOK;
}

int wlh_rtt_host_start(wlh_rtt_host_t *public_host) {
    wlh_rtt_host_impl_t *host = wlh_rtt_impl(public_host);
    if (host == RT_NULL || host->magic != WLH_RTT_MAGIC || !host->initialized)
        return -RT_EINVAL;
    if (host->started)
        return -RT_EBUSY;
    host->wifi_initialized = false;
    if (wlh_host_start(&host->core) != WLH_HOST_OK)
        return -RT_ERROR;
    host->started = true;
    return RT_EOK;
}

int wlh_rtt_host_stop(wlh_rtt_host_t *public_host) {
    wlh_rtt_host_impl_t *host = wlh_rtt_impl(public_host);
    if (host == RT_NULL || host->magic != WLH_RTT_MAGIC || !host->started)
        return -RT_EINVAL;
    wlh_rtt_wlan_reset(host);
    host->wifi_initialized = false;
    if (wlh_host_stop(&host->core) != WLH_HOST_OK)
        return -RT_ERROR;
    host->started = false;
    return RT_EOK;
}

int wlh_rtt_host_recover(wlh_rtt_host_t *public_host) {
    wlh_rtt_host_impl_t *host = wlh_rtt_impl(public_host);
    int result;
    if (host == RT_NULL || host->magic != WLH_RTT_MAGIC || !host->started)
        return -RT_EINVAL;
    rt_mutex_take(host->lock, RT_WAITING_FOREVER);
    if (host->recovering) {
        rt_mutex_release(host->lock);
        return -RT_EBUSY;
    }
    host->recovering = true;
    rt_mutex_release(host->lock);

#ifdef WLH_RTT_HOST_USING_IPERF2
    (void)wlh_rtt_iperf2_stop();
#endif
    wlh_rtt_wlan_reset(host);
    result = wlh_host_stop(&host->core) == WLH_HOST_OK ? RT_EOK : -RT_ERROR;
    if (result == RT_EOK && host->config.transport_ops->soft_reset != RT_NULL)
        result = host->config.transport_ops->soft_reset(
            host->config.transport_context
        );
    host->wifi_initialized = false;
    if (result == RT_EOK && wlh_host_start(&host->core) != WLH_HOST_OK)
        result = -RT_ERROR;
    if (result == RT_EOK)
        host->recoveries++;
    else if (host->config.board_ops != RT_NULL &&
             host->config.board_ops->fatal_recovery != RT_NULL)
        host->config.board_ops->fatal_recovery(
            host->config.board_context, result
        );
    host->recovering = false;
    return result;
}

int wlh_rtt_host_input(
    wlh_rtt_host_t *public_host, const void *frame, size_t length
) {
    wlh_rtt_host_impl_t *host = wlh_rtt_impl(public_host);
    if (host == RT_NULL || host->magic != WLH_RTT_MAGIC || frame == RT_NULL)
        return -RT_EINVAL;
    return (int)wlh_host_on_frame(&host->core, frame, length);
}

void wlh_rtt_host_link_event(
    wlh_rtt_host_t *public_host, wlh_rtt_link_event_t event
) {
    wlh_rtt_host_impl_t *host = wlh_rtt_impl(public_host);
    if (host == RT_NULL || host->magic != WLH_RTT_MAGIC)
        return;
    if (event == WLH_RTT_LINK_LOST)
        wlh_host_transport_lost(&host->core);
}

int wlh_rtt_host_get_stats(
    wlh_rtt_host_t *public_host, wlh_rtt_stats_t *stats
) {
    wlh_rtt_host_impl_t *host = wlh_rtt_impl(public_host);
    wlh_host_diagnostics_t core_stats;
    if (host == RT_NULL || host->magic != WLH_RTT_MAGIC || stats == RT_NULL)
        return -RT_EINVAL;
    rt_memset(stats, 0, sizeof(*stats));
    wlh_host_get_diagnostics(&host->core, &core_stats);
    stats->host_state = core_stats.state;
    stats->session_id = core_stats.session_id;
    stats->recoveries = host->recoveries;
    stats->ap_clients = host->ap_clients;
    stats->sta = host->interfaces[WLH_RTT_INTERFACE_STA].stats;
    stats->ap = host->interfaces[WLH_RTT_INTERFACE_AP].stats;
    rt_memcpy(
        stats->ap_client,
        host->ap_client,
        sizeof(host->ap_client)
    );
    if (host->config.transport_ops->get_stats != RT_NULL)
        host->config.transport_ops->get_stats(
            host->config.transport_context, &stats->transport
        );
    return RT_EOK;
}

wlh_rtt_host_t *wlh_rtt_host_default(void) {
    return &default_host;
}

int wlh_rtt_host_default_start(void) {
#ifdef WLH_RTT_HOST_USING_SDIO
    wlh_rtt_host_config_t config;
    const wlh_rtt_transport_ops_t *transport_ops;
    const wlh_rtt_board_ops_t *board_ops;
    void *transport_context;
    void *board_context;
    int result;
    rt_memset(&config, 0, sizeof(config));
    result = wlh_rtt_sdio_transport_create(
        &default_host, &transport_ops, &transport_context
    );
    if (result != RT_EOK)
        return result;
    board_ops = wlh_rtt_board_get_ops(&board_context);
    config.sta_device_name = WLH_RTT_HOST_STA_DEVICE_NAME;
    config.ap_device_name = WLH_RTT_HOST_AP_DEVICE_NAME;
    config.transport_ops = transport_ops;
    config.transport_context = transport_context;
    config.board_ops = board_ops;
    config.board_context = board_context;
    result = wlh_rtt_host_init(&default_host, &config);
    if (result == RT_EOK)
        result = wlh_rtt_host_start(&default_host);
    if (result != RT_EOK)
        log_e("WL-hosted startup failed: %d", result);
    return result;
#else
    return -RT_ENOSYS;
#endif
}

#ifdef WLH_RTT_HOST_USING_SDIO
INIT_APP_EXPORT(wlh_rtt_host_default_start);
#endif
