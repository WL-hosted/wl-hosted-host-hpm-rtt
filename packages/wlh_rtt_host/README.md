# WL-hosted RT-Thread Host package

`wlh_rtt_host` is a portable RT-Thread adapter for WL-hosted Host Core. It
registers one station WLAN device and one SoftAP WLAN device, maps their
Ethernet channels into RT-Thread's lwIP WLAN protocol, and optionally provides
an RT-Thread SDIO transport and IPv4 iPerf2.

## Integration boundary

The package uses only RT-Thread public WLAN, SDIO, lwIP, netdev, SAL, IPC and
standard C APIs plus `wl-hosted-core`. It contains no HPM SDK headers, register
accesses, cache primitives or board pin names. A BSP supplies its RT-Thread
SDIO host controller and may provide:

```c
const wlh_rtt_board_ops_t *wlh_rtt_board_get_ops(void **context);
```

The only board callback is `fatal_recovery`, invoked after both Host Core
recovery and the transport soft reset fail. Cold-reset sequencing belongs to
the BSP and must run before SDIO enumeration.

## Core dependency

The package SConscript locates Core in this order:

1. an exported absolute `WLH_CORE_ROOT`;
2. a `core/` directory inside the package.

It invokes Core's SConscript with `WLH_CORE_ROLE="host"`; Simulator IPC,
POSIX OSAL, tests and Coprocessor Core are not built. The current HPM BSP
exports its root `core` submodule.

## Interfaces and networking

Initialization always registers devices in this order:

| RT WLAN device | Mode | Core channel | lwIP/netdev |
|---|---|---|---|
| `wlan0` | station | `ETHERNET_STA` | `w0` |
| `wlan1` | SoftAP | `ETHERNET_AP` | `w1` |

Host Core initializes Wi-Fi with STA and AP interface flags together. RT-Thread
therefore owns the normal `wifi` MSH commands, DHCP client on `w0`, DHCP server
on `w1`, SAL and ping. The AP uses `192.168.169.1/24`, with leases `.2` through
`.254`. Router advertisement, NAT, IP forwarding, custom DNS forwarding, IPv6,
credential persistence and automatic reconnect are intentionally absent.

The AP accepts 1-32 byte SSIDs, channel 1-14, OPEN without a password, or
WPA2-PSK with an 8-63 byte password. Unsupported security never silently
downgrades to OPEN.

## Ownership and recovery

Transport TX is bounded and retains each Host Core frame until exactly one
completion. Generation tags reject callbacks from a prior transport instance.
Each WLAN interface owns a separate fixed RX pool; a shared worker injects
copied frames into the matching RT WLAN device. Requests have bounded
completion slots and generation IDs, so a cancelled scan cannot complete a
new scan.

Recovery reports pending operations as failed, takes `w0` and `w1` down,
clears AP clients, stops iPerf2, stops Host Core, performs a transport soft
reset, rebuilds the Session, and initializes APSTA again. It deliberately does
not restore a previous station connection or SoftAP configuration.

## Porting

To move the package to another RT-Thread SDIO BSP:

1. copy this directory into a package repository and register its Kconfig;
2. provide `WLH_CORE_ROOT` or bundle Core as `core/`;
3. enable the RT-Thread WLAN/lwIP/SDIO dependencies in Kconfig;
4. implement the board's SDIO host, DMA/cache policy and coprocessor cold reset;
5. optionally supply `fatal_recovery`.

No package business source needs board-specific edits. Native pure-logic tests
are available under `tests/` and build with ordinary CMake/CTest.
