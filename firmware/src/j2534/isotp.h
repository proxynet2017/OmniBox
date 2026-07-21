
#ifndef ISOTP_H
#define ISOTP_H

#include <stdint.h>

typedef struct { uint8_t data[8]; uint8_t len; } isotp_frame_t;

enum { ISOTP_TX_IDLE = 0, ISOTP_TX_WAIT_FC, ISOTP_TX_SEND_CF };
enum { ISOTP_RX_IDLE = 0, ISOTP_RX_RECV };
enum {
  ISOTP_ERR_NONE = 0,
  ISOTP_ERR_LENGTH,
  ISOTP_ERR_SEQUENCE,
  ISOTP_ERR_OVERFLOW,
  ISOTP_ERR_FLOW_STATUS,
  ISOTP_ERR_WAIT_LIMIT,
};

typedef struct {
  
  uint8_t *rx_buf;   
  uint16_t rx_cap;
  uint8_t  fc_bs;   
  uint8_t  fc_stmin;  
  
  const uint8_t *tx_data; uint16_t tx_len, tx_idx;
  uint8_t tx_sn, tx_state, tx_bs_rem, tx_stmin, tx_wait_count;

  uint16_t rx_len, rx_idx;
  uint8_t rx_sn, rx_state, rx_bs_cnt, rx_done;
  uint8_t last_error;
} isotp_t;

void isotp_init(isotp_t *c, uint8_t *rx_buf, uint16_t rx_cap, uint8_t fc_bs, uint8_t fc_stmin);


int isotp_send_start(isotp_t *c, const uint8_t *data, uint16_t len, isotp_frame_t *out);


int isotp_send_next(isotp_t *c, isotp_frame_t *out);


int isotp_rx(isotp_t *c, const uint8_t *d, uint8_t n, isotp_frame_t *out, int *send);


uint8_t isotp_stmin_to_ms(uint8_t stmin);

#endif 
