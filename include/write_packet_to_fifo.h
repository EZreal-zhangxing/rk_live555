#ifndef WRITE_FIFO
#define WRITE_FIFO
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include "safely_queue.h"
#ifndef FFMPEG_HEAD
extern "C"{
#include<libavcodec/avcodec.h>
#include<libavutil/time.h>
}

#endif

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

#define FRAME_BUFFER_SIZE 1920*1080

class FramePacket{
public:
    FramePacket() = default;
    FramePacket(uint8_t * addr,int dsize):size(dsize){
        dataPtr = (uint8_t *)malloc(dsize);
        memmove(dataPtr,addr,dsize);
    };
    long send_memory_time;
    long pop_out_time;
    uint8_t * dataPtr;
    int size;
    FramePacket * next; 
};

// static unsigned char * frameBuffer = (unsigned char *)malloc(FRAME_BUFFER_SIZE);

extern void fifo_write(AVPacket * &packet);

extern void buffer_write(AVPacket * &packet);

extern FramePacket * buffer_get();

extern void fifo_open();

extern void fifo_close();

extern void create_live555();

extern void create_multicast_live555();

#endif