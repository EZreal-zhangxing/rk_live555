#ifndef FFMPEG_HEAD
#define FFMPEG_HEAD


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

#include<iostream>

void print_error(int line,int res = 0,std::string selfInfo = ""){
    char errInfo[200];
    av_strerror(res,errInfo,200);
    std::cout << "[ " << line << " ] code:[" << res << "] " << errInfo << ". " << selfInfo << std::endl;
}
#endif