#include "wlh_rtt_iperf2.h"

#include <stdbool.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <rtthread.h>
#include <ulog.h>

#include "wlh_rtt_iperf2_proto.h"

typedef struct iperf_state {
    wlh_rtt_iperf2_config_t config;
    char peer[16];
    rt_thread_t thread;
    volatile bool running;
    volatile bool cancel;
    int socket_fd;
    int data_fd;
} iperf_state_t;

static iperf_state_t state = {.socket_fd = -1, .data_fd = -1};

static uint32_t elapsed_ms(rt_tick_t start) {
    return (uint32_t)((rt_tick_get() - start) * 1000u / RT_TICK_PER_SECOND);
}

static void close_fd(int *fd) {
    if (*fd >= 0) {
        closesocket(*fd);
        *fd = -1;
    }
}

static void tcp_client(void) {
    struct sockaddr_in peer;
    uint8_t buffer[1460];
    rt_tick_t start = rt_tick_get();
    rt_memset(buffer, 0xa5, sizeof(buffer));
    state.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (state.socket_fd < 0)
        return;
    rt_memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_port = htons(state.config.port);
    peer.sin_addr.s_addr = inet_addr(state.peer);
    if (connect(
            state.socket_fd, (struct sockaddr *)&peer, sizeof(peer)
        ) != 0)
        return;
    while (!state.cancel &&
           elapsed_ms(start) < state.config.seconds * 1000u) {
        if (send(state.socket_fd, buffer, sizeof(buffer), 0) <= 0)
            break;
    }
}

static void tcp_server(void) {
    struct sockaddr_in local;
    socklen_t length = sizeof(local);
    uint8_t buffer[1460];
    int reuse = 1;
    rt_tick_t start;
    state.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (state.socket_fd < 0)
        return;
    setsockopt(
        state.socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)
    );
    rt_memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(state.config.port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(
            state.socket_fd, (struct sockaddr *)&local, sizeof(local)
        ) != 0 ||
        listen(state.socket_fd, 1) != 0)
        return;
    state.data_fd = accept(
        state.socket_fd, (struct sockaddr *)&local, &length
    );
    if (state.data_fd < 0)
        return;
    start = rt_tick_get();
    while (!state.cancel &&
           elapsed_ms(start) < state.config.seconds * 1000u) {
        if (recv(state.data_fd, buffer, sizeof(buffer), 0) <= 0)
            break;
    }
}

static void udp_client(void) {
    struct sockaddr_in peer;
    uint8_t buffer[WLH_IPERF2_UDP_SIZE];
    uint32_t sequence = 0u;
    uint64_t bytes_per_second =
        (uint64_t)state.config.udp_mbps * 125000u;
    rt_tick_t start = rt_tick_get();
    state.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (state.socket_fd < 0)
        return;
    rt_memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_port = htons(state.config.port);
    peer.sin_addr.s_addr = inet_addr(state.peer);
    rt_memset(buffer, 0x5a, sizeof(buffer));
    while (!state.cancel &&
           elapsed_ms(start) < state.config.seconds * 1000u) {
        uint32_t now_ms = elapsed_ms(start);
        uint64_t packet_budget =
            bytes_per_second * now_ms /
            ((uint64_t)WLH_IPERF2_UDP_SIZE * 1000u);
        if ((uint64_t)sequence >= packet_budget) {
            rt_thread_mdelay(1u);
            continue;
        }
        wlh_iperf2_udp_header_encode(
            buffer,
            sizeof(buffer),
            (int32_t)sequence++,
            now_ms / 1000u,
            (now_ms % 1000u) * 1000u
        );
        if (sendto(
                state.socket_fd,
                buffer,
                sizeof(buffer),
                0,
                (struct sockaddr *)&peer,
                sizeof(peer)
            ) < 0)
            break;
    }
    wlh_iperf2_udp_header_encode(
        buffer,
        sizeof(buffer),
        -(int32_t)sequence,
        elapsed_ms(start) / 1000u,
        (elapsed_ms(start) % 1000u) * 1000u
    );
    (void)sendto(
        state.socket_fd,
        buffer,
        sizeof(buffer),
        0,
        (struct sockaddr *)&peer,
        sizeof(peer)
    );
}

static void udp_server(void) {
    struct sockaddr_in local;
    struct sockaddr_in peer;
    socklen_t peer_length = sizeof(peer);
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 200000};
    uint8_t buffer[WLH_IPERF2_UDP_SIZE];
    rt_tick_t start = rt_tick_get();
    uint64_t total_bytes = 0u;
    uint32_t datagrams = 0u;
    uint32_t errors = 0u;
    uint32_t out_of_order = 0u;
    int32_t expected_sequence = 0;
    state.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (state.socket_fd < 0)
        return;
    (void)setsockopt(
        state.socket_fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    );
    rt_memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(state.config.port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(
            state.socket_fd, (struct sockaddr *)&local, sizeof(local)
        ) != 0)
        return;
    while (!state.cancel &&
           elapsed_ms(start) < state.config.seconds * 1000u) {
        int32_t sequence;
        uint32_t seconds;
        uint32_t microseconds;
        int received = recvfrom(
            state.socket_fd,
            buffer,
            sizeof(buffer),
            0,
            (struct sockaddr *)&peer,
            &peer_length
        );
        if (received <= 0)
            continue;
        if (wlh_iperf2_udp_header_decode(
                buffer,
                (size_t)received,
                &sequence,
                &seconds,
                &microseconds
            ) != 0)
            continue;
        if (sequence < 0) {
            uint32_t duration = elapsed_ms(start);
            (void)wlh_iperf2_udp_server_report_encode(
                buffer,
                sizeof(buffer),
                total_bytes,
                duration,
                errors,
                out_of_order,
                datagrams
            );
            (void)sendto(
                state.socket_fd,
                buffer,
                WLH_IPERF2_UDP_SERVER_REPORT_SIZE,
                0,
                (struct sockaddr *)&peer,
                peer_length
            );
            break;
        }
        total_bytes += (uint32_t)received;
        datagrams++;
        if (sequence < expected_sequence)
            out_of_order++;
        else if (sequence > expected_sequence)
            errors += (uint32_t)(sequence - expected_sequence);
        if (sequence >= expected_sequence)
            expected_sequence = sequence + 1;
    }
}

static void iperf_main(void *parameter) {
    (void)parameter;
    if (state.config.protocol == WLH_RTT_IPERF2_TCP &&
        state.config.role == WLH_RTT_IPERF2_CLIENT)
        tcp_client();
    else if (state.config.protocol == WLH_RTT_IPERF2_TCP)
        tcp_server();
    else if (state.config.role == WLH_RTT_IPERF2_CLIENT)
        udp_client();
    else
        udp_server();
    close_fd(&state.data_fd);
    close_fd(&state.socket_fd);
    state.running = false;
    log_i("iPerf2 session stopped");
}

int wlh_rtt_iperf2_start(const wlh_rtt_iperf2_config_t *config) {
    if (config == RT_NULL || state.running ||
        (config->protocol != WLH_RTT_IPERF2_TCP &&
         config->protocol != WLH_RTT_IPERF2_UDP) ||
        (config->role != WLH_RTT_IPERF2_CLIENT &&
         config->role != WLH_RTT_IPERF2_SERVER) ||
        (config->role == WLH_RTT_IPERF2_CLIENT &&
         (config->peer_ipv4 == RT_NULL ||
          inet_addr(config->peer_ipv4) == INADDR_NONE)))
        return -RT_EINVAL;
    rt_memset(&state, 0, sizeof(state));
    state.socket_fd = -1;
    state.data_fd = -1;
    state.config = *config;
    if (state.config.port == 0u)
        state.config.port = WLH_IPERF2_PORT;
    if (state.config.seconds == 0u)
        state.config.seconds = 30u;
    if (state.config.udp_mbps == 0u)
        state.config.udp_mbps = 20u;
    if (config->peer_ipv4 != RT_NULL) {
        rt_strncpy(state.peer, config->peer_ipv4, sizeof(state.peer) - 1u);
        state.config.peer_ipv4 = state.peer;
    }
    state.running = true;
    state.thread = rt_thread_create(
        "iperf2", iperf_main, RT_NULL, 4096u, 18u, 10u
    );
    if (state.thread == RT_NULL) {
        state.running = false;
        return -RT_ENOMEM;
    }
    return rt_thread_startup(state.thread);
}

int wlh_rtt_iperf2_stop(void) {
    if (!state.running)
        return RT_EOK;
    state.cancel = true;
    if (state.data_fd >= 0)
        shutdown(state.data_fd, SHUT_RDWR);
    if (state.socket_fd >= 0)
        shutdown(state.socket_fd, SHUT_RDWR);
    return RT_EOK;
}

int wlh_rtt_iperf2_is_running(void) {
    return state.running ? 1 : 0;
}
