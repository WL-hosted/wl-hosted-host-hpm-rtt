#include "wlh_rtt_internal.h"

#include <string.h>

static int allocate_block(wlh_rtt_interface_t *interface) {
    int index;
    rt_base_t level = rt_hw_interrupt_disable();
    index = wlh_rtt_pool_take(
        &interface->rx_free_mask, WLH_RTT_HOST_RX_POOL_PER_INTERFACE
    );
    rt_hw_interrupt_enable(level);
    return index;
}

static void free_block(wlh_rtt_interface_t *interface, unsigned index) {
    rt_base_t level = rt_hw_interrupt_disable();
    wlh_rtt_pool_put(&interface->rx_free_mask, index);
    rt_hw_interrupt_enable(level);
}

static void rx_main(void *parameter) {
    wlh_rtt_host_impl_t *host = parameter;
    wlh_rtt_rx_job_t job;
    while (host->initialized) {
        wlh_rtt_interface_t *interface;
        uint8_t *frame;
        if (rt_mq_recv(
                host->rx_queue, &job, sizeof(job), RT_WAITING_FOREVER
            ) != RT_EOK ||
            job.interface_id >= WLH_RTT_INTERFACE_COUNT)
            continue;
        interface = &host->interfaces[job.interface_id];
        frame = interface->rx_storage +
                (size_t)job.block * WLH_RTT_ETHERNET_MAX_FRAME;
        if (!interface->active ||
            rt_wlan_dev_report_data(
                &interface->device, frame, (int)job.length
            ) != RT_EOK)
            interface->stats.rx_delivery_failed++;
        else
            interface->stats.rx_frames++;
        free_block(interface, job.block);
    }
}

int wlh_rtt_wlan_rx_init(wlh_rtt_host_impl_t *host) {
    unsigned id;
    host->rx_queue = rt_mq_create(
        "wlh-rx",
        sizeof(wlh_rtt_rx_job_t),
        WLH_RTT_RX_QUEUE_DEPTH,
        RT_IPC_FLAG_FIFO
    );
    if (host->rx_queue == RT_NULL)
        return -RT_ENOMEM;
    for (id = 0u; id < WLH_RTT_INTERFACE_COUNT; ++id) {
        wlh_rtt_interface_t *interface = &host->interfaces[id];
        interface->rx_storage = rt_malloc(
            (size_t)WLH_RTT_HOST_RX_POOL_PER_INTERFACE *
            WLH_RTT_ETHERNET_MAX_FRAME
        );
        if (interface->rx_storage == RT_NULL)
            return -RT_ENOMEM;
        interface->rx_free_mask = wlh_rtt_pool_initial_mask(
            WLH_RTT_HOST_RX_POOL_PER_INTERFACE
        );
    }
    host->rx_thread = rt_thread_create(
        "wlh-rx",
        rx_main,
        host,
        WLH_RTT_HOST_RX_STACK_SIZE,
        13u,
        10u
    );
    if (host->rx_thread == RT_NULL)
        return -RT_ENOMEM;
    return rt_thread_startup(host->rx_thread);
}

void wlh_rtt_wlan_rx_deinit(wlh_rtt_host_impl_t *host) {
    unsigned id;
    if (host->rx_thread != RT_NULL)
        rt_thread_delete(host->rx_thread);
    if (host->rx_queue != RT_NULL)
        rt_mq_delete(host->rx_queue);
    for (id = 0u; id < WLH_RTT_INTERFACE_COUNT; ++id) {
        rt_free(host->interfaces[id].rx_storage);
        host->interfaces[id].rx_storage = RT_NULL;
    }
    host->rx_thread = RT_NULL;
    host->rx_queue = RT_NULL;
}

void wlh_rtt_wlan_rx_enqueue(
    wlh_rtt_host_impl_t *host,
    uint8_t interface_id,
    const uint8_t *frame,
    size_t length
) {
    wlh_rtt_interface_t *interface;
    wlh_rtt_rx_job_t job;
    int block;
    if (interface_id >= WLH_RTT_INTERFACE_COUNT || frame == RT_NULL ||
        length < 1u || length > WLH_RTT_ETHERNET_MAX_FRAME)
        return;
    interface = &host->interfaces[interface_id];
    block = allocate_block(interface);
    if (block < 0) {
        interface->stats.rx_pool_empty++;
        return;
    }
    rt_memcpy(
        interface->rx_storage +
            (size_t)block * WLH_RTT_ETHERNET_MAX_FRAME,
        frame,
        length
    );
    job.interface_id = interface_id;
    job.block = (uint8_t)block;
    job.length = (uint16_t)length;
    if (rt_mq_send(host->rx_queue, &job, sizeof(job)) != RT_EOK) {
        interface->stats.rx_queue_full++;
        free_block(interface, (unsigned)block);
    }
}
