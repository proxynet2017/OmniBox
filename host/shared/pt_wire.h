
#ifndef PT_WIRE_H
#define PT_WIRE_H

#include <stdint.h>
#include "j2534_abi.h"

#define PT_WIRE_HDR 24u

uint32_t pt_rd32(const uint8_t *p);
void   pt_wr32(uint8_t *p, uint32_t v);


int pt_wire_encode(uint8_t *out, uint32_t cap, const PASSTHRU_MSG *m);

int pt_wire_decode(const uint8_t *in, uint32_t n, PASSTHRU_MSG *m);

#endif 
