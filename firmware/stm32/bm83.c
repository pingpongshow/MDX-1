/* BM83 (IS2083 host-MCU mode) transport for the phone link: BLE "Transparent UART" service.
 *
 * The module is driven over USART1 (BT_TX/BT_RX, 115200 8N1) with Microchip's UART command
 * packets:   0xAA  len_hi  len_lo  opcode  params...  checksum
 * where len counts opcode + params and checksum is the two's complement of the byte sum of
 * len_hi .. last param. Events from the module use the same wrapper.
 *
 * !! Opcode numbers below are taken from the BM83 UART command set as remembered and MUST be
 * !! checked against Microchip's "BM83 Bluetooth Audio Module UART Command Set" (the IS2083
 * !! host MCU protocol document) before bring-up. Only this table should need editing; the
 * !! framing, ring buffers and hal_link_* functions do not depend on the numbers.
 *
 * Phone side: Microchip Transparent UART service 49535343-FE7D-4AE5-8FA9-9FAFD205E455,
 *   write characteristic 49535343-8841-43F4-A8D4-ECBE34729BB3 (phone -> module),
 *   notify characteristic 49535343-1E4D-4BD9-BA61-23C647249616 (module -> phone).            */
#include "stm32h7xx_hal.h"
#include "../core/link/link.h"
#include <string.h>

/* ---- BM83 opcodes (VERIFY) */
#define BM_CMD_MMI_ACTION          0x02   /* param: link index, MMI code                          */
#define BM_CMD_CHANGE_DEVICE_NAME  0x05   /* param: name bytes                                     */
#define BM_CMD_LE_SIGNALING        0x29   /* sub-command 0x01 = set advertising data / enable adv  */
#define BM_CMD_SEND_TRANSPARENT    0x12   /* param: link type (0x02 = BLE), then data              */
#define BM_EVT_TRANSPARENT_DATA    0x22   /* param: link type, then data                           */
#define BM_EVT_LE_CONNECTED        0x32   /* sub-event 0x00 connect / 0x01 disconnect              */
#define BM_EVT_CMD_ACK             0x00
#define BM_MMI_POWER_ON            0x51
#define BM_MMI_ENTER_PAIRING       0x5D
#define BM_LINK_BLE                0x02

extern UART_HandleTypeDef huart1;

/* Control lines (DS70005402G Figure 6-8, host-MCU interface over UART) */
#define BM_MFB_PORT   GPIOE
#define BM_MFB_PIN    GPIO_PIN_3      /* PWR(MFB) pin 26: power key and host->module wake            */
#define BM_RST_PORT   GPIOE
#define BM_RST_PIN    GPIO_PIN_7      /* RST_N pin 43, active low (module has an internal pull-up)   */
#define BM_WAKE_PORT  GPIOE
#define BM_WAKE_PIN   GPIO_PIN_1      /* P0_0 / UART_TX_IND pin 49, active high: module has data     */
#define BM_WAKE_IDLE_MS 500           /* after this long with no traffic, assume the module slept    */

static uint8_t rx_ring[1024]; static volatile int rx_wr, rx_rd;        /* payload bytes from the phone */
static uint8_t pkt[300]; static int pkt_n, pkt_len, pkt_state;
static int ble_connected;

/* True while P0_0 (UART_TX_IND) is asserted: the module is awake and has something for the host. */
int bm83_host_wake_asserted(void) { return HAL_GPIO_ReadPin(BM_WAKE_PORT, BM_WAKE_PIN) == GPIO_PIN_SET; }

static uint32_t bm_last_traffic_ms;

/* DS70005402G Figure 6-10: a module that has dropped to its 32 kHz low-power mode is woken with a
 * 1 ms MFB pulse, then after 2 ms the command is sent with MFB held high for longer than the command
 * slot. MFB is also the power key, so the pulse must stay far below the ~1 s power-off hold time.
 * Skipped while traffic is recent, so a burst of BLE chunks does not pulse the key on every frame. */
static void bm_wake(void)
{
    if ((uint32_t)(HAL_GetTick() - bm_last_traffic_ms) <= BM_WAKE_IDLE_MS) return;
    HAL_GPIO_WritePin(BM_MFB_PORT, BM_MFB_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(BM_MFB_PORT, BM_MFB_PIN, GPIO_PIN_RESET);
    HAL_Delay(2);
}

static void bm_send(uint8_t opcode, const uint8_t *p, int n)
{
    uint8_t b[300]; int len = n + 1, k = 0;
    b[k++] = 0xAA; b[k++] = (uint8_t)(len >> 8); b[k++] = (uint8_t)len; b[k++] = opcode;
    memcpy(b + k, p, n); k += n;
    uint8_t sum = 0; for (int i = 1; i < k; i++) sum = (uint8_t)(sum + b[i]);
    b[k++] = (uint8_t)(0x100 - sum);
    bm_wake();
    HAL_GPIO_WritePin(BM_MFB_PORT, BM_MFB_PIN, GPIO_PIN_SET);      /* hold MFB over the command slot */
    HAL_UART_Transmit(&huart1, b, (uint16_t)k, 50);
    HAL_GPIO_WritePin(BM_MFB_PORT, BM_MFB_PIN, GPIO_PIN_RESET);
    bm_last_traffic_ms = HAL_GetTick();
}

static void bm_event(uint8_t op, const uint8_t *p, int n)
{
    switch (op) {
    case BM_EVT_TRANSPARENT_DATA:
        if (n >= 1) for (int i = 1; i < n; i++) { int nx = (rx_wr + 1) % (int)sizeof rx_ring; if (nx != rx_rd) { rx_ring[rx_wr] = p[i]; rx_wr = nx; } }
        bm_last_traffic_ms = HAL_GetTick();
        break;
    case BM_EVT_LE_CONNECTED:
        if (n >= 1) ble_connected = (p[0] == 0x00);
        break;
    default: break;
    }
}

/* feed raw UART bytes (called from hal_stm32_poll) */
void bm83_rx_byte(uint8_t c)
{
    switch (pkt_state) {
    case 0: if (c == 0xAA) { pkt_n = 0; pkt_state = 1; } break;
    case 1: pkt_len = c << 8; pkt_state = 2; break;
    case 2: pkt_len |= c; if (pkt_len < 1 || pkt_len > (int)sizeof pkt - 2) pkt_state = 0; else pkt_state = 3; break;
    default:
        pkt[pkt_n++] = c;
        if (pkt_n == pkt_len + 1) {                                  /* opcode + params + checksum */
            uint8_t sum = (uint8_t)((pkt_len >> 8) + (pkt_len & 0xFF)); for (int i = 0; i < pkt_n; i++) sum = (uint8_t)(sum + pkt[i]);
            if (sum == 0) bm_event(pkt[0], pkt + 1, pkt_len - 1);
            pkt_state = 0;
        }
        break;
    }
}

void bm83_init(void)
{
    /* Power-on per DS70005402G Figure 6-12: hold RST_N low, raise MFB, release RST_N about 20 ms
     * later, then wait ~400 ms before the first UART command (the module NACKs until it is ready). */
    HAL_GPIO_WritePin(BM_RST_PORT, BM_RST_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BM_MFB_PORT, BM_MFB_PIN, GPIO_PIN_SET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(BM_RST_PORT, BM_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(400);
    bm_last_traffic_ms = HAL_GetTick();          /* module is awake: no MFB pulse needed yet */
    uint8_t mmi[2] = {0x00, BM_MMI_POWER_ON}; bm_send(BM_CMD_MMI_ACTION, mmi, 2); HAL_Delay(200);
    const char *name = "MDX-1"; bm_send(BM_CMD_CHANGE_DEVICE_NAME, (const uint8_t *)name, (int)strlen(name)); HAL_Delay(50);
    uint8_t adv[2] = {0x01, 0x01}; bm_send(BM_CMD_LE_SIGNALING, adv, 2);            /* enable BLE advertising */
}

/* ---- link transport (core/link/link.h) */
int hal_link_write(const uint8_t *b, int n)
{
    /* BLE notifications carry up to 20 bytes with the default MTU; chunk conservatively */
    for (int off = 0; off < n; off += 20) {
        uint8_t p[21]; int m = n - off > 20 ? 20 : n - off; p[0] = BM_LINK_BLE; memcpy(p + 1, b + off, m);
        bm_send(BM_CMD_SEND_TRANSPARENT, p, m + 1);
    }
    return n;
}
int hal_link_read(uint8_t *b, int max)
{
    int n = 0; while (n < max && rx_rd != rx_wr) { b[n++] = rx_ring[rx_rd]; rx_rd = (rx_rd + 1) % (int)sizeof rx_ring; }
    return n;
}
int hal_link_connected(void) { return ble_connected; }
