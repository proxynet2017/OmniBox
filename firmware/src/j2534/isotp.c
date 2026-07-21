
#include "isotp.h"


#define PCI_SF 0x0u
#define PCI_FF 0x1u
#define PCI_CF 0x2u
#define PCI_FC 0x3u

#define FS_CTS  0x0u
#define FS_WAIT 0x1u
#define FS_OVFLW 0x2u
#define ISOTP_WAIT_MAX 3u

static uint16_t u16min(uint16_t a, uint16_t b) { return (a < b) ? a : b; }

uint8_t isotp_stmin_to_ms(uint8_t stmin)
{
  if (stmin <= 0x7Fu)         return stmin;  
  if (stmin >= 0xF1u && stmin <= 0xF9u) return 1u;  
  return 0x7Fu;                    
}

void isotp_init(isotp_t *c, uint8_t *rx_buf, uint16_t rx_cap, uint8_t fc_bs, uint8_t fc_stmin)
{
  c->rx_buf = rx_buf; c->rx_cap = rx_cap;
  c->fc_bs = fc_bs; c->fc_stmin = fc_stmin;
  c->tx_data = 0; c->tx_len = c->tx_idx = 0;
  c->tx_sn = c->tx_state = c->tx_bs_rem = c->tx_stmin = c->tx_wait_count = 0;
  c->rx_len = c->rx_idx = 0;
  c->rx_sn = c->rx_state = c->rx_bs_cnt = c->rx_done = 0;
  c->last_error = ISOTP_ERR_NONE;
}

int isotp_send_start(isotp_t *c, const uint8_t *data, uint16_t len, isotp_frame_t *out)
{
  c->last_error = ISOTP_ERR_NONE;
  if (len == 0u || len > 4095u) { c->last_error = ISOTP_ERR_LENGTH; return 0; }
  if (len <= 7u) {                  
    out->data[0] = (uint8_t)len;          
    for (uint16_t i = 0; i < len; i++) out->data[1 + i] = data[i];
    out->len = (uint8_t)(1u + len);
    c->tx_state = ISOTP_TX_IDLE;
    return 1;
  }
  
  out->data[0] = (uint8_t)(0x10u | ((len >> 8) & 0x0Fu));
  out->data[1] = (uint8_t)(len & 0xFFu);
  for (uint8_t i = 0; i < 6u; i++) out->data[2 + i] = data[i];
  out->len = 8u;
  c->tx_data = data; c->tx_len = len; c->tx_idx = 6u;
  c->tx_sn = 1u; c->tx_state = ISOTP_TX_WAIT_FC; c->tx_bs_rem = 0u; c->tx_wait_count = 0;
  return 1;
}

int isotp_send_next(isotp_t *c, isotp_frame_t *out)
{
  if (c->tx_state != ISOTP_TX_SEND_CF) return 0;
  uint16_t rem = (uint16_t)(c->tx_len - c->tx_idx);
  if (rem == 0u) { c->tx_state = ISOTP_TX_IDLE; return 0; }
  uint16_t cnt = u16min(7u, rem);
  out->data[0] = (uint8_t)(0x20u | (c->tx_sn & 0x0Fu));  
  for (uint16_t i = 0; i < cnt; i++) out->data[1 + i] = c->tx_data[c->tx_idx + i];
  out->len = (uint8_t)(1u + cnt);
  c->tx_idx = (uint16_t)(c->tx_idx + cnt);
  c->tx_sn = (uint8_t)((c->tx_sn + 1u) & 0x0Fu);
  if (c->tx_idx >= c->tx_len) {
    c->tx_state = ISOTP_TX_IDLE;            
  } else if (c->tx_bs_rem != 0u) {            
    if (--c->tx_bs_rem == 0u) c->tx_state = ISOTP_TX_WAIT_FC;  
  }
  return 1;
}

static void make_fc(const isotp_t *c, uint8_t fs, isotp_frame_t *out)
{
  out->data[0] = (uint8_t)(0x30u | (fs & 0x0Fu));
  out->data[1] = c->fc_bs;
  out->data[2] = c->fc_stmin;
  out->len = 3u;
}

int isotp_rx(isotp_t *c, const uint8_t *d, uint8_t n, isotp_frame_t *out, int *send)
{
  *send = 0;
  c->last_error = ISOTP_ERR_NONE;
  if (n < 1u) { c->last_error = ISOTP_ERR_LENGTH; return 0; }
  uint8_t pci = (uint8_t)((d[0] >> 4) & 0x0Fu);

  if (pci == PCI_SF) {
    uint16_t len = (uint16_t)(d[0] & 0x0Fu);
    if (len == 0u || len > 7u || n < (uint8_t)(1u + len)) { c->last_error = ISOTP_ERR_LENGTH; return 0; }
    if (len > c->rx_cap) { c->last_error = ISOTP_ERR_OVERFLOW; return 0; }
    for (uint16_t i = 0; i < len; i++) c->rx_buf[i] = d[1 + i];
    c->rx_len = len; c->rx_done = 1u; c->rx_state = ISOTP_RX_IDLE;
    return 1;
  }
  if (pci == PCI_FF) {
    if (n < 8u) { c->last_error = ISOTP_ERR_LENGTH; return 0; }
    uint16_t len = (uint16_t)(((uint16_t)(d[0] & 0x0Fu) << 8) | d[1]);
    if (len <= 7u) { c->last_error = ISOTP_ERR_LENGTH; return 0; }
    if (len > c->rx_cap) {
      make_fc(c, FS_OVFLW, out); *send = 1;
      c->last_error = ISOTP_ERR_OVERFLOW;
      c->rx_state = ISOTP_RX_IDLE;
      return 0;
    }
    uint16_t cnt = u16min(6u, len);
    for (uint16_t i = 0; i < cnt; i++) c->rx_buf[i] = d[2 + i];
    c->rx_len = len; c->rx_idx = cnt; c->rx_sn = 1u;
    c->rx_state = ISOTP_RX_RECV; c->rx_bs_cnt = c->fc_bs;
    make_fc(c, FS_CTS, out); *send = 1;         
    return 0;
  }
  if (pci == PCI_CF) {
    if (n < 2u) { c->last_error = ISOTP_ERR_LENGTH; return 0; }
    if (c->rx_state != ISOTP_RX_RECV) { c->last_error = ISOTP_ERR_SEQUENCE; return 0; }
    uint8_t sn = (uint8_t)(d[0] & 0x0Fu);
    if (sn != c->rx_sn) { c->rx_state = ISOTP_RX_IDLE; c->last_error = ISOTP_ERR_SEQUENCE; return 0; }
    uint16_t rem = (uint16_t)(c->rx_len - c->rx_idx);
    uint16_t cnt = u16min((uint16_t)(n - 1u), u16min(7u, rem));
    for (uint16_t i = 0; i < cnt; i++) c->rx_buf[c->rx_idx + i] = d[1 + i];
    c->rx_idx = (uint16_t)(c->rx_idx + cnt);
    c->rx_sn = (uint8_t)((c->rx_sn + 1u) & 0x0Fu);
    if (c->rx_idx >= c->rx_len) {
      c->rx_done = 1u; c->rx_state = ISOTP_RX_IDLE;
      return 1;                    
    }
    if (c->fc_bs != 0u && --c->rx_bs_cnt == 0u) {    
      make_fc(c, FS_CTS, out); *send = 1;
      c->rx_bs_cnt = c->fc_bs;
    }
    return 0;
  }
  if (pci == PCI_FC) {
    if (c->tx_state != ISOTP_TX_WAIT_FC || n < 3u) { c->last_error = ISOTP_ERR_LENGTH; return 0; }
    uint8_t fs = (uint8_t)(d[0] & 0x0Fu);
    if (fs == FS_CTS) {
      c->tx_bs_rem = d[1]; c->tx_stmin = d[2]; c->tx_state = ISOTP_TX_SEND_CF; c->tx_wait_count = 0;
    } else if (fs == FS_WAIT) {
      if (++c->tx_wait_count > ISOTP_WAIT_MAX) {
        c->tx_state = ISOTP_TX_IDLE; c->last_error = ISOTP_ERR_WAIT_LIMIT;
      }
    } else if (fs == FS_OVFLW) {
      c->tx_state = ISOTP_TX_IDLE; c->last_error = ISOTP_ERR_OVERFLOW;
    } else {
      c->tx_state = ISOTP_TX_IDLE; c->last_error = ISOTP_ERR_FLOW_STATUS;
    }
    return 0;
  }
  return 0;
}
