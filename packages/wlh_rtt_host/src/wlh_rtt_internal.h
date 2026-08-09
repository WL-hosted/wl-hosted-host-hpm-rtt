#ifndef WLH_RTT_INTERNAL_H
#define WLH_RTT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <rtthread.h>
#include <dev_wlan.h>
#include <dev_wlan_mgnt.h>

#include "wlh/host.h"
#include "wlh/rtt_osal.h"
#include "wlh_rtt_host.h"
#include "wlh_rtt_logic.h"

#define WLH_RTT_MAGIC 0x574c4852u
#define WLH_RTT_INTERFACE_STA 0u
#define WLH_RTT_INTERFACE_AP 1u
#define WLH_RTT_INTERFACE_COUNT 2u
#define WLH_RTT_ETHERNET_MAX_FRAME 1600u
#define WLH_RTT_EXECUTOR_DEPTH 16u
#define WLH_RTT_LIFECYCLE_DEPTH 4u
#define WLH_RTT_REQUEST_COMPLETION_SLOTS 4u
#define WLH_RTT_RX_QUEUE_DEPTH \
    (WLH_RTT_HOST_RX_POOL_PER_INTERFACE * WLH_RTT_INTERFACE_COUNT)

typedef enum wlh_rtt_request_kind {
    WLH_RTT_REQUEST_NONE = 0,
    WLH_RTT_REQUEST_SCAN,
    WLH_RTT_REQUEST_CONNECT,
    WLH_RTT_REQUEST_DISCONNECT,
    WLH_RTT_REQUEST_AP_START,
    WLH_RTT_REQUEST_AP_STOP
} wlh_rtt_request_kind_t;

typedef struct wlh_rtt_request {
    wlh_rtt_request_kind_t kind;
    uint32_t generation;
    bool active;
} wlh_rtt_request_t;

struct wlh_rtt_interface;

typedef struct wlh_rtt_request_completion {
    struct wlh_rtt_interface *interface;
    wlh_rtt_request_kind_t kind;
    uint32_t generation;
    bool in_use;
} wlh_rtt_request_completion_t;

typedef struct wlh_rtt_interface {
    struct rt_wlan_device device;
    struct wlh_rtt_host_impl *host;
    uint8_t id;
    bool active;
    bool registered;
    uint8_t mac[6];
    struct rt_wlan_info info;
    wlh_rtt_request_t request;
    wlh_rtt_request_completion_t
        completions[WLH_RTT_REQUEST_COMPLETION_SLOTS];
    uint8_t *rx_storage;
    uint64_t rx_free_mask;
    wlh_rtt_interface_stats_t stats;
} wlh_rtt_interface_t;

typedef struct wlh_rtt_executor_job {
    wlh_task_fn task;
    void *context;
} wlh_rtt_executor_job_t;

typedef struct wlh_rtt_lifecycle_job {
    bool start;
    wlh_transport_lifecycle_complete_fn completion;
    void *context;
} wlh_rtt_lifecycle_job_t;

typedef struct wlh_rtt_rx_job {
    uint8_t interface_id;
    uint8_t block;
    uint16_t length;
} wlh_rtt_rx_job_t;

typedef struct wlh_rtt_host_impl {
    uint32_t magic;
    wlh_rtt_host_config_t config;
    wlh_host_t core;
    wlh_rtt_osal_t osal;
    rt_mutex_t lock;
    rt_mq_t executor_queue;
    rt_mq_t lifecycle_queue;
    rt_mq_t rx_queue;
    rt_thread_t executor_thread;
    rt_thread_t lifecycle_thread;
    rt_thread_t rx_thread;
    wlh_rtt_interface_t interfaces[WLH_RTT_INTERFACE_COUNT];
    uint32_t next_generation;
    uint32_t recoveries;
    uint32_t ap_clients;
    wlh_rtt_ap_client_stats_t ap_client[WLH_RTT_HOST_AP_MAX_CLIENTS];
    bool initialized;
    bool started;
    bool wifi_initialized;
    bool recovering;
} wlh_rtt_host_impl_t;

wlh_rtt_host_impl_t *wlh_rtt_impl(wlh_rtt_host_t *host);
const wlh_rtt_host_impl_t *wlh_rtt_impl_const(const wlh_rtt_host_t *host);

int wlh_rtt_lifecycle_init(wlh_rtt_host_impl_t *host);
void wlh_rtt_lifecycle_deinit(wlh_rtt_host_impl_t *host);
int wlh_rtt_wlan_register(wlh_rtt_host_impl_t *host);
void wlh_rtt_wlan_reset(wlh_rtt_host_impl_t *host);
void wlh_rtt_wlan_event(
    wlh_rtt_host_impl_t *host, const wlh_host_event_t *event
);
int wlh_rtt_wlan_rx_init(wlh_rtt_host_impl_t *host);
void wlh_rtt_wlan_rx_deinit(wlh_rtt_host_impl_t *host);
void wlh_rtt_wlan_rx_enqueue(
    wlh_rtt_host_impl_t *host,
    uint8_t interface_id,
    const uint8_t *frame,
    size_t length
);
void wlh_rtt_request_complete(
    void *context,
    wlh_host_result_t result,
    uint16_t status_domain,
    int16_t status_code,
    const uint8_t *payload,
    size_t payload_size
);
uint32_t wlh_rtt_request_begin(
    wlh_rtt_host_impl_t *host,
    wlh_rtt_interface_t *interface,
    wlh_rtt_request_kind_t kind,
    wlh_rtt_request_completion_t **completion
);
void wlh_rtt_request_abort(wlh_rtt_request_completion_t *completion);
void wlh_rtt_request_finish(
    wlh_rtt_interface_t *interface, wlh_rtt_request_kind_t kind
);
void wlh_rtt_report_event(
    wlh_rtt_interface_t *interface,
    rt_wlan_dev_event_t event,
    void *data,
    int length
);
rt_wlan_security_t wlh_rtt_security_from_wire(uint32_t security);
int wlh_rtt_security_to_wire(rt_wlan_security_t security, uint32_t *wire);

#endif
