/* MDX-1 phone link: a small framed protocol over any byte stream (BLE transparent UART via the
 * BM83 module on the target, a loopback buffer in the simulator, USB CDC later if wanted).
 *
 * Frame:  0xA5  len  type  seq  payload[len]  crc16(2, big-endian)
 *   len   payload length, 0..LINK_MAX_PAYLOAD
 *   type  request type from the phone; the detector answers with type | 0x80 and the same seq.
 *         Unsolicited frames (status stream, events) use seq = 0.
 *   crc   CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over type, seq and payload.
 * All multi-byte payload fields are little-endian.
 *
 * The phone never needs to know the settings layout: it asks for the item table (LM_ITEM) and
 * builds its own menu from name / range / labels, exactly the list the detector's own menu shows.
 * See docs/MDX-1_link_protocol.md for the field-by-field description.                         */
#ifndef MDX_LINK_H
#define MDX_LINK_H
#include "../det/detector.h"
#include "../ui/screens.h"
#include "../log/log.h"

#define LINK_PROTO_VER    1
#define LINK_MAX_PAYLOAD  240
#define LINK_SOF          0xA5
#define LINK_FW_VERSION   MDX_FW_VERSION
#define LINK_HW_REV       "A5"

enum {  /* message types (requests); responses are type | 0x80 */
    LM_HELLO = 0x01, LM_ITEM = 0x02, LM_VALUES = 0x03, LM_SET = 0x04, LM_ACTION = 0x05,
    LM_STATUS = 0x06, LM_FIND = 0x07, LM_LEARNED = 0x08, LM_DIAG = 0x09, LM_EVENT = 0x10, LM_ERR = 0x7F
};
enum {  /* actions (LM_ACTION payload[0]) */
    LA_NOISE_CANCEL = 1, LA_GROUND_GRAB, LA_PINPOINT_ON, LA_PINPOINT_OFF, LA_LEARN, LA_FORGET,
    LA_MARK_FIND, LA_SAVE, LA_WIZARD_START, LA_WIZARD_STEP, LA_WIZARD_CANCEL, LA_POWER_OFF, LA_DEFAULTS, LA_ACCEPT_ALL
};
enum {  /* error codes (LM_ERR payload: [failed type][code]) */
    LE_BAD_CRC = 1, LE_NOT_AUTH, LE_BAD_PIN, LE_BAD_INDEX, LE_BAD_VALUE, LE_BAD_LEN, LE_UNKNOWN, LE_BUSY
};
enum {  /* events (LM_EVENT payload[0]) */
    LV_NC_DONE = 1, LV_FIND = 2, LV_LOW_BATT = 3, LV_WIZARD = 4, LV_COIL = 5, LV_SETTINGS = 6
};
enum {  /* status flag bits */
    LSF_PRESENT = 1, LSF_ACCEPTED = 2, LSF_FERROUS = 4, LSF_PINPOINT = 8, LSF_CHARGING = 16, LSF_LOW_BATT = 32, LSF_COIL = 64, LSF_NC_ACTIVE = 128
};
#define LINK_STATUS_LEN 55

typedef struct {
    detector_t *d; ui_t *u; logger_t *lg;
    int   connected, authed;
    uint32_t last_rx_ms;
    uint16_t status_period_ms;   /* 0 = no stream */
    uint32_t next_status_ms;
    /* receive parser */
    uint8_t rx[LINK_MAX_PAYLOAD + 6]; int rx_n, rx_len, rx_state;
    /* things the main loop must act on */
    int   mark_find, want_power_off;
    /* event edge detectors */
    int   last_nc_active, last_low_batt, last_wizard_step; uint32_t last_finds;
    /* counters for diagnostics / tests */
    uint32_t frames_ok, frames_bad, tx_frames;
    uint8_t tx[LINK_MAX_PAYLOAD + 6];
} link_t;

void link_init(link_t *l, detector_t *d, ui_t *u, logger_t *lg);
void link_rx_bytes(link_t *l, const uint8_t *b, int n);       /* feed bytes from the transport      */
void link_tick(link_t *l, uint32_t now_ms);                   /* status stream, events, timeouts    */
void link_disconnected(link_t *l);                            /* transport dropped: forget auth     */

/* helpers shared with the test bench / fixtures */
uint16_t link_crc16(const uint8_t *d, int n);
int  link_build_frame(uint8_t *out, int type, int seq, const uint8_t *payload, int len);   /* returns frame length */
int  link_pack_status(const link_t *l, uint8_t out[LINK_STATUS_LEN]);

/* transport: implemented by the HAL (sim: loopback buffers, target: BM83 transparent UART) */
int  hal_link_write(const uint8_t *b, int n);
int  hal_link_read(uint8_t *b, int max);
int  hal_link_connected(void);
#endif
