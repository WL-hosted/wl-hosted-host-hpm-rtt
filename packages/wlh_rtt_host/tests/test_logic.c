#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>

#include "wlh_rtt_iperf2_proto.h"
#include "wlh_rtt_logic.h"
#include "wlh_rtt_sdio_protocol.h"

static void test_sdio_protocol(void) {
    size_t length = 0u;
    assert(wlh_rtt_sdio_padded_size(1u) == 512u);
    assert(wlh_rtt_sdio_padded_size(512u) == 512u);
    assert(wlh_rtt_sdio_padded_size(513u) == 1024u);
    assert(wlh_rtt_sdio_padded_size(4092u) == 4096u);
    assert(wlh_rtt_sdio_padded_size(4093u) == 0u);
    assert(wlh_rtt_sdio_window_address(513u) == 0x1f800u - 513u);
    assert(wlh_rtt_sdio_accumulated_length(100u, 612u, &length) == 0);
    assert(length == 512u);
    assert(wlh_rtt_sdio_accumulated_length(
               0x0ffff0u, 0x000010u, &length
           ) == 0);
    assert(length == 32u);
    assert(wlh_rtt_sdio_accumulated_length(0u, 5u, &length) != 0);
    assert(wlh_rtt_sdio_accumulated_length(0u, 4093u, &length) != 0);
    assert(wlh_rtt_sdio_available_tokens(0xfffu, 1u) == 2u);
}

static void test_softap_and_routing(void) {
    const uint32_t open = 1u;
    const uint32_t wpa2 = 4u;
    assert(wlh_rtt_softap_validate(
               4u, 0u, open, 6u, false, open, wpa2
           ) == WLH_RTT_VALID);
    assert(wlh_rtt_softap_validate(
               4u, 8u, wpa2, 6u, false, open, wpa2
           ) == WLH_RTT_VALID);
    assert(wlh_rtt_softap_validate(
               0u, 0u, open, 6u, false, open, wpa2
           ) == WLH_RTT_INVALID);
    assert(wlh_rtt_softap_validate(
               4u, 7u, wpa2, 6u, false, open, wpa2
           ) == WLH_RTT_INVALID);
    assert(wlh_rtt_softap_validate(
               4u, 0u, open, 0u, false, open, wpa2
           ) == WLH_RTT_INVALID);
    assert(wlh_rtt_softap_validate(
               4u, 0u, open, 6u, true, open, wpa2
           ) == WLH_RTT_UNSUPPORTED);
    assert(wlh_rtt_route_wire_interface(1u, 1u, 2u) == WLH_RTT_ROUTE_STA);
    assert(wlh_rtt_route_wire_interface(2u, 1u, 2u) == WLH_RTT_ROUTE_AP);
    assert(wlh_rtt_route_wire_interface(0u, 1u, 2u) ==
           WLH_RTT_ROUTE_INVALID);
}

static void test_generation_and_pools(void) {
    uint64_t sta = wlh_rtt_pool_initial_mask(16u);
    uint64_t ap = wlh_rtt_pool_initial_mask(16u);
    int index;
    assert(wlh_rtt_next_generation(0u) == 1u);
    assert(wlh_rtt_next_generation(UINT32_MAX) == 1u);
    for (index = 0; index < 16; ++index)
        assert(wlh_rtt_pool_take(&sta, 16u) == index);
    assert(wlh_rtt_pool_take(&sta, 16u) == -1);
    assert(wlh_rtt_pool_take(&ap, 16u) == 0);
    assert(ap != sta);
    wlh_rtt_pool_put(&sta, 7u);
    assert(wlh_rtt_pool_take(&sta, 16u) == 7);
}

static void test_ap_clients_and_recovery_clear(void) {
    wlh_rtt_ap_client_stats_t clients[4] = {0};
    const uint8_t first[6] = {0x02, 0, 0, 0, 0, 1};
    const uint8_t second[6] = {0x02, 0, 0, 0, 0, 2};
    uint32_t count = 0u;
    assert(wlh_rtt_ap_client_update(
        clients, 4u, first, true, -42, 7u, 0u, &count
    ));
    assert(wlh_rtt_ap_client_update(
        clients, 4u, second, true, -55, 8u, 0u, &count
    ));
    assert(count == 2u && clients[0].rssi_dbm == -42);
    assert(wlh_rtt_ap_client_update(
        clients, 4u, first, false, 0, 7u, 4u, &count
    ));
    assert(count == 1u && clients[0].ieee80211_reason == 4u);
    wlh_rtt_ap_client_clear(clients, 4u, &count);
    assert(count == 0u && !clients[0].active && !clients[1].active);
}

static void test_iperf2_protocol(void) {
    uint8_t packet[1470] = {0};
    int32_t sequence;
    uint32_t seconds;
    uint32_t microseconds;
    uint32_t fields[10];
    assert(wlh_iperf2_udp_header_encode(
               packet, sizeof(packet), -17, 3u, 4000u
           ) == 0);
    assert(wlh_iperf2_udp_header_decode(
               packet, sizeof(packet), &sequence, &seconds, &microseconds
           ) == 0);
    assert(sequence == -17 && seconds == 3u && microseconds == 4000u);
    assert(wlh_iperf2_udp_server_report_encode(
               packet,
               sizeof(packet),
               UINT64_C(0x0000000200000003),
               1234u,
               5u,
               6u,
               7u
           ) == 0);
    memcpy(fields, packet + WLH_IPERF2_UDP_HEADER_SIZE, sizeof(fields));
    assert(ntohl(fields[0]) == UINT32_C(0x80000000));
    assert(ntohl(fields[1]) == 2u && ntohl(fields[2]) == 3u);
    assert(ntohl(fields[3]) == 1u && ntohl(fields[4]) == 234000u);
    assert(ntohl(fields[5]) == 5u && ntohl(fields[6]) == 6u);
    assert(ntohl(fields[7]) == 7u);
}

int main(void) {
    test_sdio_protocol();
    test_softap_and_routing();
    test_generation_and_pools();
    test_ap_clients_and_recovery_clear();
    test_iperf2_protocol();
    puts("wlh_rtt_host logic tests passed");
    return 0;
}
