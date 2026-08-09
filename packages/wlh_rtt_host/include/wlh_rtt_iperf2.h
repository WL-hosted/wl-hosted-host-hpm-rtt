#ifndef WLH_RTT_IPERF2_H
#define WLH_RTT_IPERF2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wlh_rtt_iperf2_protocol {
    WLH_RTT_IPERF2_TCP = 1,
    WLH_RTT_IPERF2_UDP = 2
} wlh_rtt_iperf2_protocol_t;

typedef enum wlh_rtt_iperf2_role {
    WLH_RTT_IPERF2_CLIENT = 1,
    WLH_RTT_IPERF2_SERVER = 2
} wlh_rtt_iperf2_role_t;

typedef struct wlh_rtt_iperf2_config {
    wlh_rtt_iperf2_protocol_t protocol;
    wlh_rtt_iperf2_role_t role;
    const char *peer_ipv4;
    uint16_t port;
    uint32_t seconds;
    uint32_t udp_mbps;
} wlh_rtt_iperf2_config_t;

int wlh_rtt_iperf2_start(const wlh_rtt_iperf2_config_t *config);
int wlh_rtt_iperf2_stop(void);
int wlh_rtt_iperf2_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
