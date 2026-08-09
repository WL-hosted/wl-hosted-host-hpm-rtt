#ifndef WLH_RTT_IPERF2_PROTO_H
#define WLH_RTT_IPERF2_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WLH_IPERF2_PORT 5001u
#define WLH_IPERF2_UDP_SIZE 1470u
#define WLH_IPERF2_UDP_HEADER_SIZE 12u
#define WLH_IPERF2_UDP_SERVER_REPORT_SIZE 52u

int wlh_iperf2_udp_header_encode(
    uint8_t *buffer,
    size_t size,
    int32_t sequence,
    uint32_t seconds,
    uint32_t microseconds
);
int wlh_iperf2_udp_header_decode(
    const uint8_t *buffer,
    size_t size,
    int32_t *sequence,
    uint32_t *seconds,
    uint32_t *microseconds
);
int wlh_iperf2_udp_server_report_encode(
    uint8_t *buffer,
    size_t size,
    uint64_t total_bytes,
    uint32_t elapsed_ms,
    uint32_t errors,
    uint32_t out_of_order,
    uint32_t datagrams
);

#endif
