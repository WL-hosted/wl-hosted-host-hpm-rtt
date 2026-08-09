#ifndef WLH_RTT_SDIO_PROTOCOL_H
#define WLH_RTT_SDIO_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define WLH_RTT_SDIO_BLOCK_SIZE 512u
#define WLH_RTT_SDIO_MAX_FRAME 4092u
#define WLH_RTT_SDIO_STAGING_SIZE 4096u
#define WLH_RTT_SDIO_END_ADDRESS 0x1f800u
#define WLH_RTT_SDIO_LENGTH_MODULUS 0x100000u
#define WLH_RTT_SDIO_TOKEN_MODULUS 0x1000u

size_t wlh_rtt_sdio_padded_size(size_t frame_size);
uint32_t wlh_rtt_sdio_window_address(size_t frame_size);
int wlh_rtt_sdio_accumulated_length(
    uint32_t previous, uint32_t current, size_t *frame_size
);
uint32_t wlh_rtt_sdio_available_tokens(
    uint32_t consumed, uint32_t offered
);

#endif
