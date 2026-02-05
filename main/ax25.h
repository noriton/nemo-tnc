// ax25.h のイメージ
#include <stdint.h>

typedef struct {
    char dest_call[7];
    char src_call[7];
    uint8_t dest_ssid;
    uint8_t src_ssid;
} ax25_header_t;

size_t ax25_build_ui_frame(const ax25_header_t *header, const uint8_t *info, size_t info_len, uint8_t *out_buf);
void encode_callsign(uint8_t *out_buf, const char *callsign, uint8_t ssid, bool is_last);
size_t decode_callsign(const uint8_t *in_buf, char *callsign, uint8_t *ssid);
