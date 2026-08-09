#define LOG_TAG "cmd.test"

#include "cmd_test.hpp"
#include <rtconfig.h>

#ifdef BSP_USING_PSRAM

#include "drv_psram.h"

#include <finsh.h>
#include <rtthread.h>
#include <ulog.h>

#include <cstdint>
#include <mutex>

namespace {

constexpr rt_size_t kPayloadSize = 4096U;
constexpr rt_size_t kHeaderSize = sizeof(rt_uint8_t *);
constexpr rt_size_t kStride = kPayloadSize + kHeaderSize;
constexpr rt_size_t kExpectedBlockCount = (8U * 1024U * 1024U) / kStride;
constexpr rt_size_t kExpectedTailSize =
    (8U * 1024U * 1024U) - kExpectedBlockCount * kStride;

static_assert(kExpectedBlockCount == 2046U);
static_assert(kExpectedTailSize == 8U);

std::mutex g_psram_test_mutex;

enum class Pattern {
    Address,
    InverseAddress,
    Checkerboard,
};

uint32_t expected_word(uintptr_t address, Pattern pattern) {
    const uint32_t word_address = static_cast<uint32_t>(address >> 2U);
    switch (pattern) {
    case Pattern::Address:
        return word_address ^ 0xA5C35A3CU;
    case Pattern::InverseAddress:
        return ~(word_address ^ 0xA5C35A3CU);
    case Pattern::Checkerboard:
        return (word_address & 1U) ? 0x55AA55AAU : 0xAA55AA55U;
    }
    return 0U;
}

const char *pattern_name(Pattern pattern) {
    switch (pattern) {
    case Pattern::Address:
        return "address";
    case Pattern::InverseAddress:
        return "inverse-address";
    case Pattern::Checkerboard:
        return "checkerboard";
    }
    return "unknown";
}

void sync_full_psram_cache() {
    psram_cache_sync();
}

bool run_pattern(void **blocks, rt_size_t block_count, Pattern pattern) {
    log_i("PSRAM test pattern: %s", pattern_name(pattern));
    for (rt_size_t block = 0; block < block_count; ++block) {
        auto *words = static_cast<volatile uint32_t *>(blocks[block]);
        for (rt_size_t word = 0; word < kPayloadSize / sizeof(uint32_t);
             ++word) {
            const uintptr_t address = reinterpret_cast<uintptr_t>(&words[word]);
            words[word] = expected_word(address, pattern);
        }
        if ((block & 0x1FU) == 0U) {
            rt_thread_yield();
        }
    }

    sync_full_psram_cache();

    for (rt_size_t block = 0; block < block_count; ++block) {
        auto *words = static_cast<volatile uint32_t *>(blocks[block]);
        for (rt_size_t word = 0; word < kPayloadSize / sizeof(uint32_t);
             ++word) {
            const uintptr_t address = reinterpret_cast<uintptr_t>(&words[word]);
            const uint32_t expected = expected_word(address, pattern);
            if (words[word] != expected) {
                log_e(
                    "PSRAM %s mismatch at 0x%08lx: got=0x%08lx "
                    "expected=0x%08lx",
                    pattern_name(pattern),
                    (unsigned long)address,
                    (unsigned long)words[word],
                    (unsigned long)expected
                );
                return false;
            }
        }
        if ((block & 0x1FU) == 0U) {
            rt_thread_yield();
        }
    }
    return true;
}

bool test_tail_bytes() {
    const uintptr_t tail_address =
        psram_get_base() + kExpectedBlockCount * kStride;
    auto *tail = reinterpret_cast<volatile uint8_t *>(tail_address);
    for (rt_size_t i = 0; i < kExpectedTailSize; ++i) {
        tail[i] = static_cast<uint8_t>(0xC3U ^ (i * 0x17U));
    }
    sync_full_psram_cache();
    for (rt_size_t i = 0; i < kExpectedTailSize; ++i) {
        const uint8_t expected = static_cast<uint8_t>(0xC3U ^ (i * 0x17U));
        if (tail[i] != expected) {
            log_e(
                "PSRAM tail mismatch at 0x%08lx: got=0x%02x expected=0x%02x",
                (unsigned long)(tail_address + i),
                tail[i],
                expected
            );
            return false;
        }
    }
    return true;
}

} // namespace

extern "C" int psram_test(int argc, char **argv) {
    (void)argc;
    (void)argv;

    std::unique_lock<std::mutex> test_lock(
        g_psram_test_mutex, std::try_to_lock
    );
    if (!test_lock.owns_lock()) {
        log_w("psram_test is already running");
        return -RT_EBUSY;
    }
    if (!psram_is_ready()) {
        log_e("PSRAM is unavailable; check startup diagnostics");
        return -RT_ERROR;
    }
    if (psram_get_size() != 8U * 1024U * 1024U) {
        log_e("unexpected PSRAM size: %lu", (unsigned long)psram_get_size());
        return -RT_ERROR;
    }

    log_w(
        "destructive test: overwriting all PSRAM 0x%08lx-0x%08lx",
        (unsigned long)psram_get_base(),
        (unsigned long)(psram_get_base() + psram_get_size() - 1U)
    );

    auto **blocks =
        static_cast<void **>(rt_malloc(kExpectedBlockCount * sizeof(void *)));
    if (blocks == RT_NULL) {
        log_e(
            "failed to allocate %lu-byte internal pointer table",
            (unsigned long)(kExpectedBlockCount * sizeof(void *))
        );
        return -RT_ENOMEM;
    }

    struct rt_mempool pool;
    rt_size_t allocated = 0U;
    bool passed = true;

    if (rt_mp_init(
            &pool,
            "psramtest",
            reinterpret_cast<void *>(psram_get_base()),
            psram_get_size(),
            kPayloadSize
        ) != RT_EOK) {
        log_e("rt_mp_init failed");
        rt_free(blocks);
        return -RT_ERROR;
    }

    for (; allocated < kExpectedBlockCount; ++allocated) {
        blocks[allocated] = rt_mp_alloc(&pool, 0);
        if (blocks[allocated] == RT_NULL) {
            log_e(
                "PSRAM pool allocation stopped at %lu/%lu",
                (unsigned long)allocated,
                (unsigned long)kExpectedBlockCount
            );
            passed = false;
            break;
        }
    }

    if (allocated == kExpectedBlockCount) {
        void *extra = rt_mp_alloc(&pool, 0);
        if (extra != RT_NULL) {
            log_e("PSRAM pool unexpectedly allowed an extra allocation");
            rt_mp_free(extra);
            passed = false;
        } else {
            log_i(
                "PSRAM pool allocated all %lu blocks; extra allocation "
                "rejected",
                (unsigned long)allocated
            );
        }
    }

    if (passed) {
        passed = run_pattern(blocks, allocated, Pattern::Address) &&
                 run_pattern(blocks, allocated, Pattern::InverseAddress) &&
                 run_pattern(blocks, allocated, Pattern::Checkerboard) &&
                 test_tail_bytes();
    }

    for (rt_size_t i = allocated; i > 0U; --i) {
        rt_mp_free(blocks[i - 1U]);
        if ((i & 0x3FU) == 0U) {
            rt_thread_yield();
        }
    }

    if (pool.block_free_count != pool.block_total_count ||
        pool.block_total_count != kExpectedBlockCount) {
        log_e(
            "PSRAM pool count restore failed: free=%lu total=%lu expected=%lu",
            (unsigned long)pool.block_free_count,
            (unsigned long)pool.block_total_count,
            (unsigned long)kExpectedBlockCount
        );
        passed = false;
    } else {
        void *again = rt_mp_alloc(&pool, 0);
        if (again == RT_NULL) {
            log_e("PSRAM pool cannot allocate after full release");
            passed = false;
        } else {
            rt_mp_free(again);
        }
    }

    rt_mp_detach(&pool);
    rt_free(blocks);

    if (passed) {
        log_i(
            "PSRAM destructive test PASSED: %lu blocks + %lu tail bytes",
            (unsigned long)kExpectedBlockCount,
            (unsigned long)kExpectedTailSize
        );
        return RT_EOK;
    }
    log_e("PSRAM destructive test FAILED");
    return -RT_ERROR;
}

MSH_CMD_EXPORT(psram_test, destructively test all 8 MiB XPI1 PSRAM);

#endif /* BSP_USING_PSRAM */

#ifdef PKG_USING_WL_HOSTED_RTT_HOST

#include <finsh.h>
#include <lwip/ip_addr.h>
#include <netdev.h>
#include <rtthread.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "wlh_rtt_host.h"
#ifdef WLH_RTT_HOST_USING_IPERF2
#include "wlh_rtt_iperf2.h"
#endif

namespace {

const char *host_state_name(uint32_t state) {
    static const char *const names[] = {
        "uninitialized",
        "transport-starting",
        "waiting-peer",
        "negotiating",
        "ready",
        "congested",
        "recovering",
        "failed",
        "stopping",
    };
    return state < sizeof(names) / sizeof(names[0]) ? names[state] : "unknown";
}

void print_netdev(const char *name, const char *role, bool active) {
    struct netdev *device = netdev_get_by_name(name);
    char address[48] = "0.0.0.0";
    if (device == RT_NULL) {
        rt_kprintf("  %s/%s: absent\n", role, name);
        return;
    }
    (void)ipaddr_ntoa_r(&device->ip_addr, address, sizeof(address));
    rt_kprintf(
        "  %s/%s: %s link=%s ip=%s mtu=%u\n",
        role,
        name,
        active ? "active" : "inactive",
        netdev_is_link_up(device) ? "up" : "down",
        address,
        device->mtu
    );
}

uint32_t parse_u32(const char *text, uint32_t fallback) {
    char *end = RT_NULL;
    unsigned long value;
    if (text == RT_NULL || *text == '\0')
        return fallback;
    value = std::strtoul(text, &end, 10);
    return end != text && *end == '\0' && value <= UINT32_MAX
               ? static_cast<uint32_t>(value)
               : fallback;
}

} // namespace

extern "C" int wlh_status(int argc, char **argv) {
    wlh_rtt_stats_t stats;
    (void)argc;
    (void)argv;
    if (wlh_rtt_host_get_stats(wlh_rtt_host_default(), &stats) != RT_EOK) {
        rt_kprintf("WL-hosted is not initialized\n");
        return -RT_ERROR;
    }
    rt_kprintf(
        "WL-hosted: session=%s id=%lu recoveries=%lu\n",
        host_state_name(stats.host_state),
        static_cast<unsigned long>(stats.session_id),
        static_cast<unsigned long>(stats.recoveries)
    );
    rt_kprintf(
        "  SDIO: tx=%lu err=%lu qfull=%lu rx=%lu err=%lu invalid=%lu "
        "reset=%lu\n",
        static_cast<unsigned long>(stats.transport.tx_frames),
        static_cast<unsigned long>(stats.transport.tx_errors),
        static_cast<unsigned long>(stats.transport.tx_queue_full),
        static_cast<unsigned long>(stats.transport.rx_frames),
        static_cast<unsigned long>(stats.transport.rx_errors),
        static_cast<unsigned long>(stats.transport.rx_invalid_length),
        static_cast<unsigned long>(stats.transport.bus_resets)
    );
    print_netdev("w0", "wlan0 STA", stats.sta.active);
    rt_kprintf(
        "    DHCP client=%s tx=%lu/drop=%lu rx=%lu pool=%lu queue=%lu "
        "delivery=%lu\n",
        stats.sta.active ? "running" : "stopped",
        static_cast<unsigned long>(stats.sta.tx_frames),
        static_cast<unsigned long>(stats.sta.tx_dropped),
        static_cast<unsigned long>(stats.sta.rx_frames),
        static_cast<unsigned long>(stats.sta.rx_pool_empty),
        static_cast<unsigned long>(stats.sta.rx_queue_full),
        static_cast<unsigned long>(stats.sta.rx_delivery_failed)
    );
    print_netdev("w1", "wlan1 AP", stats.ap.active);
    rt_kprintf(
        "    DHCP server=%s clients=%lu tx=%lu/drop=%lu rx=%lu pool=%lu "
        "queue=%lu delivery=%lu\n",
        stats.ap.active ? "running" : "stopped",
        static_cast<unsigned long>(stats.ap_clients),
        static_cast<unsigned long>(stats.ap.tx_frames),
        static_cast<unsigned long>(stats.ap.tx_dropped),
        static_cast<unsigned long>(stats.ap.rx_frames),
        static_cast<unsigned long>(stats.ap.rx_pool_empty),
        static_cast<unsigned long>(stats.ap.rx_queue_full),
        static_cast<unsigned long>(stats.ap.rx_delivery_failed)
    );
    for (unsigned i = 0; i < WLH_RTT_STATS_MAX_AP_CLIENTS; ++i) {
        const wlh_rtt_ap_client_stats_t &client = stats.ap_client[i];
        if (!client.active && client.association_id == 0u)
            continue;
        rt_kprintf(
            "    client %02x:%02x:%02x:%02x:%02x:%02x %s rssi=%d aid=%lu "
            "reason=%lu\n",
            client.mac[0],
            client.mac[1],
            client.mac[2],
            client.mac[3],
            client.mac[4],
            client.mac[5],
            client.active ? "active" : "left",
            client.rssi_dbm,
            static_cast<unsigned long>(client.association_id),
            static_cast<unsigned long>(client.ieee80211_reason)
        );
    }
    return RT_EOK;
}

extern "C" int wlh_reset(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return wlh_rtt_host_recover(wlh_rtt_host_default());
}

MSH_CMD_EXPORT(wlh_status, show WL - hosted SDIO STA AP and DHCP status);
MSH_CMD_EXPORT(wlh_reset, recover the WL - hosted transport and session);

#ifdef WLH_RTT_HOST_USING_IPERF2
extern "C" int iperf(int argc, char **argv) {
    wlh_rtt_iperf2_config_t config{};
    if (argc == 2 && std::strcmp(argv[1], "stop") == 0)
        return wlh_rtt_iperf2_stop();
    if (argc < 3 ||
        (std::strcmp(argv[1], "tcp") != 0 &&
         std::strcmp(argv[1], "udp") != 0) ||
        (std::strcmp(argv[2], "client") != 0 &&
         std::strcmp(argv[2], "server") != 0)) {
        rt_kprintf(
            "iperf tcp client <ipv4> [seconds]\n"
            "iperf tcp server [seconds]\n"
            "iperf udp client <ipv4> [seconds] [mbps]\n"
            "iperf udp server [seconds]\n"
            "iperf stop\n"
        );
        return -RT_EINVAL;
    }
    config.protocol = std::strcmp(argv[1], "tcp") == 0 ? WLH_RTT_IPERF2_TCP
                                                       : WLH_RTT_IPERF2_UDP;
    config.role = std::strcmp(argv[2], "client") == 0 ? WLH_RTT_IPERF2_CLIENT
                                                      : WLH_RTT_IPERF2_SERVER;
    if (config.role == WLH_RTT_IPERF2_CLIENT) {
        if (argc < 4)
            return -RT_EINVAL;
        config.peer_ipv4 = argv[3];
        config.seconds = argc > 4 ? parse_u32(argv[4], 30u) : 30u;
        config.udp_mbps = argc > 5 ? parse_u32(argv[5], 20u) : 20u;
    } else {
        config.seconds = argc > 3 ? parse_u32(argv[3], 30u) : 30u;
    }
    return wlh_rtt_iperf2_start(&config);
}

MSH_CMD_EXPORT(iperf, run the package IPv4 iPerf2 client or server);
#endif

#endif /* PKG_USING_WL_HOSTED_RTT_HOST */
