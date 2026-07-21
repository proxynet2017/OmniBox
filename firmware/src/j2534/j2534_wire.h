
#ifndef J2534_WIRE_H
#define J2534_WIRE_H

#include <stdint.h>
#include "j2534_defs.h"

#define J2534_WIRE_MSG_HDR 24u


int j2534_wire_encode_msg(uint8_t *out, uint32_t cap, const j2534_msg_t *m);


int j2534_wire_decode_msg(const uint8_t *in, uint32_t n, j2534_msg_t *m);


uint32_t j2534_wire_rd32(const uint8_t *p);
void   j2534_wire_wr32(uint8_t *p, uint32_t v);

#endif 
