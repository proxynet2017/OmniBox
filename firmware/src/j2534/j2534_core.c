
#include <string.h>
#include "j2534_core.h"
#include "isotp.h"
#include "kline_asm.h"
#include "trace.h"
#include "../drivers/drivers.h"
#include "../board/board.h"


static void trace_can_frame(uint8_t tag, uint8_t ch, uint32_t id, const uint8_t *d, uint8_t n)
{
  if (!trace_enabled()) return;
  uint8_t b[12];
  b[0] = (uint8_t)(id >> 24); b[1] = (uint8_t)(id >> 16);
  b[2] = (uint8_t)(id >> 8); b[3] = (uint8_t)id;
  uint8_t m = (n > 8u) ? 8u : n;
  for (uint8_t i = 0; i < m; i++) b[4 + i] = d[i];
  trace_record(tag, ch, 0, board_micros(), b, (uint16_t)(4u + m));
}
static void trace_bytes(uint8_t tag, uint8_t ch, const uint8_t *d, uint16_t n)
{
  if (!trace_enabled()) return;
  trace_record(tag, ch, 0, board_micros(), d, n);
}
static void trace_event(uint8_t ch, uint8_t code)
{
  if (!trace_enabled()) return;
  trace_record(TRACE_EVT, ch, code, board_micros(), 0, 0);
}


#define J2534_KLINE_GAP_MS 10u


#define J2534_ISOTP_LINKS 2
#define J2534_ISOTP_BUF  J2534_MSG_DATA_MAX

typedef struct {
  uint8_t in_use;
  uint8_t ch_id;
  uint8_t can_phys;
  uint32_t tx_id;
  uint32_t rx_id;
  uint32_t tx_flags;
  isotp_t tp;
  uint8_t rxbuf[J2534_ISOTP_BUF];
  uint8_t txbuf[J2534_ISOTP_BUF];
  uint32_t next_cf_ms;
  uint32_t n_bs_deadline;
  uint32_t n_cr_deadline;
  uint8_t tx_active;
  uint8_t pad;
} isotp_link_t;

#define J2534_ISO15765_PAD_BYTE 0x00u


#define J2534_N_BS_MS 1000u
#define J2534_N_CR_MS 1000u

static isotp_link_t s_links[J2534_ISOTP_LINKS];
static kline_asm_t s_kasm[KLINE_CH_COUNT];
static j2534_channel_t s_ch[J2534_MAX_CHANNELS];
static int s_open;


static uint8_t s_rx_large[J2534_RX_LARGE_POOL][J2534_MSG_DATA_MAX];
static uint8_t s_rx_large_used[J2534_RX_LARGE_POOL];

static int rx_large_alloc(void)
{
  for (int i = 0; i < J2534_RX_LARGE_POOL; i++)
    if (!s_rx_large_used[i]) { s_rx_large_used[i] = 1; return i; }
  return -1;
}
static void rx_large_free(int idx)
{
  if (idx >= 0 && idx < J2534_RX_LARGE_POOL) s_rx_large_used[idx] = 0;
}

static int is_iso15765(uint16_t proto)
{
  return proto == J2534_ISO15765 || proto == J2534_ISO15765_PS ||
      proto == J2534_SW_ISO15765_PS;
}

static int is_kline(uint16_t proto)
{
  return proto == J2534_ISO9141 || proto == J2534_ISO14230 ||
      proto == J2534_ISO9141_PS || proto == J2534_ISO14230_PS;
}

static int is_j1850(uint16_t proto)
{
  return proto == J2534_J1850VPW || proto == J2534_J1850PWM ||
      proto == J2534_J1850VPW_PS || proto == J2534_J1850PWM_PS;
}


static uint32_t id_be(const uint8_t *d, uint32_t n)
{
  if (n < 4) return 0;
  return ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
      ((uint32_t)d[2] << 8) | (uint32_t)d[3];
}

static uint16_t copy_filter_data(uint8_t *dst, const j2534_msg_t *m)
{
  uint32_t n = m ? m->data_size : 0u;
  if (n > J2534_FILTER_DATA_MAX) n = J2534_FILTER_DATA_MAX;
  if (n && m) memcpy(dst, m->data, n);
  return (uint16_t)n;
}

void j2534_init(void)
{
  memset(s_ch, 0, sizeof(s_ch));
  memset(s_links, 0, sizeof(s_links));
  memset(s_rx_large_used, 0, sizeof(s_rx_large_used));
  for (int i = 0; i < KLINE_CH_COUNT; i++) kline_asm_init(&s_kasm[i]);
  trace_init();
  s_open = 0;
  can_init(); kline_init(); j1850_init(); swcan_init(); feps_init(); matrix_init();
}


static void chan_rx_clear(j2534_channel_t *c)
{
  while (c->rx_tail != c->rx_head) {
    rx_large_free(c->rxq[c->rx_tail].large);
    c->rx_tail = (uint16_t)((c->rx_tail + 1) % J2534_RXQ_DEPTH);
  }
  c->rx_head = c->rx_tail = 0;
  c->rx_overflow = 0;
}

static j2534_channel_t *chan(uint32_t id)
{
  if (id >= J2534_MAX_CHANNELS || !s_ch[id].in_use) return 0;
  return &s_ch[id];
}

uint8_t j2534_open(uint32_t *device_id)
{
  if (device_id) *device_id = 1;
  s_open = 1;
  return J2534_STATUS_NOERROR;
}

uint8_t j2534_close(void)
{
  for (uint32_t i = 0; i < J2534_MAX_CHANNELS; i++)
    if (s_ch[i].in_use) j2534_disconnect(i);
  s_open = 0;
  feps_off();
  return J2534_STATUS_NOERROR;
}

uint8_t j2534_connect(uint16_t protocol_id, uint32_t flags, uint32_t baud, uint32_t *channel_id)
{
  if (!s_open) return J2534_ERR_DEVICE_NOT_CONNECTED;
  for (uint32_t i = 0; i < J2534_MAX_CHANNELS; i++) {
    if (s_ch[i].in_use) continue;
    j2534_channel_t *c = &s_ch[i];
    memset(c, 0, sizeof(*c));
    c->in_use = 1; c->protocol_id = protocol_id; c->flags = flags; c->baudrate = baud;
    c->can_phys = 0xFF;
    c->kline_phys = 0xFF;

    switch (protocol_id) {
    case J2534_CAN: case J2534_ISO15765:
    case J2534_CAN_PS: case J2534_ISO15765_PS:
      c->can_phys = CAN_CH1;
      if (can_open(CAN_CH1, baud, baud, flags) != 0) { c->in_use = 0; return J2534_ERR_FAILED; }
      break;
    case J2534_SW_CAN_PS: case J2534_SW_ISO15765_PS:
      c->can_phys = SWCAN_MCP3;
      swcan_set_mode(0);
      if (can_open(SWCAN_MCP3, baud, baud, flags) != 0) { c->in_use = 0; return J2534_ERR_FAILED; }
      break;
    case J2534_ISO9141: case J2534_ISO14230:
    case J2534_ISO9141_PS: case J2534_ISO14230_PS:
      c->kline_phys = KLINE_CH1;
      kline_asm_reset(&s_kasm[KLINE_CH1]);
      kline_asm_set_mode(&s_kasm[KLINE_CH1],
        (protocol_id == J2534_ISO14230 || protocol_id == J2534_ISO14230_PS)
          ? KLINE_MODE_ISO14230 : KLINE_MODE_TIMING);
      kline_open(KLINE_CH1, baud ? baud : 10400); break;
    case J2534_J1850VPW: case J2534_J1850VPW_PS: j1850_set_mode_vpw(1); break;
    case J2534_J1850PWM: case J2534_J1850PWM_PS: j1850_set_mode_vpw(0); break;
    default: c->in_use = 0; return J2534_ERR_INVALID_PROTOCOL_ID;
    }
    if (channel_id) *channel_id = i;
    return J2534_STATUS_NOERROR;
  }
  return J2534_ERR_FAILED;
}


static void free_links_of(uint32_t ch_id)
{
  for (int i = 0; i < J2534_ISOTP_LINKS; i++)
    if (s_links[i].in_use && s_links[i].ch_id == ch_id)
      memset(&s_links[i], 0, sizeof(s_links[i]));
}

uint8_t j2534_disconnect(uint32_t channel_id)
{
  j2534_channel_t *c = chan(channel_id);
  if (!c) return J2534_ERR_INVALID_CHANNEL_ID;
  free_links_of(channel_id);
  chan_rx_clear(c);
  c->in_use = 0;
  return J2534_STATUS_NOERROR;
}



static isotp_link_t *link_alloc(void)
{
  for (int i = 0; i < J2534_ISOTP_LINKS; i++)
    if (!s_links[i].in_use) return &s_links[i];
  return 0;
}
static int link_index(const isotp_link_t *lk) { return (int)(lk - s_links); }

uint8_t j2534_start_filter_msg(uint32_t channel_id, uint8_t type,
                  const j2534_msg_t *mask, const j2534_msg_t *pattern,
                  const j2534_msg_t *flow, uint32_t *filter_id)
{
  j2534_channel_t *c = chan(channel_id);
  if (!c) return J2534_ERR_INVALID_CHANNEL_ID;
  if (!mask || !pattern || !flow) return J2534_ERR_NULL_PARAMETER;
  if (type != J2534_PASS_FILTER && type != J2534_BLOCK_FILTER &&
    type != J2534_FLOW_CONTROL_FILTER)
    return J2534_ERR_INVALID_MSG;
  if (type == J2534_FLOW_CONTROL_FILTER && !is_iso15765(c->protocol_id))
    return J2534_ERR_INVALID_MSG;

	  for (uint32_t i = 0; i < J2534_MAX_FILTERS; i++) {
	    if (c->filters[i].type != 0) continue;
	    j2534_filter_t *f = &c->filters[i];
	    f->mask  = id_be(mask->data, mask->data_size);
	    f->pattern = id_be(pattern->data, pattern->data_size);
	    f->link  = -1;
	    f->mask_len = copy_filter_data(f->mask_data, mask);
	    f->pattern_len = copy_filter_data(f->pattern_data, pattern);
	    f->flow_len = copy_filter_data(f->flow_data, flow);
	    if (type == J2534_FLOW_CONTROL_FILTER) {
	      isotp_link_t *lk = link_alloc();
	      if (!lk) return J2534_ERR_FAILED;
	      memset(lk, 0, sizeof(*lk));
	      lk->in_use = 1; lk->ch_id = (uint8_t)channel_id; lk->can_phys = c->can_phys;
	      lk->tx_id = id_be(flow->data, flow->data_size);
	      lk->rx_id = f->pattern;
	      isotp_init(&lk->tp, lk->rxbuf, sizeof(lk->rxbuf), 0, 0);
	      f->fc_id = lk->tx_id;
	      f->link = (int8_t)link_index(lk);
	    }
    f->type = type;
    if (filter_id) *filter_id = i;
    return J2534_STATUS_NOERROR;
  }
	  return J2534_ERR_FAILED;
}

uint8_t j2534_start_filter(uint32_t channel_id, uint8_t type,
              uint32_t mask_id, uint32_t pattern_id, uint32_t fc_id,
              uint32_t *filter_id)
{
  j2534_msg_t mask, pattern, flow;
  memset(&mask, 0, sizeof(mask));
  memset(&pattern, 0, sizeof(pattern));
  memset(&flow, 0, sizeof(flow));
  mask.data_size = pattern.data_size = flow.data_size = 4;
  mask.data[0] = (uint8_t)(mask_id >> 24); mask.data[1] = (uint8_t)(mask_id >> 16);
  mask.data[2] = (uint8_t)(mask_id >> 8); mask.data[3] = (uint8_t)mask_id;
  pattern.data[0] = (uint8_t)(pattern_id >> 24); pattern.data[1] = (uint8_t)(pattern_id >> 16);
  pattern.data[2] = (uint8_t)(pattern_id >> 8); pattern.data[3] = (uint8_t)pattern_id;
  flow.data[0] = (uint8_t)(fc_id >> 24); flow.data[1] = (uint8_t)(fc_id >> 16);
  flow.data[2] = (uint8_t)(fc_id >> 8); flow.data[3] = (uint8_t)fc_id;
  return j2534_start_filter_msg(channel_id, type, &mask, &pattern, &flow, filter_id);
}

uint8_t j2534_stop_filter(uint32_t channel_id, uint32_t filter_id)
{
  j2534_channel_t *c = chan(channel_id);
  if (!c) return J2534_ERR_INVALID_CHANNEL_ID;
  if (filter_id >= J2534_MAX_FILTERS || c->filters[filter_id].type == 0)
    return J2534_ERR_INVALID_MSG;
  j2534_filter_t *f = &c->filters[filter_id];
  if (f->link >= 0 && f->link < J2534_ISOTP_LINKS)
    memset(&s_links[f->link], 0, sizeof(s_links[f->link]));
  memset(f, 0, sizeof(*f));
  return J2534_STATUS_NOERROR;
}



uint8_t j2534_start_periodic(uint32_t channel_id, const uint8_t *data, uint16_t len,
               uint32_t tx_flags, uint32_t interval_ms, uint32_t *msg_id)
{
  j2534_channel_t *c = chan(channel_id);
  if (!c) return J2534_ERR_INVALID_CHANNEL_ID;
  if (!data || len == 0 || len > J2534_PERIODIC_MAX_DATA) return J2534_ERR_INVALID_MSG;
  if (interval_ms == 0) return J2534_ERR_INVALID_MSG;
  for (uint32_t i = 0; i < J2534_MAX_PERIODIC; i++) {
    if (c->periodics[i].in_use) continue;
    j2534_periodic_t *pm = &c->periodics[i];
    memset(pm, 0, sizeof(*pm));
    pm->interval_ms = interval_ms;
    pm->tx_flags = tx_flags;
    pm->len = len;
    memcpy(pm->data, data, len);
    pm->last_ms = board_millis();
    pm->in_use = 1;
    if (msg_id) *msg_id = i;
    return J2534_STATUS_NOERROR;
  }
  return J2534_ERR_FAILED;
}

uint8_t j2534_stop_periodic(uint32_t channel_id, uint32_t msg_id)
{
  j2534_channel_t *c = chan(channel_id);
  if (!c) return J2534_ERR_INVALID_CHANNEL_ID;
  if (msg_id >= J2534_MAX_PERIODIC || !c->periodics[msg_id].in_use)
    return J2534_ERR_INVALID_MSG;
  c->periodics[msg_id].in_use = 0;
  return J2534_STATUS_NOERROR;
}


void j2534_rx_push(uint32_t channel_id, const j2534_msg_t *msg)
{
  j2534_channel_t *c = chan(channel_id);
  if (!c || !msg) return;
  uint16_t next = (uint16_t)((c->rx_head + 1) % J2534_RXQ_DEPTH);
  if (next == c->rx_tail) { c->rx_overflow = 1; trace_event((uint8_t)channel_id, TRACE_EVT_RX_OVERFLOW); return; }

  uint32_t ds = msg->data_size;
  if (ds > J2534_MSG_DATA_MAX) ds = J2534_MSG_DATA_MAX;
  j2534_rx_slot_t *s = &c->rxq[c->rx_head];

  if (ds <= J2534_RX_INLINE) {
    s->large = -1;
    memcpy(s->inl, msg->data, ds);
  } else {
    int idx = rx_large_alloc();
    if (idx < 0) { c->rx_overflow = 1; trace_event((uint8_t)channel_id, TRACE_EVT_RX_OVERFLOW); return; }
    s->large = (int16_t)idx;
    memcpy(s_rx_large[idx], msg->data, ds);
  }
  s->protocol_id = msg->protocol_id; s->rx_status = msg->rx_status;
  s->tx_flags = msg->tx_flags; s->timestamp = msg->timestamp;
  s->extra_data_index = msg->extra_data_index; s->data_size = ds;
  c->rx_head = next;
}


static void rx_slot_to_msg(j2534_rx_slot_t *s, j2534_msg_t *out)
{
  memset(out, 0, sizeof(*out));
  out->protocol_id = s->protocol_id; out->rx_status = s->rx_status;
  out->tx_flags = s->tx_flags; out->timestamp = s->timestamp;
  out->extra_data_index = s->extra_data_index; out->data_size = s->data_size;
  if (s->large < 0) memcpy(out->data, s->inl, s->data_size);
  else { memcpy(out->data, s_rx_large[s->large], s->data_size); rx_large_free(s->large); }
}

uint8_t j2534_read_msgs(uint32_t channel_id, j2534_msg_t *out, uint32_t *count, uint32_t timeout_ms)
{
  j2534_channel_t *c = chan(channel_id);
  if (!c || !out || !count) return J2534_ERR_NULL_PARAMETER;
  uint32_t want = *count, got = 0;
  uint32_t t0 = board_millis();
  while (got < want) {
    if (c->rx_tail != c->rx_head) {
      rx_slot_to_msg(&c->rxq[c->rx_tail], &out[got++]);
      c->rx_tail = (uint16_t)((c->rx_tail + 1) % J2534_RXQ_DEPTH);
    } else {
      if ((board_millis() - t0) >= timeout_ms) break;
    }
  }
  *count = got;
  if (c->rx_overflow) {
    c->rx_overflow = 0;
    return J2534_ERR_BUFFER_OVERFLOW;
  }
  return got ? J2534_STATUS_NOERROR : (timeout_ms ? J2534_ERR_TIMEOUT : J2534_ERR_BUFFER_EMPTY);
}


static void link_can_tx(isotp_link_t *lk, const uint8_t *d, uint8_t n)
{
  trace_can_frame(TRACE_BUS_TX, lk->can_phys, lk->tx_id, d, n);
  if (lk->pad && n < 8u) {
    uint8_t b[8];
    for (uint8_t i = 0; i < n; i++) b[i] = d[i];
    for (uint8_t i = n; i < 8u; i++) b[i] = J2534_ISO15765_PAD_BYTE;
    can_tx(lk->can_phys, lk->tx_id, b, 8u, lk->tx_flags);
  } else {
    can_tx(lk->can_phys, lk->tx_id, d, n, lk->tx_flags);
  }
}


static uint8_t iso15765_write(j2534_channel_t *c, uint32_t tx_flags, const uint8_t *data, uint16_t n)
{
  uint32_t id = id_be(data, n);
  uint16_t plen = (n > 4) ? (uint16_t)(n - 4) : 0;
  if (plen == 0) return J2534_ERR_INVALID_MSG;
  int fi = j2534_find_flow_by_tx(c->filters, J2534_MAX_FILTERS, id);
  if (fi < 0) {

    if (plen > 7u) return J2534_ERR_INVALID_MSG;
    uint8_t sf[8];
    sf[0] = (uint8_t)plen;
    memcpy(sf + 1, data + 4, plen);
    uint8_t flen = (uint8_t)(1u + plen);
    if (tx_flags & J2534_ISO15765_FRAME_PAD) {
      for (uint8_t i = flen; i < 8u; i++) sf[i] = J2534_ISO15765_PAD_BYTE;
      flen = 8u;
    }
    trace_can_frame(TRACE_BUS_TX, c->can_phys, id, sf, flen);
    can_tx(c->can_phys, id, sf, flen, tx_flags);
    return J2534_STATUS_NOERROR;
  }
  isotp_link_t *lk = &s_links[c->filters[fi].link];
  if (plen > sizeof(lk->txbuf)) return J2534_ERR_INVALID_MSG;
  memcpy(lk->txbuf, data + 4, plen);
	    lk->tx_flags = tx_flags;
	    if (c->can_fd) lk->tx_flags |= OMNI_CAN_FD_FRAME;
  lk->pad = (tx_flags & J2534_ISO15765_FRAME_PAD) ? 1u : 0u;
  isotp_frame_t f;
  if (!isotp_send_start(&lk->tp, lk->txbuf, plen, &f)) return J2534_ERR_INVALID_MSG;
  link_can_tx(lk, f.data, f.len);
  if (lk->tp.tx_state == ISOTP_TX_WAIT_FC) {
    lk->tx_active = 1u;
    lk->n_bs_deadline = board_millis() + J2534_N_BS_MS;
  } else {
    lk->tx_active = 0u;
  }
  return J2534_STATUS_NOERROR;
}


static uint8_t j2534_tx_one(j2534_channel_t *c, uint32_t tx_flags, const uint8_t *data, uint16_t n)
{
  switch (c->protocol_id) {
  case J2534_ISO15765: case J2534_ISO15765_PS: case J2534_SW_ISO15765_PS:
    return iso15765_write(c, tx_flags, data, n);
	  case J2534_CAN: case J2534_CAN_PS:
	    trace_can_frame(TRACE_BUS_TX, c->can_phys, id_be(data, n), data + 4, (uint8_t)(n > 4 ? n - 4 : 0));
	    can_tx(c->can_phys, id_be(data, n), data + 4, (uint8_t)(n > 4 ? n - 4 : 0),
	        tx_flags | (c->can_fd ? OMNI_CAN_FD_FRAME : 0u));
	    return J2534_STATUS_NOERROR;
  case J2534_ISO9141: case J2534_ISO14230:
    trace_bytes(TRACE_BUS_TX, c->kline_phys, data, n);
    kline_tx(c->kline_phys, data, n); return J2534_STATUS_NOERROR;
  case J2534_J1850VPW: case J2534_J1850PWM:
    trace_bytes(TRACE_BUS_TX, 0xFF, data, n);
    j1850_tx(data, n); return J2534_STATUS_NOERROR;
  default: return J2534_ERR_NOT_SUPPORTED;
  }
}

static j2534_msg_t s_loopback;

uint8_t j2534_write_msgs(uint32_t channel_id, const j2534_msg_t *in, uint32_t count, uint32_t timeout_ms)
{
  (void)timeout_ms;
  j2534_channel_t *c = chan(channel_id);
  if (!c || !in) return J2534_ERR_NULL_PARAMETER;
  for (uint32_t i = 0; i < count; i++) {
    uint8_t rc = j2534_tx_one(c, in[i].tx_flags, in[i].data, (uint16_t)in[i].data_size);
    if (rc != J2534_STATUS_NOERROR) return rc;
    if (c->loopback) {
      s_loopback = in[i];
      s_loopback.rx_status |= J2534_TX_MSG_TYPE;
      j2534_rx_push(channel_id, &s_loopback);
    }
  }
  return J2534_STATUS_NOERROR;
}

static uint32_t le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


static void chan_links_set_fc(j2534_channel_t *c, int set_bs, uint8_t bs, int set_st, uint8_t st)
{
  for (uint32_t i = 0; i < J2534_MAX_FILTERS; i++) {
    int8_t li = c->filters[i].link;
    if (c->filters[i].type != J2534_FLOW_CONTROL_FILTER || li < 0 || li >= J2534_ISOTP_LINKS) continue;
    if (set_bs) s_links[li].tp.fc_bs = bs;
    if (set_st) s_links[li].tp.fc_stmin = st;
  }
}

static void apply_config(j2534_channel_t *c, uint32_t param, uint32_t value)
{
  switch (param) {
  case J2534_CFG_ISO15765_BS: case J2534_CFG_BS_TX:
    c->cfg_bs = (uint8_t)value; chan_links_set_fc(c, 1, (uint8_t)value, 0, 0); break;
  case J2534_CFG_ISO15765_STMIN: case J2534_CFG_STMIN_TX:
    c->cfg_stmin = (uint8_t)value; chan_links_set_fc(c, 0, 0, 1, (uint8_t)value); break;
	  case J2534_CFG_LOOPBACK: c->loopback = value ? 1u : 0u; break;
	  case J2534_CFG_DATA_RATE:
	    c->baudrate = value;
	    if (c->can_phys != 0xFF) can_open(c->can_phys, c->baudrate, c->data_baudrate, c->flags);
	    else if (c->kline_phys != 0xFF) kline_open(c->kline_phys, c->baudrate);
	    break;
	  case OMNI_CFG_PHYSICAL_BUS:
	    if (value < CAN_CH_COUNT) {
	      if (c->can_phys != 0xFF) can_close(c->can_phys);
	      c->can_phys = (uint8_t)value;
	      can_open(c->can_phys, c->baudrate, c->data_baudrate, c->flags);
	    }
	    break;
	  case OMNI_CFG_OBD_PIN:
	    c->obd_pin = (uint8_t)value;
	    if (c->can_phys >= CAN_CH2 && c->can_phys <= CAN_CH4_MCP1) matrix_can_connect(c->can_phys + 1u);
	    else if (c->kline_phys == KLINE_CH2 || c->kline_phys == KLINE_CH3) matrix_kline_route(c->kline_phys + 1u, c->obd_pin);
	    break;
	  case OMNI_CFG_TERMINATION: c->termination = value ? 1u : 0u; matrix_can1_termination(c->termination); break;
	  case OMNI_CFG_CAN_SWAP: c->can_swap = value ? 1u : 0u; matrix_can1_polarity_swap(c->can_swap); break;
	  case OMNI_CFG_CAN_DATA_RATE: c->data_baudrate = value; if (c->can_phys != 0xFF) can_open(c->can_phys, c->baudrate, c->data_baudrate, c->flags); break;
	  case OMNI_CFG_CAN_FD: c->can_fd = value ? 1u : 0u; break;
	  case OMNI_CFG_SWCAN_MODE: c->swcan_mode = (uint8_t)value; swcan_set_mode(c->swcan_mode); break;
	  default: break;
	  }
}

static uint32_t read_config(const j2534_channel_t *c, uint32_t param)
{
  switch (param) {
  case J2534_CFG_ISO15765_BS: case J2534_CFG_BS_TX:    return c->cfg_bs;
  case J2534_CFG_ISO15765_STMIN: case J2534_CFG_STMIN_TX: return c->cfg_stmin;
	  case J2534_CFG_LOOPBACK:                return c->loopback;
	  case J2534_CFG_DATA_RATE:                return c->baudrate;
	  case OMNI_CFG_PHYSICAL_BUS:              return c->can_phys == 0xFF ? 0xFFFFFFFFu : c->can_phys;
	  case OMNI_CFG_OBD_PIN:                return c->obd_pin;
	  case OMNI_CFG_TERMINATION:              return c->termination;
	  case OMNI_CFG_CAN_SWAP:                return c->can_swap;
	  case OMNI_CFG_CAN_DATA_RATE:             return c->data_baudrate;
	  case OMNI_CFG_CAN_FD:                 return c->can_fd;
	  case OMNI_CFG_SWCAN_MODE:              return c->swcan_mode;
	  default: return 0;
	  }
}

uint8_t j2534_ioctl(uint32_t channel_id, uint32_t ioctl_id,
          const uint8_t *in, uint16_t in_len, uint8_t *out, uint16_t *out_len)
{

  if (ioctl_id == J2534_READ_VBATT || ioctl_id == J2534_READ_PROG_VOLTAGE) {
    uint32_t mv = 0;
    if (ioctl_id == J2534_READ_VBATT) feps_read_vbatt_mv(&mv); else feps_read_prog_mv(&mv);
    if (out && out_len && *out_len >= 4) { memcpy(out, &mv, 4); *out_len = 4; }
    return J2534_STATUS_NOERROR;
  }

  j2534_channel_t *c = chan(channel_id);
  if (!c) return J2534_ERR_INVALID_CHANNEL_ID;

  switch (ioctl_id) {
  case J2534_SET_CONFIG: {
    if (!in || in_len < 4) return J2534_ERR_NULL_PARAMETER;
    uint32_t n = le32(in);
    if (4u + n * 8u > in_len) return J2534_ERR_INVALID_MSG;
    for (uint32_t i = 0; i < n; i++)
      apply_config(c, le32(in + 4 + i * 8), le32(in + 8 + i * 8));
    return J2534_STATUS_NOERROR; }
  case J2534_GET_CONFIG: {
    if (!in || in_len < 4 || !out || !out_len) return J2534_ERR_NULL_PARAMETER;
    uint32_t n = le32(in);
    if (4u + n * 8u > in_len || 4u + n * 8u > *out_len) return J2534_ERR_INVALID_MSG;
    out[0] = (uint8_t)n; out[1] = (uint8_t)(n >> 8); out[2] = (uint8_t)(n >> 16); out[3] = (uint8_t)(n >> 24);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t param = le32(in + 4 + i * 8);
      uint32_t value = read_config(c, param);
      uint8_t *o = out + 4 + i * 8;
      o[0] = (uint8_t)param; o[1] = (uint8_t)(param >> 8); o[2] = (uint8_t)(param >> 16); o[3] = (uint8_t)(param >> 24);
      o[4] = (uint8_t)value; o[5] = (uint8_t)(value >> 8); o[6] = (uint8_t)(value >> 16); o[7] = (uint8_t)(value >> 24);
    }
    *out_len = (uint16_t)(4u + n * 8u);
    return J2534_STATUS_NOERROR; }
  case J2534_CLEAR_RX_BUFFER:
    chan_rx_clear(c); return J2534_STATUS_NOERROR;
  case J2534_CLEAR_TX_BUFFER:
    for (uint32_t i = 0; i < J2534_MAX_FILTERS; i++) {
      int8_t li = c->filters[i].link;
      if (c->filters[i].type == J2534_FLOW_CONTROL_FILTER && li >= 0 && li < J2534_ISOTP_LINKS) {
        s_links[li].tp.tx_state = ISOTP_TX_IDLE; s_links[li].tx_active = 0;
      }
    }
    return J2534_STATUS_NOERROR;
  case J2534_CLEAR_MSG_FILTERS:
    for (uint32_t i = 0; i < J2534_MAX_FILTERS; i++) {
      int8_t li = c->filters[i].link;
      if (li >= 0 && li < J2534_ISOTP_LINKS) memset(&s_links[li], 0, sizeof(s_links[li]));
    }
    memset(c->filters, 0, sizeof(c->filters));
    return J2534_STATUS_NOERROR;
  case J2534_CLEAR_PERIODIC_MSGS:
    memset(c->periodics, 0, sizeof(c->periodics));
    return J2534_STATUS_NOERROR;
	  case J2534_FIVE_BAUD_INIT:
	    if (!is_kline(c->protocol_id) || c->kline_phys == 0xFF) return J2534_ERR_NOT_SUPPORTED;
	    if (!in || in_len < 1 || !out || !out_len || *out_len < 2) return J2534_ERR_NULL_PARAMETER;
	    {
	      uint8_t kb[2] = {0, 0};
	      if (kline_five_baud_init(c->kline_phys, in[0], kb) != 0) return J2534_ERR_FAILED;
	      out[0] = kb[0]; out[1] = kb[1]; *out_len = 2;
	      return J2534_STATUS_NOERROR;
	    }
	  case J2534_FAST_INIT:
	    if (!is_kline(c->protocol_id) || c->kline_phys == 0xFF) return J2534_ERR_NOT_SUPPORTED;
	    if (!out_len) return J2534_ERR_NULL_PARAMETER;
	    {
	      uint8_t rn = (uint8_t)*out_len;
	      if (kline_fast_init(c->kline_phys, in, (uint8_t)in_len, out, &rn) != 0) return J2534_ERR_FAILED;
	      *out_len = rn;
	      return J2534_STATUS_NOERROR;
	    }
  default: return J2534_ERR_NOT_SUPPORTED;
  }
}

uint8_t j2534_set_prog_voltage(uint32_t pin, uint32_t millivolts)
{
  if (millivolts == J2534_VOLTAGE_OFF)    { feps_off(); return J2534_STATUS_NOERROR; }
  if (millivolts == J2534_SHORT_TO_GROUND)  { feps_short_to_ground((uint8_t)pin); return J2534_STATUS_NOERROR; }
  if (millivolts > J2534_FEPS_MAX_MV)    return J2534_ERR_NOT_SUPPORTED;
  feps_route_to_pin((uint8_t)pin);
  feps_set_voltage_mv(millivolts);
  return J2534_STATUS_NOERROR;
}

uint8_t j2534_read_vbatt(uint32_t *millivolts)
{
  return feps_read_vbatt_mv(millivolts) == 0 ? J2534_STATUS_NOERROR : J2534_ERR_FAILED;
}




static void push_isotp_msg(const j2534_channel_t *c, uint32_t ch_id, isotp_link_t *lk)
{
  j2534_msg_t m; memset(&m, 0, sizeof(m));
  m.protocol_id = c->protocol_id;
  if (lk->rx_id > 0x7FFu) m.rx_status |= J2534_CAN_29BIT_ID;
  m.data[0] = (uint8_t)(lk->rx_id >> 24); m.data[1] = (uint8_t)(lk->rx_id >> 16);
  m.data[2] = (uint8_t)(lk->rx_id >> 8); m.data[3] = (uint8_t)lk->rx_id;
  uint16_t n = lk->tp.rx_len;
  if (n > (J2534_MSG_DATA_MAX - 4)) n = J2534_MSG_DATA_MAX - 4;
  memcpy(m.data + 4, lk->rxbuf, n);
  m.data_size = 4u + n;
  m.extra_data_index = m.data_size;
  j2534_rx_push(ch_id, &m);
  lk->tp.rx_done = 0; lk->tp.rx_state = ISOTP_RX_IDLE;
}


static void j2534_drain_can(uint32_t ch_id, j2534_channel_t *c)
{
  uint32_t id, flags; uint8_t data[64], len;
  while (can_read(c->can_phys, &id, data, &len, &flags)) {
    trace_can_frame(TRACE_BUS_RX, c->can_phys, id, data, len);
    if (is_iso15765(c->protocol_id)) {
      int fi = j2534_find_flow_by_rx(c->filters, J2534_MAX_FILTERS, id);
      if (fi < 0) continue;
      isotp_link_t *lk = &s_links[c->filters[fi].link];
      isotp_frame_t out; int send = 0;
      int done = isotp_rx(&lk->tp, data, len, &out, &send);
      if (send) link_can_tx(lk, out.data, out.len);
      if (done) push_isotp_msg(c, ch_id, lk);

      if (lk->tp.rx_state == ISOTP_RX_RECV) lk->n_cr_deadline = board_millis() + J2534_N_CR_MS;

      if (lk->tp.tx_state == ISOTP_TX_SEND_CF) { lk->tx_active = 1; lk->next_cf_ms = board_millis(); }
      continue;
    }
	    if (!j2534_filters_accept_msg(c->filters, J2534_MAX_FILTERS, id, data, len)) continue;
    j2534_msg_t m; memset(&m, 0, sizeof(m));
    m.protocol_id = c->protocol_id;
    m.rx_status |= flags & (J2534_CAN_29BIT_ID | OMNI_CAN_FD_FRAME | OMNI_CAN_BRS);
    m.data[0] = (uint8_t)(id >> 24); m.data[1] = (uint8_t)(id >> 16);
    m.data[2] = (uint8_t)(id >> 8); m.data[3] = (uint8_t)id;
    for (uint8_t i = 0; i < len; i++) m.data[4 + i] = data[i];
    m.data_size = 4u + len;
    m.extra_data_index = m.data_size;
    j2534_rx_push(ch_id, &m);
  }
}


static void kline_push_msg(uint32_t ch_id, j2534_channel_t *c, kline_asm_t *a)
{
  j2534_msg_t m; memset(&m, 0, sizeof(m));
  m.protocol_id = c->protocol_id;
  uint16_t n = a->len;
  if (n > J2534_MSG_DATA_MAX) n = J2534_MSG_DATA_MAX;
  memcpy(m.data, a->buf, n);
  m.data_size = n;
  m.extra_data_index = m.data_size;
  trace_bytes(TRACE_BUS_RX, c->kline_phys, m.data, n);
  j2534_rx_push(ch_id, &m);
  kline_asm_reset(a);
}

static void j2534_drain_kline(uint32_t ch_id, j2534_channel_t *c)
{
  kline_asm_t *a = &s_kasm[c->kline_phys];
  uint8_t b;
  while (kline_read(c->kline_phys, &b)) {
    kline_asm_push(a, b, board_millis());
    if (kline_asm_ready_len(a))
      kline_push_msg(ch_id, c, a);
  }

  if (kline_asm_ready(a, board_millis(), J2534_KLINE_GAP_MS))
    kline_push_msg(ch_id, c, a);
}


static void j2534_drain_j1850(uint32_t ch_id, j2534_channel_t *c)
{
  uint8_t buf[16]; uint16_t n = 0;
  if (j1850_read(buf, &n)) {
    j2534_msg_t m; memset(&m, 0, sizeof(m));
    m.protocol_id = c->protocol_id;
    if (n > sizeof(buf)) n = sizeof(buf);
    memcpy(m.data, buf, n);
    m.data_size = n;
    m.extra_data_index = m.data_size;
    trace_bytes(TRACE_BUS_RX, 0xFF, m.data, n);
    j2534_rx_push(ch_id, &m);
  }
}


static void j2534_pace_isotp_tx(void)
{
  uint32_t now = board_millis();
  for (int i = 0; i < J2534_ISOTP_LINKS; i++) {
    isotp_link_t *lk = &s_links[i];
    if (!lk->in_use || !lk->tx_active) continue;
    if (lk->tp.tx_state != ISOTP_TX_SEND_CF) continue;
    if ((int32_t)(now - lk->next_cf_ms) < 0) continue;
    isotp_frame_t f;
    if (isotp_send_next(&lk->tp, &f)) {
      link_can_tx(lk, f.data, f.len);
      lk->next_cf_ms = now + isotp_stmin_to_ms(lk->tp.tx_stmin);
    }
    if (lk->tp.tx_state == ISOTP_TX_IDLE)    lk->tx_active = 0;
    else if (lk->tp.tx_state == ISOTP_TX_WAIT_FC)
      lk->n_bs_deadline = now + J2534_N_BS_MS;
  }
}


static void j2534_check_isotp_timeouts(void)
{
  uint32_t now = board_millis();
  for (int i = 0; i < J2534_ISOTP_LINKS; i++) {
    isotp_link_t *lk = &s_links[i];
    if (!lk->in_use) continue;
    if (lk->tx_active && lk->tp.tx_state == ISOTP_TX_WAIT_FC &&
      (int32_t)(now - lk->n_bs_deadline) >= 0) {
      lk->tp.tx_state = ISOTP_TX_IDLE; lk->tx_active = 0;
      trace_event(lk->can_phys, TRACE_EVT_NBS_TIMEOUT);
    }
    if (lk->tp.rx_state == ISOTP_RX_RECV &&
      (int32_t)(now - lk->n_cr_deadline) >= 0) {
      lk->tp.rx_state = ISOTP_RX_IDLE; lk->tp.rx_done = 0;
      trace_event(lk->can_phys, TRACE_EVT_NCR_TIMEOUT);
    }
  }
}


static void j2534_run_periodics(void)
{
  uint32_t now = board_millis();
  for (uint32_t i = 0; i < J2534_MAX_CHANNELS; i++) {
    j2534_channel_t *c = &s_ch[i];
    if (!c->in_use) continue;
    for (uint32_t k = 0; k < J2534_MAX_PERIODIC; k++) {
      j2534_periodic_t *pm = &c->periodics[k];
      if (!pm->in_use) continue;
      if ((uint32_t)(now - pm->last_ms) < pm->interval_ms) continue;
      j2534_tx_one(c, pm->tx_flags, pm->data, pm->len);
      pm->last_ms = now;
    }
  }
}

void j2534_service(void)
{
  can_service(); kline_service(); j1850_service();
  for (uint32_t i = 0; i < J2534_MAX_CHANNELS; i++) {
    j2534_channel_t *c = &s_ch[i];
    if (!c->in_use) continue;
    if (c->can_phys != 0xFF)    j2534_drain_can(i, c);
    else if (is_kline(c->protocol_id) && c->kline_phys != 0xFF) j2534_drain_kline(i, c);
    else if (is_j1850(c->protocol_id)) j2534_drain_j1850(i, c);
  }
  j2534_pace_isotp_tx();
  j2534_check_isotp_timeouts();
  j2534_run_periodics();
}
