
#include <stdio.h>
#include <string.h>
#include "../ptcore/pt_core.h"
#include "../shared/pt_proto.h"
#include "../vdevice/vdevice.h"

static int g_fail = 0, g_tests = 0;
#define CHECK(cond, msg) do { g_tests++; if (!(cond)) { printf(" FAIL: %s\n", msg); g_fail++; } } while (0)

static void mk_idmsg(PASSTHRU_MSG *m, uint32_t can_id)
{
  memset(m, 0, sizeof(*m));
  m->ProtocolID = J2534_ISO15765;
  m->Data[0] = (uint8_t)(can_id >> 24); m->Data[1] = (uint8_t)(can_id >> 16);
  m->Data[2] = (uint8_t)(can_id >> 8); m->Data[3] = (uint8_t)can_id;
  m->DataSize = 4;
}
static void mk_req(PASSTHRU_MSG *m, uint32_t can_id, const uint8_t *uds, uint16_t n)
{
  mk_idmsg(m, can_id);
  for (uint16_t i = 0; i < n; i++) m->Data[4 + i] = uds[i];
  m->DataSize = (uint32_t)(4 + n);
}


static void open_iso15765(pt_handle_t *h, uint32_t *ch)
{
  pt_transport_t *t = vdevice_init();
  pt_init(h, t);
  uint32_t dev = 0;
  CHECK(pt_open(h, "vdev", &dev) == J2534_STATUS_NOERROR, "PassThruOpen");
  CHECK(pt_connect(h, dev, J2534_ISO15765, 0, 500000, ch) == J2534_STATUS_NOERROR, "PassThruConnect");
  PASSTHRU_MSG mask, pat, flow;
  mk_idmsg(&mask, 0x7FF); mk_idmsg(&pat, 0x7E8); mk_idmsg(&flow, 0x7E0);
  uint32_t fid = 0;
  CHECK(pt_start_filter(h, *ch, J2534_FLOW_CONTROL_FILTER, &mask, &pat, &flow, &fid) == J2534_STATUS_NOERROR,
     "PassThruStartMsgFilter (flow control)");
}


static void test_e2e_sf(void)
{
  printf("test_e2e_sf\n");
  pt_handle_t h; uint32_t ch = 0;
  open_iso15765(&h, &ch);

  uint8_t uds[2] = { 0x3E, 0x00 };
  PASSTHRU_MSG req; mk_req(&req, 0x7E0, uds, 2);
  uint32_t num = 1;
  int32_t st = pt_write_msgs(&h, ch, &req, &num, 100);
  CHECK(st == J2534_STATUS_NOERROR && num == 1, "WriteMsgs TesterPresent");

  PASSTHRU_MSG resp; num = 1;
  st = pt_read_msgs(&h, ch, &resp, &num, 100);
  CHECK(st == J2534_STATUS_NOERROR && num == 1, "ReadMsgs response");
  CHECK(resp.DataSize == 6, "response = 4 (id) + 2 (7E 00)");
  uint32_t rid = ((uint32_t)resp.Data[0] << 24) | (resp.Data[1] << 16) | (resp.Data[2] << 8) | resp.Data[3];
  CHECK(rid == 0x7E8, "response ID = 0x7E8 (ECU)");
  CHECK(resp.Data[4] == 0x7E && resp.Data[5] == 0x00, "response positive 0x7E 0x00");
}


static void test_e2e_multiframe(void)
{
  printf("test_e2e_multiframe\n");
  pt_handle_t h; uint32_t ch = 0;
  open_iso15765(&h, &ch);

  uint8_t uds[3] = { 0x22, 0xF1, 0x90 };     
  PASSTHRU_MSG req; mk_req(&req, 0x7E0, uds, 3);
  uint32_t num = 1;
  CHECK(pt_write_msgs(&h, ch, &req, &num, 100) == J2534_STATUS_NOERROR && num == 1, "WriteMsgs RDBI");

  PASSTHRU_MSG resp; num = 1;
  int32_t st = pt_read_msgs(&h, ch, &resp, &num, 200);
  CHECK(st == J2534_STATUS_NOERROR && num == 1, "ReadMsgs long response");
  
  CHECK(resp.DataSize == 4 + 103, "response multiframe reassembled (107 o)");
  CHECK(resp.Data[4] == 0x62 && resp.Data[5] == 0xF1 && resp.Data[6] == 0x90, "header 0x62 F1 90");
  CHECK(resp.Data[7] == 0xA0 && resp.Data[7 + 99] == (uint8_t)(0xA0 + 99), "100-byte payload intact");
}


static void test_e2e_negative(void)
{
  printf("test_e2e_negative\n");
  pt_handle_t h; uint32_t ch = 0;
  open_iso15765(&h, &ch);

  uint8_t uds[2] = { 0x7D, 0x00 };        
  PASSTHRU_MSG req; mk_req(&req, 0x7E0, uds, 2);
  uint32_t num = 1;
  pt_write_msgs(&h, ch, &req, &num, 100);

  PASSTHRU_MSG resp; num = 1;
  pt_read_msgs(&h, ch, &resp, &num, 100);
  CHECK(resp.DataSize == 4 + 3, "negative response = 3 bytes");
  CHECK(resp.Data[4] == 0x7F && resp.Data[5] == 0x7D && resp.Data[6] == 0x11, "7F 7D 11 (serviceNotSupported)");
}


static void test_e2e_vbatt(void)
{
  printf("test_e2e_vbatt\n");
  pt_handle_t h; uint32_t ch = 0;
  open_iso15765(&h, &ch);
  uint32_t mv = 0;
  CHECK(pt_read_vbatt(&h, &mv) == J2534_STATUS_NOERROR, "ReadVbatt status");
  CHECK(mv == 12500, "VBATT = 12500 mV (mock FEPS)");
}


static void test_e2e_config(void)
{
  printf("test_e2e_config\n");
  pt_handle_t h; uint32_t ch = 0;
  open_iso15765(&h, &ch);
  SCONFIG set[2] = { { J2534_CFG_ISO15765_BS, 4 }, { J2534_CFG_ISO15765_STMIN, 10 } };
  CHECK(pt_set_config(&h, ch, set, 2) == J2534_STATUS_NOERROR, "SET_CONFIG BS/STMIN");
  SCONFIG get[2] = { { J2534_CFG_ISO15765_BS, 0 }, { J2534_CFG_ISO15765_STMIN, 0 } };
  CHECK(pt_get_config(&h, ch, get, 2) == J2534_STATUS_NOERROR, "GET_CONFIG");
  CHECK(get[0].Value == 4 && get[1].Value == 10, "read-back values = BS 4, STmin 10");
}

static void test_e2e_caps_and_omni_config(void)
{
  printf("test_e2e_caps_and_omni_config\n");
  pt_transport_t *t = vdevice_init();
  pt_handle_t h; pt_init(&h, t);
  pt_caps_t caps;
  CHECK(pt_get_caps(&h, &caps) == J2534_STATUS_NOERROR, "GET_CAPS status");
  CHECK(caps.proto_version == OMNIBOX_PROTO_VERSION, "protocol version");
  CHECK((caps.caps & OMNI_CAP_J2534_ISO15765) != 0, "ISO15765 capability");
  CHECK(caps.can_channels >= 5, "multi-CAN capability count");

  uint32_t dev = 0, ch = 0;
  CHECK(pt_open(&h, "vdev", &dev) == J2534_STATUS_NOERROR, "Open for Omni config");
  CHECK(pt_connect(&h, dev, J2534_CAN, 0, 500000, &ch) == J2534_STATUS_NOERROR, "Connect CAN for Omni config");
  SCONFIG set[4] = {
    { OMNI_CFG_PHYSICAL_BUS, 2 },
    { OMNI_CFG_TERMINATION, 1 },
    { OMNI_CFG_CAN_SWAP, 1 },
    { OMNI_CFG_CAN_DATA_RATE, 2000000 },
  };
  CHECK(pt_set_config(&h, ch, set, 4) == J2534_STATUS_NOERROR, "SET_CONFIG Omni params");
  SCONFIG get[4] = {
    { OMNI_CFG_PHYSICAL_BUS, 0 },
    { OMNI_CFG_TERMINATION, 0 },
    { OMNI_CFG_CAN_SWAP, 0 },
    { OMNI_CFG_CAN_DATA_RATE, 0 },
  };
  CHECK(pt_get_config(&h, ch, get, 4) == J2534_STATUS_NOERROR, "GET_CONFIG Omni params");
  CHECK(get[0].Value == 2 && get[1].Value == 1 && get[2].Value == 1 &&
        get[3].Value == 2000000, "Omni params persisted");
}

static void test_e2e_payload_filter(void)
{
  printf("test_e2e_payload_filter\n");
  pt_transport_t *t = vdevice_init();
  pt_handle_t h; pt_init(&h, t);
  uint32_t dev = 0, ch = 0, fid = 0;
  CHECK(pt_open(&h, "vdev", &dev) == J2534_STATUS_NOERROR, "Open payload filter");
  CHECK(pt_connect(&h, dev, J2534_CAN, 0, 500000, &ch) == J2534_STATUS_NOERROR, "Connect CAN payload filter");

  PASSTHRU_MSG mask, pat, flow;
  memset(&mask, 0, sizeof mask); memset(&pat, 0, sizeof pat); memset(&flow, 0, sizeof flow);
  mask.ProtocolID = pat.ProtocolID = flow.ProtocolID = J2534_CAN;
  mask.DataSize = pat.DataSize = flow.DataSize = 5;
  mask.Data[2] = 0x07; mask.Data[3] = 0xFF; mask.Data[4] = 0xFF;
  pat.Data[2] = 0x01; pat.Data[3] = 0x23; pat.Data[4] = 0xAA;
  CHECK(pt_start_filter(&h, ch, J2534_PASS_FILTER, &mask, &pat, &flow, &fid) == J2534_STATUS_NOERROR,
        "Start payload PASS filter");

  uint8_t reject[2] = { 0x55, 0x01 };
  mock_bus_queue_can(0x123, reject, 2);
  pt_caps_t caps;
  pt_get_caps(&h, &caps);
  PASSTHRU_MSG out; uint32_t num = 1;
  CHECK(pt_read_msgs(&h, ch, &out, &num, 20) == J2534_ERR_TIMEOUT, "payload mismatch rejected");

  uint8_t accept[2] = { 0xAA, 0x02 };
  mock_bus_queue_can(0x123, accept, 2);
  pt_get_caps(&h, &caps);
  num = 1;
  CHECK(pt_read_msgs(&h, ch, &out, &num, 20) == J2534_STATUS_NOERROR && num == 1,
        "payload match accepted");
  CHECK(out.DataSize == 6 && out.Data[4] == 0xAA && out.Data[5] == 0x02, "payload preserved");
}

static void test_e2e_kline_init_ioctl_wire(void)
{
  printf("test_e2e_kline_init_ioctl_wire\n");
  pt_transport_t *t = vdevice_init();
  pt_handle_t h; pt_init(&h, t);
  uint32_t dev = 0, ch = 0;
  CHECK(pt_open(&h, "vdev", &dev) == J2534_STATUS_NOERROR, "Open K-line");
  CHECK(pt_connect(&h, dev, J2534_ISO9141, 0, 10400, &ch) == J2534_STATUS_NOERROR, "Connect ISO9141");
  uint8_t addr = 0x33;
  uint8_t kb[2] = {0};
  uint32_t out_len = sizeof(kb);
  CHECK(pt_ioctl_bytes(&h, ch, J2534_FIVE_BAUD_INIT, &addr, 1, kb, &out_len) == J2534_STATUS_NOERROR,
        "FIVE_BAUD_INIT transported");
  CHECK(out_len == 2, "FIVE_BAUD_INIT keybyte count");
  uint8_t wake[5] = { 0xC1, 0x33, 0xF1, 0x81, 0x66 };
  uint8_t rsp[8] = {0};
  out_len = sizeof(rsp);
  CHECK(pt_ioctl_bytes(&h, ch, J2534_FAST_INIT, wake, sizeof(wake), rsp, &out_len) == J2534_STATUS_NOERROR,
        "FAST_INIT transported");
}


static void test_e2e_clear_filters(void)
{
  printf("test_e2e_clear_filters\n");
  pt_handle_t h; uint32_t ch = 0;
  open_iso15765(&h, &ch);
  CHECK(pt_ioctl_clear(&h, ch, J2534_CLEAR_MSG_FILTERS) == J2534_STATUS_NOERROR, "CLEAR_MSG_FILTERS");
  
  uint8_t sf[2] = { 0x3E, 0x00 };
  PASSTHRU_MSG req; mk_req(&req, 0x7E0, sf, 2);
  uint32_t num = 1;
  CHECK(pt_write_msgs(&h, ch, &req, &num, 100) == J2534_STATUS_NOERROR, "SF accepted without flow filter");
  
  uint8_t big[8] = { 0x2E, 0xF1, 0x90, 1, 2, 3, 4, 5 };
  PASSTHRU_MSG bigm; mk_req(&bigm, 0x7E0, big, 8); num = 1;
  CHECK(pt_write_msgs(&h, ch, &bigm, &num, 100) == J2534_ERR_INVALID_MSG, "multiframe refused without flow filter");
}


static void test_e2e_periodic(void)
{
  printf("test_e2e_periodic\n");
  pt_handle_t h; uint32_t ch = 0;
  open_iso15765(&h, &ch);
  uint8_t uds[2] = { 0x3E, 0x00 };
  PASSTHRU_MSG tp; mk_req(&tp, 0x7E0, uds, 2);
  uint32_t mid = 0;
  CHECK(pt_start_periodic(&h, ch, &tp, 25, &mid) == J2534_STATUS_NOERROR, "StartPeriodicMsg");
  CHECK(pt_stop_periodic(&h, ch, mid) == J2534_STATUS_NOERROR, "StopPeriodicMsg");
}


static void test_e2e_version(void)
{
  printf("test_e2e_version\n");
  pt_handle_t h; uint32_t ch = 0;
  open_iso15765(&h, &ch);
  char fw[80] = {0}, dll[80] = {0}, api[80] = {0};
  CHECK(pt_read_version(&h, fw, dll, api) == J2534_STATUS_NOERROR, "ReadVersion status");
  CHECK(strcmp(fw, "OmniBox FW 0.1 dev HW:1.0 beta") == 0, "version firmware OmniBox");
  CHECK(strcmp(api, "04.04") == 0, "version API J2534");
  CHECK(strcmp(dll, "OmniBox DLL 0.1 dev") == 0, "version DLL OmniBox");
}


static void test_e2e_29bit_scan(void)
{
  printf("test_e2e_29bit_scan\n");
  pt_transport_t *t = vdevice_init();
  pt_handle_t h; pt_init(&h, t);
  uint32_t dev = 0, ch = 0, fid = 0;
  CHECK(pt_open(&h, "vdev", &dev) == J2534_STATUS_NOERROR, "Open");
  CHECK(pt_connect(&h, dev, J2534_ISO15765, J2534_CAN_29BIT_ID, 500000, &ch) == J2534_STATUS_NOERROR, "Connect 29bit");

  
  PASSTHRU_MSG mask, pat, flow;
  mk_idmsg(&mask, 0xFFFFFFFF); mk_idmsg(&pat, 0x18DAF10E); mk_idmsg(&flow, 0x18DA0EF1);
  CHECK(pt_start_filter(&h, ch, J2534_FLOW_CONTROL_FILTER, &mask, &pat, &flow, &fid) == J2534_STATUS_NOERROR,
     "StartMsgFilter 29bit");

  
  uint8_t fonc[2] = { 0x3E, 0x80 };
  PASSTHRU_MSG fmsg; mk_req(&fmsg, 0x18DBEFF1, fonc, 2);
  uint32_t num = 1;
  CHECK(pt_write_msgs(&h, ch, &fmsg, &num, 100) == J2534_STATUS_NOERROR, "WriteMsgs functional SF without flow filter");

  
  uint8_t uds[2] = { 0x3E, 0x00 };
  PASSTHRU_MSG req; mk_req(&req, 0x18DA0EF1, uds, 2); num = 1;
  CHECK(pt_write_msgs(&h, ch, &req, &num, 100) == J2534_STATUS_NOERROR && num == 1, "WriteMsgs physical 29bit");

  PASSTHRU_MSG resp; num = 1;
  int32_t st = pt_read_msgs(&h, ch, &resp, &num, 100);
  CHECK(st == J2534_STATUS_NOERROR && num == 1, "ReadMsgs response 29bit");
  uint32_t rid = ((uint32_t)resp.Data[0] << 24) | (resp.Data[1] << 16) | (resp.Data[2] << 8) | resp.Data[3];
  CHECK(rid == 0x18DAF10E, "response ID = 0x18DAF10E (TA/SA swapped)");
  CHECK(resp.DataSize == 6 && resp.Data[4] == 0x7E && resp.Data[5] == 0x00, "response 7E 00");
  CHECK((resp.RxStatus & J2534_CAN_29BIT_ID) != 0, "RxStatus marks 29bit");

  
  PASSTHRU_MSG req2; mk_req(&req2, 0x18DA10F1, uds, 2); num = 1;
  pt_write_msgs(&h, ch, &req2, &num, 100);
  PASSTHRU_MSG r2; num = 1;
  CHECK(pt_read_msgs(&h, ch, &r2, &num, 50) == J2534_ERR_TIMEOUT, "ECU 0x10 silent (not emulated)");
}


static void uds_xchg(pt_handle_t *h, uint32_t ch, uint32_t txid, const uint8_t *req, uint16_t rn,
           PASSTHRU_MSG *resp, const char *label)
{
  PASSTHRU_MSG m; mk_req(&m, txid, req, rn);
  uint32_t num = 1;
  CHECK(pt_write_msgs(h, ch, &m, &num, 200) == J2534_STATUS_NOERROR && num == 1, label);
  num = 1;
  CHECK(pt_read_msgs(h, ch, resp, &num, 200) == J2534_STATUS_NOERROR && num == 1, label);
}

static void test_e2e_uds_services(void)
{
  printf("test_e2e_uds_services\n");
  pt_transport_t *t = vdevice_init();
  pt_handle_t h; pt_init(&h, t);
  uint32_t dev = 0, ch = 0, fid = 0;
  pt_open(&h, "vdev", &dev);
  pt_connect(&h, dev, J2534_ISO15765, J2534_CAN_29BIT_ID, 500000, &ch);
  PASSTHRU_MSG mask, pat, flow;
  mk_idmsg(&mask, 0xFFFFFFFF); mk_idmsg(&pat, 0x18DAF10E); mk_idmsg(&flow, 0x18DA0EF1);
  pt_start_filter(&h, ch, J2534_FLOW_CONTROL_FILTER, &mask, &pat, &flow, &fid);
  const uint32_t TX = 0x18DA0EF1;
  PASSTHRU_MSG r;

  uint8_t sess[2] = { 0x10, 0x03 };           
  uds_xchg(&h, ch, TX, sess, 2, &r, "DiagnosticSessionControl");
  CHECK(r.Data[4] == 0x50 && r.Data[5] == 0x03, "response 50 03");

  uint8_t seed[2] = { 0x27, 0x01 };           
  uds_xchg(&h, ch, TX, seed, 2, &r, "SecurityAccess requestSeed");
  CHECK(r.Data[4] == 0x67 && r.Data[5] == 0x01 && r.DataSize == 10, "response 67 01 + seed");

  uint8_t key[6] = { 0x27, 0x02, 0x11, 0x22, 0x33, 0x44 };
  uds_xchg(&h, ch, TX, key, 6, &r, "SecurityAccess sendKey");
  CHECK(r.Data[4] == 0x67 && r.Data[5] == 0x02, "response 67 02 (unlocked)");

  
  uint8_t up[11] = { 0x35, 0x00, 0x44, 0, 0, 0, 0, 0, 0, 0, 0x40 };
  uds_xchg(&h, ch, TX, up, 11, &r, "RequestUpload");
  CHECK(r.Data[4] == 0x75, "response 75 (maxBlock)");

  uint8_t td[2] = { 0x36, 0x01 };            
  uds_xchg(&h, ch, TX, td, 2, &r, "TransferData");
  CHECK(r.Data[4] == 0x76 && r.Data[5] == 0x01 && r.DataSize == (uint32_t)(4 + 2 + 0x40), "response 76 + 64 o upload");

  uint8_t te[1] = { 0x37 };               
  uds_xchg(&h, ch, TX, te, 1, &r, "RequestTransferExit");
  CHECK(r.Data[4] == 0x77, "response 77");

  uint8_t rmba[4] = { 0x23, 0x11, 0x00, 0x20 };     
  uds_xchg(&h, ch, TX, rmba, 4, &r, "ReadMemoryByAddress");
  CHECK(r.Data[4] == 0x63 && r.DataSize == (uint32_t)(4 + 1 + 0x20), "response 63 + 32 bytes");
}


static void test_e2e_dump_read(void)
{
  printf("test_e2e_dump_read\n");
  
  const char *path = "/tmp/vdev_test_dump.bin";
  FILE *f = fopen(path, "wb");
  CHECK(f != NULL, "test dump creation");
  if (!f) return;
  uint8_t buf[512];
  for (int i = 0; i < 512; i++) buf[i] = (uint8_t)(i & 0xFF);
  fwrite(buf, 1, sizeof buf, f); fclose(f);

  pt_transport_t *t = vdevice_init();      
  CHECK(mock_bus_load_dump(path, 0x80000000u) == 0, "dump loaded");
  pt_handle_t h; pt_init(&h, t);
  uint32_t dev = 0, ch = 0, fid = 0;
  pt_open(&h, "vdev", &dev);
  pt_connect(&h, dev, J2534_ISO15765, J2534_CAN_29BIT_ID, 500000, &ch);
  PASSTHRU_MSG mask, pat, flow;
  mk_idmsg(&mask, 0xFFFFFFFF); mk_idmsg(&pat, 0x18DAF10E); mk_idmsg(&flow, 0x18DA0EF1);
  pt_start_filter(&h, ch, J2534_FLOW_CONTROL_FILTER, &mask, &pat, &flow, &fid);

  
  uint8_t rdfull[10] = { 0x23, 0x44, 0x80,0x00,0x00,0x10, 0x00,0x00,0x00,0x10 };
  PASSTHRU_MSG req; mk_req(&req, 0x18DA0EF1, rdfull, 10);
  uint32_t num = 1;
  pt_write_msgs(&h, ch, &req, &num, 200);
  PASSTHRU_MSG resp; num = 1;
  int32_t st = pt_read_msgs(&h, ch, &resp, &num, 200);
  CHECK(st == J2534_STATUS_NOERROR && num == 1, "ReadMemoryByAddress dump");
  CHECK(resp.Data[4] == 0x63, "response positive 0x63");
  CHECK(resp.DataSize == (uint32_t)(4 + 1 + 16), "63 + 16 bytes");
  CHECK(resp.Data[5] == 0x10 && resp.Data[5 + 15] == 0x1F, "real dump bytes (0x10..0x1F)");

  
  uint8_t oor[10] = { 0x23, 0x44, 0x90,0x00,0x00,0x00, 0x00,0x00,0x00,0x10 };
  PASSTHRU_MSG req2; mk_req(&req2, 0x18DA0EF1, oor, 10); num = 1;
  pt_write_msgs(&h, ch, &req2, &num, 200);
  PASSTHRU_MSG r2; num = 1;
  pt_read_msgs(&h, ch, &r2, &num, 200);
  CHECK(r2.Data[4] == 0x7F && r2.Data[6] == 0x31, "out of range -> NRC 0x31");

  
}

int main(void)
{
  test_e2e_sf();
  test_e2e_multiframe();
  test_e2e_negative();
  test_e2e_vbatt();
  test_e2e_config();
  test_e2e_caps_and_omni_config();
  test_e2e_payload_filter();
  test_e2e_kline_init_ioctl_wire();
  test_e2e_clear_filters();
  test_e2e_periodic();
  test_e2e_version();
  test_e2e_29bit_scan();
  test_e2e_uds_services();
  test_e2e_dump_read();    
  printf("\n%d/%d checks passed\n", g_tests - g_fail, g_tests);
  return g_fail ? 1 : 0;
}
