#include "wlh_rtt_internal.h"

#include <string.h>

#include <drivers/dev_sdio.h>
#include <ulog.h>

#include "wlh/protocol/wire.h"
#include "wlh_rtt_sdio_protocol.h"

#define WLH_SDIO_VENDOR 0x6666u
#define WLH_SDIO_PRODUCT 0x2222u
#define WLH_SDIO_FUNCTION 1u
#define WLH_SDIO_ADDRESS_MASK 0x3ffu
#define WLH_SDIO_TOKEN_MASK 0xfffu
#define WLH_SDIO_NEW_PACKET_BIT (1u << 23)
#define WLH_SDIO_REG_TOKEN 0x44u
#define WLH_SDIO_REG_INT_RAW 0x50u
#define WLH_SDIO_REG_PACKET_LEN 0x60u
#define WLH_SDIO_REG_HOST_TO_SLAVE 0x8cu
#define WLH_SDIO_REG_INT_CLEAR 0xd4u
#define WLH_SDIO_REG_HOST_INT_ENABLE 0xdcu
#define WLH_SDIO_RX_BURST 8u
#define WLH_SDIO_TX_BURST 8u
#define WLH_SDIO_INVALID_LIMIT 10u

typedef struct wlh_sdio_tx_job {
    uint8_t *frame;
    size_t length;
    wlh_rtt_tx_done_fn done;
    void *context;
    uint32_t generation;
} wlh_sdio_tx_job_t;

typedef struct wlh_sdio_transport {
    wlh_rtt_host_t *host;
    struct rt_sdio_function *function;
    rt_mutex_t lock;
    rt_sem_t wake;
    rt_sem_t probed;
    rt_mq_t tx_queue;
    rt_thread_t worker;
    uint8_t staging[WLH_RTT_SDIO_STAGING_SIZE];
    uint32_t rx_byte_count;
    uint32_t tx_buffer_count;
    uint32_t generation;
    unsigned invalid_streak;
    bool running;
    bool irq_attached;
    wlh_rtt_transport_stats_t stats;
} wlh_sdio_transport_t;

static wlh_sdio_transport_t transport;

static struct rt_sdio_device_id device_id = {
    .func_code = SDIO_ANY_FUNC_ID,
    .manufacturer = WLH_SDIO_VENDOR,
    .product = WLH_SDIO_PRODUCT,
};

static int read_register(uint32_t address, uint32_t *value) {
    rt_int32_t error = RT_EOK;
    *value = sdio_io_readl(
        transport.function, address & WLH_SDIO_ADDRESS_MASK, &error
    );
    return error;
}

static int write_register(uint32_t address, uint32_t value) {
    return sdio_io_writel(
        transport.function, value, address & WLH_SDIO_ADDRESS_MASK
    );
}

static int wait_for_token(void) {
    unsigned attempt;
    for (attempt = 0u; attempt < 200u && transport.running; ++attempt) {
        uint32_t token;
        int result = read_register(WLH_SDIO_REG_TOKEN, &token);
        if (result != RT_EOK)
            return result;
        token = (token >> 16) & WLH_SDIO_TOKEN_MASK;
        if (wlh_rtt_sdio_available_tokens(
                transport.tx_buffer_count, token
            ) != 0u) {
            transport.tx_buffer_count =
                (transport.tx_buffer_count + 1u) %
                WLH_RTT_SDIO_TOKEN_MODULUS;
            return RT_EOK;
        }
        rt_thread_mdelay(1u);
    }
    return -RT_ETIMEOUT;
}

static int write_frame(const uint8_t *frame, size_t length) {
    size_t transfer_size = wlh_rtt_sdio_padded_size(length);
    int result;
    if (transfer_size == 0u)
        return -RT_EINVAL;
    result = wait_for_token();
    if (result != RT_EOK)
        return result;
    rt_memset(transport.staging, 0, transfer_size);
    rt_memcpy(transport.staging, frame, length);
    return sdio_io_write_multi_incr_b(
        transport.function,
        wlh_rtt_sdio_window_address(length),
        transport.staging,
        (rt_uint32_t)transfer_size
    );
}

static int read_frame(size_t *length) {
    uint32_t interrupt_status;
    uint32_t packet_length;
    size_t transfer_size;
    int result = read_register(WLH_SDIO_REG_INT_RAW, &interrupt_status);
    if (result != RT_EOK)
        return result;
    if ((interrupt_status & WLH_SDIO_NEW_PACKET_BIT) == 0u)
        return -RT_EEMPTY;
    result = read_register(WLH_SDIO_REG_PACKET_LEN, &packet_length);
    if (result != RT_EOK)
        return result;
    if (wlh_rtt_sdio_accumulated_length(
            transport.rx_byte_count, packet_length, length
        ) != 0) {
        transport.stats.rx_invalid_length++;
        return -RT_EINVAL;
    }
    transfer_size = wlh_rtt_sdio_padded_size(*length);
    result = sdio_io_read_multi_incr_b(
        transport.function,
        wlh_rtt_sdio_window_address(*length),
        transport.staging,
        (rt_uint32_t)transfer_size
    );
    if (result != RT_EOK)
        return result;
    if (wlh_frame_validate(
            transport.staging, *length, WLH_RTT_SDIO_MAX_FRAME
        ) != WLH_WIRE_OK)
        return -RT_EINVAL;
    result = write_register(WLH_SDIO_REG_INT_CLEAR, interrupt_status);
    if (result != RT_EOK)
        return result;
    transport.rx_byte_count =
        packet_length & (WLH_RTT_SDIO_LENGTH_MODULUS - 1u);
    return RT_EOK;
}

static void drain_rx(void) {
    unsigned burst;
    for (burst = 0u; burst < WLH_SDIO_RX_BURST && transport.running; ++burst) {
        size_t length = 0u;
        int result = read_frame(&length);
        if (result == -RT_EEMPTY) {
            transport.invalid_streak = 0u;
            break;
        }
        if (result == -RT_EINVAL) {
            if (++transport.invalid_streak >= WLH_SDIO_INVALID_LIMIT) {
                transport.stats.rx_errors++;
                transport.invalid_streak = 0u;
                wlh_rtt_host_link_event(
                    transport.host, WLH_RTT_LINK_LOST
                );
                break;
            }
            rt_thread_mdelay(1u);
            continue;
        }
        if (result != RT_EOK) {
            transport.stats.rx_errors++;
            break;
        }
        transport.invalid_streak = 0u;
        if (wlh_rtt_host_input(
                transport.host, transport.staging, length
            ) != WLH_HOST_OK)
            transport.stats.rx_errors++;
        else
            transport.stats.rx_frames++;
    }
}

static void drain_tx(void) {
    unsigned burst;
    for (burst = 0u; burst < WLH_SDIO_TX_BURST && transport.running; ++burst) {
        wlh_sdio_tx_job_t job;
        int result;
        if (rt_mq_recv(
                transport.tx_queue, &job, sizeof(job), RT_WAITING_NO
            ) != RT_EOK)
            break;
        if (job.generation != transport.generation)
            result = -RT_EIO;
        else
            result = write_frame(job.frame, job.length);
        if (result == RT_EOK)
            transport.stats.tx_frames++;
        else
            transport.stats.tx_errors++;
        job.done(job.context, job.frame, job.length, result);
    }
}

static void worker_main(void *parameter) {
    (void)parameter;
    for (;;) {
        rt_sem_take(transport.wake, RT_WAITING_FOREVER);
        if (!transport.running)
            continue;
        rt_mutex_take(transport.lock, RT_WAITING_FOREVER);
        drain_rx();
        drain_tx();
        rt_mutex_release(transport.lock);
    }
}

static void sdio_irq(struct rt_sdio_function *function) {
    (void)function;
    rt_sem_release(transport.wake);
}

static rt_int32_t probe(struct rt_mmcsd_card *card) {
    struct rt_sdio_function *function;
    if (card == RT_NULL || card->cis.manufacturer != WLH_SDIO_VENDOR ||
        card->sdio_function_num < WLH_SDIO_FUNCTION)
        return -RT_EINVAL;
    function = card->sdio_function[WLH_SDIO_FUNCTION];
    if (function == RT_NULL || function->product != WLH_SDIO_PRODUCT)
        return -RT_EINVAL;
    transport.function = function;
    sdio_set_drvdata(function, &transport);
    rt_sem_release(transport.probed);
    return RT_EOK;
}

static rt_int32_t remove_card(struct rt_mmcsd_card *card) {
    (void)card;
    transport.running = false;
    transport.function = RT_NULL;
    wlh_rtt_host_link_event(transport.host, WLH_RTT_LINK_LOST);
    return RT_EOK;
}

static struct rt_sdio_driver driver = {
    .name = "wlh-sdio",
    .probe = probe,
    .remove = remove_card,
    .id = &device_id,
};

static int transport_start(void *context) {
    wlh_sdio_transport_t *sdio = context;
    uint32_t interrupt_enable = WLH_SDIO_NEW_PACKET_BIT;
    int result;
    if (sdio->function == RT_NULL &&
        rt_sem_take(sdio->probed, rt_tick_from_millisecond(2000)) != RT_EOK)
        return -RT_ETIMEOUT;
    result = sdio_enable_func(sdio->function);
    if (result == RT_EOK)
        result = sdio_set_block_size(sdio->function, WLH_RTT_SDIO_BLOCK_SIZE);
    if (result == RT_EOK && !sdio->irq_attached) {
        result = sdio_attach_irq(sdio->function, sdio_irq);
        if (result == RT_EOK)
            sdio->irq_attached = true;
    }
    if (result == RT_EOK)
        result = write_register(
            WLH_SDIO_REG_HOST_INT_ENABLE, interrupt_enable
        );
    if (result != RT_EOK)
        return result;
    sdio->rx_byte_count = 0u;
    sdio->tx_buffer_count = 0u;
    sdio->invalid_streak = 0u;
    sdio->generation++;
    sdio->running = true;
    rt_sem_release(sdio->wake);
    return RT_EOK;
}

static int transport_stop(void *context) {
    wlh_sdio_transport_t *sdio = context;
    wlh_sdio_tx_job_t job;
    sdio->running = false;
    sdio->generation++;
    while (rt_mq_recv(sdio->tx_queue, &job, sizeof(job), RT_WAITING_NO) ==
           RT_EOK) {
        sdio->stats.tx_errors++;
        job.done(job.context, job.frame, job.length, -RT_EIO);
    }
    return RT_EOK;
}

static int transport_submit_tx(
    void *context,
    uint8_t *frame,
    size_t length,
    wlh_rtt_tx_done_fn done,
    void *done_context
) {
    wlh_sdio_transport_t *sdio = context;
    wlh_sdio_tx_job_t job;
    if (!sdio->running || frame == RT_NULL || done == RT_NULL ||
        length < WLH_FRAME_HEADER_SIZE || length > WLH_RTT_SDIO_MAX_FRAME)
        return -RT_EINVAL;
    job.frame = frame;
    job.length = length;
    job.done = done;
    job.context = done_context;
    job.generation = sdio->generation;
    if (rt_mq_send(sdio->tx_queue, &job, sizeof(job)) != RT_EOK) {
        sdio->stats.tx_queue_full++;
        return -RT_EFULL;
    }
    rt_sem_release(sdio->wake);
    return RT_EOK;
}

static int transport_soft_reset(void *context) {
    wlh_sdio_transport_t *sdio = context;
    int result;
    if (sdio->function == RT_NULL)
        return -RT_EIO;
    rt_mutex_take(sdio->lock, RT_WAITING_FOREVER);
    result = write_register(WLH_SDIO_REG_HOST_TO_SLAVE, 1u);
    if (result == RT_EOK) {
        sdio->stats.bus_resets++;
        sdio->rx_byte_count = 0u;
        sdio->tx_buffer_count = 0u;
    }
    rt_mutex_release(sdio->lock);
    if (result == RT_EOK)
        rt_thread_mdelay(200u);
    return result;
}

static int transport_get_stats(
    void *context, wlh_rtt_transport_stats_t *stats
) {
    wlh_sdio_transport_t *sdio = context;
    if (stats == RT_NULL)
        return -RT_EINVAL;
    *stats = sdio->stats;
    return RT_EOK;
}

static const wlh_rtt_transport_ops_t operations = {
    .start = transport_start,
    .stop = transport_stop,
    .submit_tx = transport_submit_tx,
    .soft_reset = transport_soft_reset,
    .get_stats = transport_get_stats,
};

static void transport_release_resources(bool unregister_driver) {
    if (unregister_driver)
        (void)sdio_unregister_driver(&driver);
    if (transport.worker != RT_NULL)
        rt_thread_delete(transport.worker);
    if (transport.tx_queue != RT_NULL)
        rt_mq_delete(transport.tx_queue);
    if (transport.probed != RT_NULL)
        rt_sem_delete(transport.probed);
    if (transport.wake != RT_NULL)
        rt_sem_delete(transport.wake);
    if (transport.lock != RT_NULL)
        rt_mutex_delete(transport.lock);
    rt_memset(&transport, 0, sizeof(transport));
}

int wlh_rtt_sdio_transport_create(
    wlh_rtt_host_t *host,
    const wlh_rtt_transport_ops_t **ops,
    void **context
) {
    int result;
    if (host == RT_NULL || ops == RT_NULL || context == RT_NULL)
        return -RT_EINVAL;
    if (transport.lock != RT_NULL)
        return -RT_EBUSY;
    rt_memset(&transport, 0, sizeof(transport));
    transport.host = host;
    transport.lock = rt_mutex_create("wlh-sdio", RT_IPC_FLAG_PRIO);
    transport.wake = rt_sem_create("wlh-sdio", 0u, RT_IPC_FLAG_FIFO);
    transport.probed = rt_sem_create("wlh-probe", 0u, RT_IPC_FLAG_FIFO);
    transport.tx_queue = rt_mq_create(
        "wlh-sdio",
        sizeof(wlh_sdio_tx_job_t),
        WLH_RTT_HOST_TX_QUEUE_DEPTH,
        RT_IPC_FLAG_FIFO
    );
    transport.worker = rt_thread_create(
        "wlh-sdio",
        worker_main,
        &transport,
        WLH_RTT_HOST_SDIO_STACK_SIZE,
        9u,
        10u
    );
    if (transport.lock == RT_NULL || transport.wake == RT_NULL ||
        transport.probed == RT_NULL || transport.tx_queue == RT_NULL ||
        transport.worker == RT_NULL) {
        transport_release_resources(false);
        return -RT_ENOMEM;
    }
    rt_thread_startup(transport.worker);
    result = sdio_register_driver(&driver);
    if (result != RT_EOK && result != -RT_EEMPTY) {
        transport_release_resources(true);
        return result;
    }
    *ops = &operations;
    *context = &transport;
    return RT_EOK;
}
