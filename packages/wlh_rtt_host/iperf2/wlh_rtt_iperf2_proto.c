#include "wlh_rtt_iperf2_proto.h"

#include <string.h>

#include <arpa/inet.h>

int wlh_iperf2_udp_header_encode(
    uint8_t *buffer,
    size_t size,
    int32_t sequence,
    uint32_t seconds,
    uint32_t microseconds
) {
    uint32_t fields[3];
    if (buffer == NULL || size < WLH_IPERF2_UDP_HEADER_SIZE)
        return -1;
    fields[0] = htonl((uint32_t)sequence);
    fields[1] = htonl(seconds);
    fields[2] = htonl(microseconds);
    memcpy(buffer, fields, sizeof(fields));
    return 0;
}

int wlh_iperf2_udp_header_decode(
    const uint8_t *buffer,
    size_t size,
    int32_t *sequence,
    uint32_t *seconds,
    uint32_t *microseconds
) {
    uint32_t fields[3];
    if (buffer == NULL || size < WLH_IPERF2_UDP_HEADER_SIZE ||
        sequence == NULL || seconds == NULL || microseconds == NULL)
        return -1;
    memcpy(fields, buffer, sizeof(fields));
    *sequence = (int32_t)ntohl(fields[0]);
    *seconds = ntohl(fields[1]);
    *microseconds = ntohl(fields[2]);
    return 0;
}

int wlh_iperf2_udp_server_report_encode(
    uint8_t *buffer,
    size_t size,
    uint64_t total_bytes,
    uint32_t elapsed_ms,
    uint32_t errors,
    uint32_t out_of_order,
    uint32_t datagrams
) {
    uint32_t fields[10];
    if (buffer == NULL || size < WLH_IPERF2_UDP_SERVER_REPORT_SIZE)
        return -1;
    fields[0] = htonl(UINT32_C(0x80000000));
    fields[1] = htonl((uint32_t)(total_bytes >> 32));
    fields[2] = htonl((uint32_t)total_bytes);
    fields[3] = htonl(elapsed_ms / 1000u);
    fields[4] = htonl((elapsed_ms % 1000u) * 1000u);
    fields[5] = htonl(errors);
    fields[6] = htonl(out_of_order);
    fields[7] = htonl(datagrams);
    fields[8] = 0u;
    fields[9] = 0u;
    memcpy(buffer + WLH_IPERF2_UDP_HEADER_SIZE, fields, sizeof(fields));
    return 0;
}
