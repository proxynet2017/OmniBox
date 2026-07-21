
#include <string.h>
#include "j2534_wire.h"

uint32_t j2534_wire_rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void j2534_wire_wr32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int j2534_wire_encode_msg(uint8_t *out, uint32_t cap, const j2534_msg_t *m)
{
  uint32_t ds = m->data_size;
  if (ds > J2534_MSG_DATA_MAX) return -1;
  if (cap < J2534_WIRE_MSG_HDR + ds) return -1;
  j2534_wire_wr32(out + 0, m->protocol_id);
  j2534_wire_wr32(out + 4, m->rx_status);
  j2534_wire_wr32(out + 8, m->tx_flags);
  j2534_wire_wr32(out + 12, m->timestamp);
  j2534_wire_wr32(out + 16, m->extra_data_index);
  j2534_wire_wr32(out + 20, ds);
  memcpy(out + J2534_WIRE_MSG_HDR, m->data, ds);
  return (int)(J2534_WIRE_MSG_HDR + ds);
}

int j2534_wire_decode_msg(const uint8_t *in, uint32_t n, j2534_msg_t *m)
{
  if (n < J2534_WIRE_MSG_HDR) return -1;
  uint32_t ds = j2534_wire_rd32(in + 20);
  if (ds > J2534_MSG_DATA_MAX) return -1;
  if (n < J2534_WIRE_MSG_HDR + ds) return -1;    
  memset(m, 0, sizeof(*m));
  m->protocol_id   = j2534_wire_rd32(in + 0);
  m->rx_status    = j2534_wire_rd32(in + 4);
  m->tx_flags     = j2534_wire_rd32(in + 8);
  m->timestamp    = j2534_wire_rd32(in + 12);
  m->extra_data_index = j2534_wire_rd32(in + 16);
  m->data_size    = ds;
  memcpy(m->data, in + J2534_WIRE_MSG_HDR, ds);
  return (int)(J2534_WIRE_MSG_HDR + ds);
}
