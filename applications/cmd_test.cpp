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
        for (rt_size_t word = 0; word < kPayloadSize / sizeof(uint32_t); ++word) {
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
        for (rt_size_t word = 0; word < kPayloadSize / sizeof(uint32_t); ++word) {
            const uintptr_t address = reinterpret_cast<uintptr_t>(&words[word]);
            const uint32_t expected = expected_word(address, pattern);
            if (words[word] != expected) {
                log_e("PSRAM %s mismatch at 0x%08lx: got=0x%08lx expected=0x%08lx",
                      pattern_name(pattern), (unsigned long)address,
                      (unsigned long)words[word], (unsigned long)expected);
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
            log_e("PSRAM tail mismatch at 0x%08lx: got=0x%02x expected=0x%02x",
                  (unsigned long)(tail_address + i), tail[i], expected);
            return false;
        }
    }
    return true;
}

} // namespace

extern "C" int psram_test(int argc, char **argv) {
    (void)argc;
    (void)argv;

    std::unique_lock<std::mutex> test_lock(g_psram_test_mutex, std::try_to_lock);
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

    log_w("destructive test: overwriting all PSRAM 0x%08lx-0x%08lx",
          (unsigned long)psram_get_base(),
          (unsigned long)(psram_get_base() + psram_get_size() - 1U));

    auto **blocks = static_cast<void **>(rt_malloc(kExpectedBlockCount * sizeof(void *)));
    if (blocks == RT_NULL) {
        log_e("failed to allocate %lu-byte internal pointer table",
              (unsigned long)(kExpectedBlockCount * sizeof(void *)));
        return -RT_ENOMEM;
    }

    struct rt_mempool pool;
    rt_size_t allocated = 0U;
    bool passed = true;

    if (rt_mp_init(&pool, "psramtest", reinterpret_cast<void *>(psram_get_base()),
                   psram_get_size(), kPayloadSize) != RT_EOK) {
        log_e("rt_mp_init failed");
        rt_free(blocks);
        return -RT_ERROR;
    }

    for (; allocated < kExpectedBlockCount; ++allocated) {
        blocks[allocated] = rt_mp_alloc(&pool, 0);
        if (blocks[allocated] == RT_NULL) {
            log_e("PSRAM pool allocation stopped at %lu/%lu",
                  (unsigned long)allocated, (unsigned long)kExpectedBlockCount);
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
            log_i("PSRAM pool allocated all %lu blocks; extra allocation rejected",
                  (unsigned long)allocated);
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
        log_e("PSRAM pool count restore failed: free=%lu total=%lu expected=%lu",
              (unsigned long)pool.block_free_count,
              (unsigned long)pool.block_total_count,
              (unsigned long)kExpectedBlockCount);
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
        log_i("PSRAM destructive test PASSED: %lu blocks + %lu tail bytes",
              (unsigned long)kExpectedBlockCount, (unsigned long)kExpectedTailSize);
        return RT_EOK;
    }
    log_e("PSRAM destructive test FAILED");
    return -RT_ERROR;
}

MSH_CMD_EXPORT(psram_test, destructively test all 8 MiB XPI1 PSRAM);

#endif /* BSP_USING_PSRAM */
