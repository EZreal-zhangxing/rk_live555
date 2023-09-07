#ifndef MPP_DECODER
#define MPP_DECODER

#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
// #include <rockchip/mpp_meta.h>

#include "rtsp_connect.h"

MPP_RET init_decoder();

int read_data_and_decode(RTSPReceiveData * &data);

void start_thread(DummySink *sink);
#endif