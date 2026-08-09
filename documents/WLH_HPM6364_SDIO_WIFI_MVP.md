# HPM6364 + SDIO + ESP32-C5 WLAN Host MVP

## Architecture and scope

The HPM6364 application consumes `packages/wlh_rtt_host` as an RT-Thread
package. RT-Thread's standard Wi-Fi management is the command and network
control plane; WL-hosted Host Core is the RPC/session and Ethernet data plane;
the package's portable SDIO worker is the transport; HPM SDXC0 is only a BSP
adapter.

The MVP supports concurrent station and SoftAP, IPv4, DHCP, SAL, ping, and one
optional iPerf2 TCP/UDP session. It does not support BLE, IPv6, NAT, IP
forwarding, saved credentials or automatic reconnect.

```text
wifi MSH -> wlan0 (STA) -> w0 -> DHCP client --+
                                                +-> Host Core -> SDIO -> C5
wifi MSH -> wlan1 (AP)  -> w1 -> DHCP server --+
```

`w0` and `w1` are produced by the existing RT-Thread WLAN lwIP protocol. Their
stable mapping follows from registering and setting `wlan0` to STATION before
setting `wlan1` to AP; no RT-Thread source modification is needed.

## Package and BSP boundary

The package depends only on RT-Thread public APIs, standard C, and
`wl-hosted-core`. It owns Host Core lifecycle, APSTA request/event mapping,
bounded requests, dual Ethernet channels, independent RX pools, SDIO wire
logic, diagnostics and iPerf2. A dependency scan must remain free of HPM SDK
headers, register names, PA pin identifiers and application-relative sources.

The HPM BSP owns PA08-PA14 pinmux, ESP_EN, SDXC/DMA/cache behavior and fatal
board reset. SDXC0 is wired as PA11 CLK, PA10 CMD, PA12 D0, PA13 D1, PA08 D2
and PA09 D3. PA14 is ESP_EN: it is driven only as open-drain low for 100 ms,
then released to the external pull-up for 1500 ms. It is never card detect or
push-pull high.

`drv_sdio.c` calls a weak `rt_hw_sdxc_prepare_device()` before enumeration.
The HPM override performs the cold reset. Enumeration begins around 375 kHz,
negotiates four-bit mode, and uses 50 MHz high speed when available (otherwise
25 MHz). A 16 KiB noncacheable bounce buffer handles unaligned ADMA ranges.

## Core build and APSTA initialization

The root SConscript skips the root `core/` walk. The package locates Core from
the exported `WLH_CORE_ROOT` or a package-local `core/`, then invokes its
SConscript with `WLH_CORE_ROLE="host"`. Core builds protocol/nanopb, RTT OSAL,
RTT ulog and Host Core only.

Host Core exposes `wlh_host_wifi_initialize_interfaces()`. The package passes
`WLH_WIFI_INTERFACE_FLAG_STA | WLH_WIFI_INTERFACE_FLAG_AP`; the older
`wlh_host_wifi_initialize()` remains a station-only compatibility API. No wire
schema changed.

## SDIO state and ownership

The transport matches Function 1, vendor `0x6666`, product `0x2222`; it uses
512-byte blocks, a 4096-byte staging buffer, maximum wire frame 4092, end
address `0x1f800`, packet-length modulus `0x100000`, and token modulus `0x1000`.

One worker serializes CMD52/CMD53. IRQ only releases a semaphore. TX uses a
bounded 16-entry queue and retains frame ownership through exactly one
completion. RX calculates accumulated length with wrap handling, reads padded
data, validates the wire header, then clears the interrupt. RX and TX use
bounded bursts so neither direction can starve the other. Transport generation
tags prevent an old callback from completing a recovered Session.

## WLAN events and data plane

STA scan, connect and disconnect each permit one active management operation.
SoftAP start and stop follow the same rule. Completion contexts come from a
bounded slot table and carry their generation. Scan results additionally carry
Core's `scan_id`. Unsupported SoftAP security returns `-RT_ENOSYS`; malformed
SSID, password or channel returns `-RT_EINVAL`.

Core connected/disconnected events are decoded by their explicit interface;
unknown interface values are ignored. AP client join/leave events update both
RT-Thread's client list and a diagnostic table containing MAC, RSSI,
association ID and IEEE 802.11 leave reason.

STA and AP each own 16 fixed 1600-byte RX buffers. The shared RX queue carries
interface ID, block index and length, and the worker calls
`rt_wlan_dev_report_data()` on the matching device. Thus station pressure
cannot consume the AP's entire pool. TX selects
`wlh_host_ethernet_sta_send()` or `wlh_host_ethernet_ap_send()` and rejects
inactive interfaces, non-READY Sessions and invalid lengths without retrying
forever.

## IPv4 and commands

RT-Thread starts the `w0` DHCP client after STA CONNECT and stops it and clears
the address after DISCONNECT. AP START assigns `192.168.169.1/24` to `w1` and
starts DHCPD; AP STOP shuts DHCPD down and lowers the link. DHCPD does not
advertise the HPM as a router.

Standard commands are `wifi scan`, `wifi join`, `wifi disc`, `wifi status`,
`wifi ap`, `wifi list_sta`, `wifi ap_stop`, and ping. Thin HPM wrappers add
`wlh_status`, `wlh_reset`, and the documented `iperf tcp|udp client|server`
forms. iPerf2 uses IPv4 port 5001, one session, 30 seconds by default and UDP
1470-byte datagrams at 20 Mbit/s. Stop sets a cancel flag and shuts down active
sockets. This change compiles and logic-tests iPerf but does not claim hardware
throughput.

## Recovery and verification boundary

Recovery reports pending scans/connects/AP operations as failed, lowers both
links, clears AP clients, cancels iPerf2, stops Host Core, performs SDIO soft
reset, rebuilds the Session and initializes APSTA. Previous STA/AP configuration
is not replayed. A failed soft recovery invokes the BSP's fatal board reset.

This MVP is build- and logic-verified without flashing hardware. Hardware Wi-Fi
association, DHCP interoperability, SDIO signal integrity and iPerf throughput
remain explicit board-validation steps.
