#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
CC="${CC:-cc}"
FLAGS="-std=c11 -Wall -Wextra -O1 -g -I.."
FW=../../firmware/src

run() {
  local name="$1"; shift
  echo "=== $name ==="
  $CC $FLAGS -o "/tmp/$name" "$@"
  "/tmp/$name"
}

run test_frame test_frame.c ../shared/pt_frame.c ../shared/pt_wire.c

run test_isotp test_isotp.c $FW/j2534/isotp.c

run test_e2e test_e2e.c \
    ../ptcore/pt_core.c ../shared/pt_frame.c ../shared/pt_wire.c \
    ../vdevice/vdevice.c ../vdevice/mock_bus.c ../vdevice/usb_mode_stub.c \
    $FW/j2534/j2534_core.c $FW/j2534/isotp.c $FW/j2534/j2534_filter.c \
    $FW/j2534/j2534_wire.c $FW/j2534/kline_asm.c $FW/j2534/trace.c \
    $FW/transport/protocol.c

run test_elm327 test_elm327.c $FW/elm327/elm327.c

$CC $FLAGS -o /tmp/test_tcp test_tcp.c \
    ../ptcore/pt_core.c ../shared/pt_frame.c ../shared/pt_wire.c ../shared/transport_tcp.c \
    ../vdevice/vdevice.c ../vdevice/mock_bus.c ../vdevice/usb_mode_stub.c \
    $FW/j2534/j2534_core.c $FW/j2534/isotp.c $FW/j2534/j2534_filter.c \
    $FW/j2534/j2534_wire.c $FW/j2534/kline_asm.c $FW/j2534/trace.c \
    $FW/transport/protocol.c -lpthread
echo "=== test_tcp ==="
/tmp/test_tcp

echo "Host tests OK."
