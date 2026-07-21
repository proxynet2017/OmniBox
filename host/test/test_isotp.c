#include <stdio.h>
#include <string.h>
#include "../../firmware/src/j2534/isotp.h"

static int g_fail = 0, g_tests = 0;
#define CHECK(cond, msg) do { g_tests++; if (!(cond)) { printf(" FAIL: %s\n", msg); g_fail++; } } while (0)

static void test_reject_malformed_single_frame(void)
{
  printf("test_reject_malformed_single_frame\n");
  uint8_t rx[32];
  isotp_t tp;
  isotp_frame_t out;
  int send = 0;
  isotp_init(&tp, rx, sizeof(rx), 0, 0);
  uint8_t bad[2] = { 0x03, 0x22 };
  CHECK(isotp_rx(&tp, bad, sizeof(bad), &out, &send) == 0, "short SF rejected");
  CHECK(tp.last_error == ISOTP_ERR_LENGTH, "short SF length error");
}

static void test_flow_control_wait_and_overflow(void)
{
  printf("test_flow_control_wait_and_overflow\n");
  uint8_t rx[32], payload[12];
  for (uint8_t i = 0; i < sizeof(payload); i++) payload[i] = i;
  isotp_t tp;
  isotp_frame_t out;
  int send = 0;
  isotp_init(&tp, rx, sizeof(rx), 0, 0);
  CHECK(isotp_send_start(&tp, payload, sizeof(payload), &out) == 1, "FF started");
  CHECK(tp.tx_state == ISOTP_TX_WAIT_FC, "waiting for FC");
  uint8_t wait_fc[3] = { 0x31, 0x00, 0x00 };
  for (int i = 0; i < 3; i++) isotp_rx(&tp, wait_fc, sizeof(wait_fc), &out, &send);
  CHECK(tp.tx_state == ISOTP_TX_WAIT_FC, "three WAIT frames tolerated");
  isotp_rx(&tp, wait_fc, sizeof(wait_fc), &out, &send);
  CHECK(tp.tx_state == ISOTP_TX_IDLE && tp.last_error == ISOTP_ERR_WAIT_LIMIT, "fourth WAIT aborts");

  isotp_init(&tp, rx, sizeof(rx), 0, 0);
  CHECK(isotp_send_start(&tp, payload, sizeof(payload), &out) == 1, "FF restarted");
  uint8_t ovflw_fc[3] = { 0x32, 0x00, 0x00 };
  isotp_rx(&tp, ovflw_fc, sizeof(ovflw_fc), &out, &send);
  CHECK(tp.tx_state == ISOTP_TX_IDLE && tp.last_error == ISOTP_ERR_OVERFLOW, "OVFLW aborts sender");
}

static void test_block_size_and_sequence(void)
{
  printf("test_block_size_and_sequence\n");
  uint8_t rx[64], payload[20];
  for (uint8_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(0xA0 + i);
  isotp_t tp;
  isotp_frame_t out;
  isotp_init(&tp, rx, sizeof(rx), 0, 0);
  CHECK(isotp_send_start(&tp, payload, sizeof(payload), &out) == 1, "FF sent");
  uint8_t fc[3] = { 0x30, 0x01, 0x00 };
  int send = 0;
  isotp_rx(&tp, fc, sizeof(fc), &out, &send);
  CHECK(tp.tx_state == ISOTP_TX_SEND_CF, "CTS starts CF");
  CHECK(isotp_send_next(&tp, &out) == 1 && tp.tx_state == ISOTP_TX_WAIT_FC, "BS=1 waits after one CF");

  isotp_init(&tp, rx, sizeof(rx), 0, 0);
  uint8_t ff[8] = { 0x10, 0x09, 1, 2, 3, 4, 5, 6 };
  CHECK(isotp_rx(&tp, ff, sizeof(ff), &out, &send) == 0 && send == 1, "FF accepted and FC generated");
  uint8_t bad_cf[3] = { 0x22, 7, 8 };
  CHECK(isotp_rx(&tp, bad_cf, sizeof(bad_cf), &out, &send) == 0, "bad SN rejected");
  CHECK(tp.last_error == ISOTP_ERR_SEQUENCE, "sequence error reported");
}

static void test_receiver_overflow_fc(void)
{
  printf("test_receiver_overflow_fc\n");
  uint8_t rx[8];
  isotp_t tp;
  isotp_frame_t out;
  int send = 0;
  isotp_init(&tp, rx, sizeof(rx), 0, 0);
  uint8_t ff[8] = { 0x10, 0x20, 1, 2, 3, 4, 5, 6 };
  CHECK(isotp_rx(&tp, ff, sizeof(ff), &out, &send) == 0 && send == 1, "oversize FF triggers FC");
  CHECK(out.data[0] == 0x32 && tp.last_error == ISOTP_ERR_OVERFLOW, "overflow FC emitted");
}

int main(void)
{
  test_reject_malformed_single_frame();
  test_flow_control_wait_and_overflow();
  test_block_size_and_sequence();
  test_receiver_overflow_fc();
  printf("\n%d/%d checks passed\n", g_tests - g_fail, g_tests);
  return g_fail ? 1 : 0;
}
