#include "wlh_rtt_internal.h"

#include <stdlib.h>
#include <string.h>

#include <ulog.h>

static void executor_main(void *parameter) {
    wlh_rtt_host_impl_t *host = parameter;
    wlh_rtt_executor_job_t job;
    while (host->initialized) {
        if (rt_mq_recv(
                host->executor_queue,
                &job,
                sizeof(job),
                RT_WAITING_FOREVER
            ) == RT_EOK &&
            job.task != RT_NULL)
            job.task(job.context);
    }
}

static void lifecycle_main(void *parameter) {
    wlh_rtt_host_impl_t *host = parameter;
    wlh_rtt_lifecycle_job_t job;
    while (host->initialized) {
        int result;
        if (rt_mq_recv(
                host->lifecycle_queue,
                &job,
                sizeof(job),
                RT_WAITING_FOREVER
            ) != RT_EOK)
            continue;
        result = job.start ? host->config.transport_ops->start(
                                 host->config.transport_context
                             )
                           : host->config.transport_ops->stop(
                                 host->config.transport_context
                             );
        job.completion(job.context, result);
    }
}

static int executor_post(
    void *context, wlh_task_fn task, void *task_context
) {
    wlh_rtt_host_impl_t *host = context;
    wlh_rtt_executor_job_t job = {task, task_context};
    if (task == RT_NULL || host == RT_NULL)
        return -1;
    return rt_mq_send(host->executor_queue, &job, sizeof(job)) == RT_EOK ? 0
                                                                         : -1;
}

static int submit_lifecycle(
    wlh_rtt_host_impl_t *host,
    bool start,
    wlh_transport_lifecycle_complete_fn completion,
    void *context
) {
    wlh_rtt_lifecycle_job_t job = {start, completion, context};
    if (completion == RT_NULL)
        return -1;
    return rt_mq_send(host->lifecycle_queue, &job, sizeof(job)) == RT_EOK ? 0
                                                                          : -1;
}

static int core_transport_start(
    void *context,
    wlh_transport_lifecycle_complete_fn completion,
    void *completion_context
) {
    return submit_lifecycle(context, true, completion, completion_context);
}

static int core_transport_stop(
    void *context,
    wlh_transport_lifecycle_complete_fn completion,
    void *completion_context
) {
    return submit_lifecycle(context, false, completion, completion_context);
}

static int core_transport_tx(
    void *context,
    uint8_t *frame,
    size_t size,
    wlh_transport_tx_complete_fn completion,
    void *completion_context
) {
    wlh_rtt_host_impl_t *host = context;
    return host->config.transport_ops->submit_tx(
        host->config.transport_context,
        frame,
        size,
        completion,
        completion_context
    );
}

static uint8_t *buffer_alloc(void *context, size_t size) {
    (void)context;
    return rt_malloc(size);
}

static void buffer_free(void *context, uint8_t *buffer) {
    (void)context;
    rt_free(buffer);
}

static void wifi_initialize_complete(
    void *context,
    wlh_host_result_t result,
    uint16_t status_domain,
    int16_t status_code,
    const uint8_t *payload,
    size_t payload_size
) {
    wlh_rtt_host_impl_t *host = context;
    (void)status_domain;
    (void)status_code;
    (void)payload;
    (void)payload_size;
    if (result != WLH_HOST_OK) {
        log_e("WL-hosted APSTA initialization failed: %d", (int)result);
        return;
    }
    host->wifi_initialized = true;
#ifdef WLH_RTT_HOST_USING_STA
    if (rt_wlan_set_mode(
            host->config.sta_device_name, RT_WLAN_STATION
        ) != RT_EOK)
        log_e("failed to set %s station mode", host->config.sta_device_name);
#endif
#ifdef WLH_RTT_HOST_USING_SOFTAP
    if (rt_wlan_set_mode(host->config.ap_device_name, RT_WLAN_AP) != RT_EOK)
        log_e("failed to set %s AP mode", host->config.ap_device_name);
#endif
}

static void host_event(void *context, const wlh_host_event_t *event) {
    wlh_rtt_host_impl_t *host = context;
    if (event->kind == WLH_HOST_EVENT_STATE_CHANGED &&
        event->state == WLH_HOST_STATE_READY && !host->wifi_initialized) {
        uint32_t flags = 0u;
#ifdef WLH_RTT_HOST_USING_STA
        flags |= WLH_WIFI_INTERFACE_FLAG_STA;
#endif
#ifdef WLH_RTT_HOST_USING_SOFTAP
        flags |= WLH_WIFI_INTERFACE_FLAG_AP;
#endif
        if (wlh_host_wifi_initialize_interfaces(
                &host->core, flags, wifi_initialize_complete, host
            ) != WLH_HOST_OK)
            log_e("failed to submit APSTA initialization");
    }
    wlh_rtt_wlan_event(host, event);
}

int wlh_rtt_lifecycle_init(wlh_rtt_host_impl_t *host) {
    wlh_host_config_t core_config;
    rt_memset(&core_config, 0, sizeof(core_config));

    host->executor_queue = rt_mq_create(
        "wlh-exec",
        sizeof(wlh_rtt_executor_job_t),
        WLH_RTT_EXECUTOR_DEPTH,
        RT_IPC_FLAG_FIFO
    );
    host->lifecycle_queue = rt_mq_create(
        "wlh-life",
        sizeof(wlh_rtt_lifecycle_job_t),
        WLH_RTT_LIFECYCLE_DEPTH,
        RT_IPC_FLAG_FIFO
    );
    if (host->executor_queue == RT_NULL || host->lifecycle_queue == RT_NULL)
        return -RT_ENOMEM;

    host->executor_thread = rt_thread_create(
        "wlh-exec",
        executor_main,
        host,
        3072u,
        12u,
        10u
    );
    host->lifecycle_thread = rt_thread_create(
        "wlh-life",
        lifecycle_main,
        host,
        WLH_RTT_HOST_SDIO_STACK_SIZE,
        11u,
        10u
    );
    if (host->executor_thread == RT_NULL || host->lifecycle_thread == RT_NULL)
        return -RT_ENOMEM;
    rt_thread_startup(host->executor_thread);
    rt_thread_startup(host->lifecycle_thread);

    wlh_rtt_osal_init(&host->osal);
    core_config.transport.context = host;
    core_config.transport.start = core_transport_start;
    core_config.transport.stop = core_transport_stop;
    core_config.transport.submit_tx = core_transport_tx;
    core_config.buffers.context = host;
    core_config.buffers.alloc = buffer_alloc;
    core_config.buffers.free = buffer_free;
    core_config.osal = wlh_rtt_osal_ops(&host->osal);
    core_config.executor.context = host;
    core_config.executor.post = executor_post;
    core_config.on_event = host_event;
    core_config.event_context = host;
    core_config.max_frame_size = 4092u;
    core_config.rpc_timeout_ms = WLH_RTT_HOST_RPC_TIMEOUT_MS;
    core_config.heartbeat_timeout_ms = 5000u;
    core_config.max_pending_rpc = WLH_HOST_MAX_PENDING;
    core_config.core_queue_depth = WLH_RTT_HOST_TX_QUEUE_DEPTH;
    core_config.ethernet_tx_depth = WLH_RTT_HOST_TX_QUEUE_DEPTH;
    core_config.ethernet_tx_aggregation_limit = 4u;
    core_config.stop_timeout_ms = 5000u;
    core_config.core_task.name = "wlh-core";
    core_config.core_task.stack_size = WLH_RTT_HOST_CORE_STACK_SIZE;
    core_config.core_task.priority = RT_THREAD_PRIORITY_MAX - 10;
    return wlh_host_init(&host->core, &core_config) == WLH_HOST_OK ? 0
                                                                  : -RT_ERROR;
}

void wlh_rtt_lifecycle_deinit(wlh_rtt_host_impl_t *host) {
    if (host->executor_thread != RT_NULL)
        rt_thread_delete(host->executor_thread);
    if (host->lifecycle_thread != RT_NULL)
        rt_thread_delete(host->lifecycle_thread);
    if (host->executor_queue != RT_NULL)
        rt_mq_delete(host->executor_queue);
    if (host->lifecycle_queue != RT_NULL)
        rt_mq_delete(host->lifecycle_queue);
    host->executor_thread = RT_NULL;
    host->lifecycle_thread = RT_NULL;
    host->executor_queue = RT_NULL;
    host->lifecycle_queue = RT_NULL;
}
