
#include <string.h>
#include "protocol.h"
#include "../j2534/j2534_core.h"
#include "../j2534/j2534_wire.h"
#include "../j2534/trace.h"
#include "../board/board.h"
#include "../drivers/drivers.h"
#include "../usb/usb_mode.h"

uint16_t proto_crc16_upd(uint16_t crc, const uint8_t *p, size_t n)
{
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)p[i] << 8;
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

uint16_t proto_crc16(const uint8_t *p, size_t n) { return proto_crc16_upd(0xFFFF, p, n); }


enum { S_SOF, S_LEN0, S_LEN1, S_SEQ, S_CMD, S_PAYLOAD, S_CRC0, S_CRC1 };
static uint8_t st = S_SOF;
static uint16_t len, idx;
static uint8_t seq, cmd;
static uint16_t crc_rx;
#define TRANSPORT_PAYLOAD_MAX (64u + (3u * (J2534_WIRE_MSG_HDR + J2534_FILTER_DATA_MAX)))
static uint8_t buf[8 + TRANSPORT_PAYLOAD_MAX];

void transport_rx_byte(uint8_t b)
{
  switch (st) {
  case S_SOF:  if (b == PROTO_SOF) st = S_LEN0; break;
  case S_LEN0: len = b; st = S_LEN1; break;
  case S_LEN1: len |= (uint16_t)b << 8;
         if (len > TRANSPORT_PAYLOAD_MAX) { st = S_SOF; break; }
         st = S_SEQ; break;
  case S_SEQ:  seq = b; buf[0] = b; st = S_CMD; break;
  case S_CMD:  cmd = b; buf[1] = b; idx = 0; st = len ? S_PAYLOAD : S_CRC0; break;
  case S_PAYLOAD: buf[2 + idx++] = b; if (idx >= len) st = S_CRC0; break;
  case S_CRC0: crc_rx = b; st = S_CRC1; break;
  case S_CRC1: crc_rx |= (uint16_t)b << 8;
         if (crc_rx == proto_crc16(buf, (size_t)(2 + len))) {
           proto_frame_t f = { seq, cmd, len, buf + 2 };
           proto_dispatch(&f);
         }
         st = S_SOF; break;
  }
}

void transport_poll(void) { }


static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24); }


static j2534_msg_t s_scratch;
static uint8_t   s_respbuf[4u + J2534_WIRE_MSG_HDR + J2534_MSG_DATA_MAX];


static uint8_t do_read_msgs(uint32_t cid, uint32_t max, uint32_t timeout,
              const uint8_t **out, uint16_t *out_n)
{
  uint32_t count = 0, off = 4;
  uint8_t first_status = J2534_STATUS_NOERROR;
  for (uint32_t k = 0; k < max; k++) {
    uint32_t one = 1;
    uint8_t rc = j2534_read_msgs(cid, &s_scratch, &one, k == 0 ? timeout : 0);
    if (rc != J2534_STATUS_NOERROR || one == 0) { if (k == 0) first_status = rc; break; }
    int w = j2534_wire_encode_msg(s_respbuf + off, (uint32_t)sizeof(s_respbuf) - off, &s_scratch);
    if (w < 0) break;
    off += (uint32_t)w; count++;
  }
  j2534_wire_wr32(s_respbuf, count);
  *out = s_respbuf; *out_n = (uint16_t)off;
  return count ? J2534_STATUS_NOERROR : first_status;
}


static uint8_t do_write_msgs(uint32_t cid, uint32_t timeout, uint32_t count,
               const uint8_t *p, uint32_t avail, uint32_t *written)
{
  uint32_t off = 0; *written = 0;
  for (uint32_t k = 0; k < count; k++) {
    int r = j2534_wire_decode_msg(p + off, avail - off, &s_scratch);
    if (r < 0) return J2534_ERR_INVALID_MSG;
    off += (uint32_t)r;
    uint8_t rc = j2534_write_msgs(cid, &s_scratch, 1, timeout);
    if (rc != J2534_STATUS_NOERROR) return rc;
    (*written)++;
  }
  return J2534_STATUS_NOERROR;
}


static uint8_t do_read_trace(const uint8_t **out, uint16_t *out_n)
{
  uint16_t count = 0, off = 2;
  trace_evt_t e;
  while ((uint32_t)off + 9u + TRACE_DATA_MAX <= sizeof(s_respbuf) && trace_pop(&e)) {
    uint8_t dlen = (e.len > TRACE_DATA_MAX) ? (uint8_t)TRACE_DATA_MAX : e.len;
    j2534_wire_wr32(s_respbuf + off, e.ts); off += 4;
    s_respbuf[off++] = e.tag; s_respbuf[off++] = e.channel;
    s_respbuf[off++] = e.code; s_respbuf[off++] = e.len; s_respbuf[off++] = dlen;
    for (uint8_t i = 0; i < dlen; i++) s_respbuf[off++] = e.data[i];
    count++;
  }
  s_respbuf[0] = (uint8_t)count; s_respbuf[1] = (uint8_t)(count >> 8);
  *out = s_respbuf; *out_n = off;
  return J2534_STATUS_NOERROR;
}

static void wr32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint8_t do_start_filter(const uint8_t *p, uint16_t len, uint8_t *out, uint16_t *out_n)
{
  if (len >= 8u + 3u * J2534_WIRE_MSG_HDR) {
    uint32_t cid = rd32(p);
    uint32_t type = rd32(p + 4);
    uint32_t off = 8;
    j2534_msg_t mask, pattern, flow;
    int r = j2534_wire_decode_msg(p + off, len - off, &mask);
    if (r < 0) return J2534_ERR_INVALID_MSG;
    off += (uint32_t)r;
    r = j2534_wire_decode_msg(p + off, len - off, &pattern);
    if (r < 0) return J2534_ERR_INVALID_MSG;
    off += (uint32_t)r;
    r = j2534_wire_decode_msg(p + off, len - off, &flow);
    if (r < 0) return J2534_ERR_INVALID_MSG;
    uint32_t fid = 0;
    uint8_t status = j2534_start_filter_msg(cid, (uint8_t)type, &mask, &pattern, &flow, &fid);
    memcpy(out, &fid, 4); *out_n = 4;
    return status;
  }
  if (len >= 20u) {
    uint32_t fid = 0;
    uint8_t status = j2534_start_filter(rd32(p), (uint8_t)rd32(p + 4),
                      rd32(p + 8), rd32(p + 12), rd32(p + 16), &fid);
    memcpy(out, &fid, 4); *out_n = 4;
    return status;
  }
  return J2534_ERR_NULL_PARAMETER;
}

void proto_dispatch(const proto_frame_t *f)
{
  uint8_t status = J2534_STATUS_NOERROR;
  uint8_t out[64]; uint16_t out_n = 0;
  const uint8_t *rdata = out;
  const uint8_t *p = f->payload;

  if (trace_enabled() && f->cmd != CMD_TRACE)
    trace_record(TRACE_USB_RX, 0xFF, f->cmd, board_micros(), f->payload, f->len);

  switch (f->cmd) {
  case CMD_PING: break;
  case CMD_GET_CAPS:
    wr32(out + 0, OMNIBOX_PROTO_VERSION);
    wr32(out + 4, OMNI_CAP_J2534_CAN | OMNI_CAP_J2534_ISO15765 |
              OMNI_CAP_MULTI_CAN | OMNI_CAP_ROUTING_MATRIX |
              OMNI_CAP_ELM327_CAN | OMNI_CAP_KLINE_EXPERIMENTAL |
              OMNI_CAP_J1850_EXPERIMENTAL | OMNI_CAP_CANFD_EXPERIMENTAL);
    wr32(out + 8, CAN_CH_COUNT);
    wr32(out + 12, KLINE_CH_COUNT);
    out_n = 16;
    break;
  case CMD_READ_VERSION: {

    static const char fwv[] = "OmniBox FW 0.1 dev HW:1.0 beta";
    static const char apiv[] = "04.04";
    uint16_t k = 0;
    memcpy(out + k, fwv, sizeof fwv); k = (uint16_t)(k + sizeof fwv);
    memcpy(out + k, apiv, sizeof apiv); k = (uint16_t)(k + sizeof apiv);
    out_n = k; break; }
  case CMD_OPEN: { uint32_t dev; status = j2534_open(&dev); memcpy(out, &dev, 4); out_n = 4; break; }
  case CMD_CLOSE: status = j2534_close(); break;
  case CMD_CONNECT: {
    uint16_t proto = (uint16_t)(p[0] | (p[1]<<8));
    uint32_t flags = rd32(p + 2), baud = rd32(p + 6), cid = 0;
    status = j2534_connect(proto, flags, baud, &cid);
    memcpy(out, &cid, 4); out_n = 4; break; }
  case CMD_DISCONNECT: status = j2534_disconnect(rd32(p)); break;
  case CMD_SET_PROG_VOLT: status = j2534_set_prog_voltage(rd32(p), rd32(p + 4)); break;
  case CMD_READ_VBATT: { uint32_t mv = 0; status = j2534_read_vbatt(&mv); memcpy(out, &mv, 4); out_n = 4; break; }
  case CMD_IOCTL: {
    uint32_t cid = rd32(p), ioc = rd32(p + 4); out_n = sizeof(out);
    status = j2534_ioctl(cid, ioc, p + 8, (uint16_t)(f->len - 8), out, &out_n); break; }
	  case CMD_START_FILTER: {
	    status = do_start_filter(p, f->len, out, &out_n); break; }
  case CMD_STOP_FILTER: {

    if (f->len < 8) { status = J2534_ERR_NULL_PARAMETER; break; }
    status = j2534_stop_filter(rd32(p), rd32(p + 4)); break; }
  case CMD_WRITE_MSGS: {

    if (f->len < 12) { status = J2534_ERR_NULL_PARAMETER; break; }
    uint32_t written = 0;
    status = do_write_msgs(rd32(p), rd32(p + 4), rd32(p + 8), p + 12, f->len - 12, &written);
    memcpy(out, &written, 4); out_n = 4; break; }
  case CMD_READ_MSGS: {

    if (f->len < 12) { status = J2534_ERR_NULL_PARAMETER; break; }
    status = do_read_msgs(rd32(p), rd32(p + 4), rd32(p + 8), &rdata, &out_n); break; }
  case CMD_START_PERIODIC: {

    if (f->len < 12) { status = J2534_ERR_NULL_PARAMETER; break; }
    uint32_t mid = 0;
    status = j2534_start_periodic(rd32(p), p + 12, (uint16_t)(f->len - 12),
                   rd32(p + 8), rd32(p + 4), &mid);
    memcpy(out, &mid, 4); out_n = 4; break; }
  case CMD_STOP_PERIODIC: {

    if (f->len < 8) { status = J2534_ERR_NULL_PARAMETER; break; }
    status = j2534_stop_periodic(rd32(p), rd32(p + 4)); break; }
  case CMD_TRACE: {

    uint8_t sub = (f->len >= 1) ? p[0] : TRACE_CTL_READ;
    if (sub == TRACE_CTL_OFF)    trace_enable(0);
    else if (sub == TRACE_CTL_ON)  trace_enable(1);
    else if (sub == TRACE_CTL_CLEAR) trace_clear();
    else if (sub == TRACE_CTL_READ) status = do_read_trace(&rdata, &out_n);
    else status = J2534_ERR_NOT_SUPPORTED;
    break; }
  case CMD_TEST_RELAY: {

    if (f->len < 3) { status = J2534_ERR_NULL_PARAMETER; break; }
    status = (matrix_test_relay(p[0], p[1], p[2]) == 0)
         ? J2534_STATUS_NOERROR : J2534_ERR_FAILED;
    break; }
  case CMD_USB_MODE_GET: {

    out[0] = (uint8_t)usb_mode_get();
    out[1] = (uint8_t)USB_MODE_COUNT;
    out_n = 2; break; }
  case CMD_USB_MODE_SET: {

    if (f->len < 1) { status = J2534_ERR_NULL_PARAMETER; break; }
    if (p[0] >= USB_MODE_COUNT) { status = J2534_ERR_NOT_SUPPORTED; break; }
    if (usb_mode_store((usb_mode_t)p[0]) != 0) { status = J2534_ERR_FAILED; break; }
    out[0] = p[0]; out_n = 1;
    usb_mode_request_reboot();
    break; }
  default: status = J2534_ERR_NOT_SUPPORTED; break;
  }
  proto_reply(f->seq, f->cmd, status, rdata, out_n);
}
