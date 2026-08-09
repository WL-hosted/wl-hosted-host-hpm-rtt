#include "wlh_rtt_sdio_protocol.h"

#include "wlh/protocol/wire.h"

size_t wlh_rtt_sdio_padded_size(size_t frame_size) {
    if (frame_size == 0u || frame_size > WLH_RTT_SDIO_MAX_FRAME)
        return 0u;
    return (frame_size + WLH_RTT_SDIO_BLOCK_SIZE - 1u) &
           ~(WLH_RTT_SDIO_BLOCK_SIZE - 1u);
}

uint32_t wlh_rtt_sdio_window_address(size_t frame_size) {
    if (wlh_rtt_sdio_padded_size(frame_size) == 0u)
        return 0u;
    return WLH_RTT_SDIO_END_ADDRESS - (uint32_t)frame_size;
}

int wlh_rtt_sdio_accumulated_length(
    uint32_t previous, uint32_t current, size_t *frame_size
) {
    uint32_t delta;
    if (frame_size == NULL || current == UINT32_MAX)
        return -1;
    current &= WLH_RTT_SDIO_LENGTH_MODULUS - 1u;
    previous &= WLH_RTT_SDIO_LENGTH_MODULUS - 1u;
    delta = (current + WLH_RTT_SDIO_LENGTH_MODULUS - previous) %
            WLH_RTT_SDIO_LENGTH_MODULUS;
    if (delta < WLH_FRAME_HEADER_SIZE || delta > WLH_RTT_SDIO_MAX_FRAME)
        return -1;
    *frame_size = delta;
    return 0;
}

uint32_t wlh_rtt_sdio_available_tokens(
    uint32_t consumed, uint32_t offered
) {
    return (offered + WLH_RTT_SDIO_TOKEN_MODULUS - consumed) %
           WLH_RTT_SDIO_TOKEN_MODULUS;
}
