
#include <string.h>
#include "pt_wire.h"

uint32_t pt_rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void pt_wr32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int pt_wire_encode(uint8_t *out, uint32_t cap, const PASSTHRU_MSG *m)
{
  uint32_t ds = m->DataSize;
  if (ds > J2534_DATA_MAX) return -1;
  if (cap < PT_WIRE_HDR + ds) return -1;
  pt_wr32(out + 0, m->ProtocolID);
  pt_wr32(out + 4, m->RxStatus);
  pt_wr32(out + 8, m->TxFlags);
  pt_wr32(out + 12, m->Timestamp);
  pt_wr32(out + 16, m->ExtraDataIndex);
  pt_wr32(out + 20, ds);
  memcpy(out + PT_WIRE_HDR, m->Data, ds);
  return (int)(PT_WIRE_HDR + ds);
}

int pt_wire_decode(const uint8_t *in, uint32_t n, PASSTHRU_MSG *m)
{
  if (n < PT_WIRE_HDR) return -1;
  uint32_t ds = pt_rd32(in + 20);
  if (ds > J2534_DATA_MAX || n < PT_WIRE_HDR + ds) return -1;
  memset(m, 0, sizeof(*m));
  m->ProtocolID   = pt_rd32(in + 0);
  m->RxStatus    = pt_rd32(in + 4);
  m->TxFlags     = pt_rd32(in + 8);
  m->Timestamp   = pt_rd32(in + 12);
  m->ExtraDataIndex = pt_rd32(in + 16);
  m->DataSize    = ds;
  memcpy(m->Data, in + PT_WIRE_HDR, ds);
  return (int)(PT_WIRE_HDR + ds);
}
