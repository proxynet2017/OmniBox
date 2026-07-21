
#include <stdio.h>
#include <string.h>
#include "../shared/pt_frame.h"
#include "../shared/pt_proto.h"
#include "../shared/pt_wire.h"

static int g_fail = 0, g_tests = 0;
#define CHECK(cond, msg) do { g_tests++; if (!(cond)) { printf(" FAIL: %s\n", msg); g_fail++; } } while (0)

static void test_crc_vector(void)
{
  printf("test_crc_vector\n");
  
  CHECK(pt_crc16((const uint8_t *)"123456789", 9) == 0x29B1, "known CRC vector");
}

static void test_build_parse(void)
{
  printf("test_build_parse\n");
  uint8_t payload[4] = { 0x00, 0xDE, 0xAD, 0xBE };  
  uint8_t frame[64];
  int len = pt_frame_build(frame, sizeof frame, 7, CMD_OPEN | PT_RESP_FLAG, payload, 4);
  CHECK(len == 5 + 4 + 2, "frame length");
  CHECK(frame[0] == PT_SOF, "SOF");

  uint8_t seq, cmd, status; const uint8_t *data; uint16_t dlen;
  int rc = pt_frame_parse(frame, (size_t)len, &seq, &cmd, &status, &data, &dlen);
  CHECK(rc == 0, "parse OK");
  CHECK(seq == 7, "seq");
  CHECK(cmd == CMD_OPEN, "cmd (response flag removed)");
  CHECK(status == 0x00, "status");
  CHECK(dlen == 3 && data[0] == 0xDE && data[2] == 0xBE, "data after status");
}

static void test_corruption(void)
{
  printf("test_corruption\n");
  uint8_t payload[2] = { 0, 0x42 };
  uint8_t frame[32];
  int len = pt_frame_build(frame, sizeof frame, 1, CMD_PING | PT_RESP_FLAG, payload, 2);
  frame[6] ^= 0xFF;                  
  uint8_t seq, cmd, status; const uint8_t *data; uint16_t dlen;
  CHECK(pt_frame_parse(frame, (size_t)len, &seq, &cmd, &status, &data, &dlen) == -1, "corrupted CRC -> rejected");
}

static void test_cap_guard(void)
{
  printf("test_cap_guard\n");
  uint8_t small[6];
  CHECK(pt_frame_build(small, sizeof small, 0, CMD_OPEN, (const uint8_t *)"xxxx", 4) == -1, "insufficient capacity -> -1");
}

static void test_msg_wire_preserves_timestamp(void)
{
  printf("test_msg_wire_preserves_timestamp\n");
  PASSTHRU_MSG m, out;
  uint8_t buf[128];
  memset(&m, 0, sizeof(m));
  m.ProtocolID = J2534_CAN;
  m.RxStatus = J2534_CAN_29BIT_ID;
  m.TxFlags = OMNI_CAN_FD_FRAME;
  m.Timestamp = 0x12345678;
  m.ExtraDataIndex = 6;
  m.DataSize = 6;
  for (uint32_t i = 0; i < m.DataSize; i++) m.Data[i] = (uint8_t)(0xA0 + i);
  int n = pt_wire_encode(buf, sizeof(buf), &m);
  CHECK(n == (int)(PT_WIRE_HDR + m.DataSize), "wire encoded length");
  CHECK(pt_wire_decode(buf, (uint32_t)n, &out) == n, "wire decode status");
  CHECK(out.Timestamp == 0x12345678, "timestamp preserved");
  CHECK(out.ExtraDataIndex == 6 && out.Data[5] == 0xA5, "metadata and data preserved");
}

int main(void)
{
  test_crc_vector();
  test_build_parse();
  test_corruption();
  test_cap_guard();
  test_msg_wire_preserves_timestamp();
  printf("\n%d/%d checks passed\n", g_tests - g_fail, g_tests);
  return g_fail ? 1 : 0;
}
