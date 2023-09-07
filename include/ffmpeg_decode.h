#ifndef FFMPEG_DECODE
#define FFMPEG_DECODE

extern "C"{
#include<libavcodec/avcodec.h>
#include<libavdevice/avdevice.h>
#include<libavformat/avformat.h>
#include<libavutil/imgutils.h>
#include<libswscale/swscale.h>
#include<libavutil/pixdesc.h>
#include<libavutil/hwcontext.h>
#include<libavutil/time.h>
#include<libavutil/error.h>
#include<libavutil/pixfmt.h>
#include<libavutil/hwcontext_drm.h>
#include<drm_fourcc.h>
}

#include "rtsp_connect.h"
void init_decoder();

void send_data(RTSPReceiveData &data);

void send_data_concat(RTSPReceiveData &data);

int read_data_and_decode(DummySink *sink);

int directly_read_data_and_decode(RTSPReceiveData * data);

void start_thread(DummySink *sink);
#endif