#ifndef MPP_DECODER
#define MPP_DECODER

#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_meta.h>

#include "rtsp_connect.h"

MPP_RET init_decoder();

MPP_RET send_data_to_decoder(RTSPReceiveData &data);

MPP_RET decode_process();

void start_thread(DummySink *sink);


#endif