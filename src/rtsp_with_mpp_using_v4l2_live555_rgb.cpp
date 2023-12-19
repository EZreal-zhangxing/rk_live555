/**
 * OpenCv捕获数据，通过FFmpeg的编码器将数据发出
*/
#ifndef COMMAND
#include "command.h"
#endif

#ifndef FFMPEG_HEAD
#include "ffmpeg_head.h"
#endif
#include<opencv2/opencv.hpp>
#include<opencv2/imgproc/imgproc.hpp>
#include<opencv2/highgui/highgui.hpp>

#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_meta.h>

#include <sys/time.h>
#include<thread>

/**
 * V4l2
*/
#include <linux/videodev2.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>

#ifndef WRITE_FIFO
#include "write_packet_to_fifo.h"
#endif

#define MPP_ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))
using namespace std;
using namespace cv;

unsigned int framecount = 0;
unsigned int width = 0,height = 0;
/**
 * 分别对应水平补齐后的步长和垂直补齐后的步长
 * RK3588 读取图片时是16字节对齐，因此需要对图片补齐方能处理
*/
unsigned int hor_stride = 0,ver_stride = 0; 

unsigned int yuv_width = 0,yuv_height = 0;
unsigned int yuv_hor_stride = 0,yuv_ver_stride = 0; 

/**
 * 1920x1080x3 的RGB图片对于16位补齐后
 * hor_stride 1920,ver_stride = 1088
 * 对于YUV图像，图像会下采样成1920x1080x3/2x1(Channel) 
 * yuv_hor_stride 1920,yuv_ver_stride = 1632
 * 这两个图片的大小从
 * 1920x1080x3(rgb) --> 1920x1080x3/2(yuv)
 * wxhxc:
 * 1920x1080x3(rgb) --> 1920x1620x1(yuv)
 * 同时由于补齐作用 YUV原始图像需要变成:
 * 1920x1620 --> 1920x1632
 * YUV图像分量的分布为：wxh的亮度Y，w/2 x h/2的U，w/2 x h/2的V
 * -------------w(1920)---------- 
 * |                            |
 * |                            |
 * |                            |
 * |             Y              h(1080)
 * |                            |
 * |                            |
 * |                            |
 * ------------------------------
 * |                            |
 * |             gap            | 8
 * ------------------------------ 
 * |                            |
 * |              U             h/4(270)
 * |                            |
 * ------------------------------
 * |             gap            | 2
 * ------------------------------ 
 * |                            |
 * |              V             h/4(270)
 * |                            |
 * ------------------------------
 * |             gap            | 2
 * ------------------------------
 * 
*/

unsigned int image_size = 0;

/*********************FFMPEG_START*/
const AVCodec * codec;
AVCodecContext *codecCtx;
AVFormatContext * formatCtx;
AVStream * stream;
AVHWDeviceType type = AV_HWDEVICE_TYPE_DRM;
AVBufferRef *hwdevice;
AVBufferRef *hwframe;
AVHWFramesContext * hwframeCtx;

AVFrame *frame; // 封装DRM的帧
AVPacket * packet; // 发送的包

long extra_data_size = 10000000;
uint8_t* cExtradata = NULL; // 数据头

AVPixelFormat hd_pix = AV_PIX_FMT_DRM_PRIME;
AVPixelFormat sw_pix = AV_PIX_FMT_BGR24;
/*********************FFMPEG_END*/
/**********************MPP_START*/
MppBufferGroup group;
MppBufferInfo info;
MppBuffer buffer;
MppBuffer commitBuffer;
MppFrame mppframe;
MppPacket mppPacket;

MppApi *mppApi;
MppCtx mppCtx;
MppEncCfg cfg;
MppTask task;
MppMeta meta;

typedef struct {
    MppPacket packet;
    AVBufferRef *encoder_ref;
} RKMPPPacketContext;
/************************MPP_END*/

void rkmpp_release_packet(void *opaque, uint8_t *data){
    RKMPPPacketContext *pkt_ctx = (RKMPPPacketContext *)opaque;
    mpp_packet_deinit(&pkt_ctx->packet);
    av_buffer_unref(&pkt_ctx->encoder_ref);
    av_free(pkt_ctx);
}

int init_encoder(Command & obj){
    int res = 0;
    avformat_network_init();

    codec = avcodec_find_encoder_by_name("h264_rkmpp");
    if(!codec){
        print_error(__LINE__,-1,"can not find h264_rkmpp encoder!");
        return -1;
    }
    // 创建编码器上下文
    codecCtx = avcodec_alloc_context3(codec);
    if(!codecCtx){
        print_error(__LINE__,-1,"can not create codec Context of h264_rkmpp!");
        return -1;
    }
    
    res = av_hwdevice_ctx_create(&hwdevice,type,"/dev/dri/card0",0,0);
    if(res < 0){
        print_error(__LINE__,res,"create hdwave device context failed!");
        return res;
    }

    codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    codecCtx->codec_id = codec->id;
    codecCtx->codec = codec;
    codecCtx->bit_rate = 1024*1024*8;
    codecCtx->codec_type = AVMEDIA_TYPE_VIDEO; //解码类型
    codecCtx->width = width;  // 宽
    codecCtx->height = height; // 高
    codecCtx->channels = 0;
    codecCtx->time_base = (AVRational){1,obj.get_fps()}; // 每帧的时间
    codecCtx->framerate = (AVRational){obj.get_fps(),1}; // 帧率
    codecCtx->pix_fmt = hd_pix; //AV_PIX_FMT_DRM_PRIME
    codecCtx->gop_size = 12; // 每组多少帧
    codecCtx->max_b_frames = 0; // b帧最大间隔

    hwframe = av_hwframe_ctx_alloc(hwdevice);
    if(!hwframe){
        print_error(__LINE__,-1,"create hdwave frame context failed!");
        return -1;
    }
    hwframeCtx = (AVHWFramesContext *)(hwframe->data);
    hwframeCtx->format    = hd_pix;
    hwframeCtx->sw_format = sw_pix;
    hwframeCtx->width     = width;
    hwframeCtx->height    = height;
    /**
     *  帧池，会预分配，后面创建与硬件关联的帧时，会从该池后面获取相应的帧
     *  initial_pool_size与pool 至少要有一个不为空
    */
    // hwframeCtx->initial_pool_size = 20;
    hwframeCtx->pool = av_buffer_pool_init(20*sizeof(AVFrame),NULL);
    res = av_hwframe_ctx_init(hwframe);
    if(res < 0){
        print_error(__LINE__,res,"init hd frame context failed!");
        return res;
    }
    codecCtx->hw_frames_ctx = hwframe;
    codecCtx->hw_device_ctx = hwdevice;

    if(!strcmp(obj.get_protocol(),"rtsp")){
        // rtsp协议
        res = avformat_alloc_output_context2(&formatCtx,NULL,"rtsp",obj.get_url());
    }else{
        // rtmp协议
        res = avformat_alloc_output_context2(&formatCtx,NULL,"flv",obj.get_url());
    }
    if(res < 0){
        print_error(__LINE__,res,"create output context failed!");
        return res;
    }

    stream = avformat_new_stream(formatCtx,codec);
    if(!stream){
        print_error(__LINE__,res,"create stream failed!");
        return -1;
    }
    stream->time_base = (AVRational){1,obj.get_fps()}; // 设置帧率
    stream->id = formatCtx->nb_streams - 1; // 设置流的索引

    res = avcodec_parameters_from_context(stream->codecpar,codecCtx);
    if(res < 0){
        print_error(__LINE__,res,"copy parameters to stream failed!");
        return -1;
    }

    // 打开输出IO RTSP不需要打开，RTMP需要打开
    if(!strcmp(obj.get_protocol(),"rtmp")){
        res = avio_open2(&formatCtx->pb,obj.get_url(),AVIO_FLAG_WRITE,NULL,NULL);
        if(res < 0){
            print_error(__LINE__,res,"avio open failed !");
            return -1;
        }
    }
    // 写入头信息   
    AVDictionary *opt = NULL;
    if(!strcmp(obj.get_protocol(),"rtsp")){
        av_dict_set(&opt, "rtsp_transport",obj.get_trans_protocol(),0);
        av_dict_set(&opt, "muxdelay", "0.1", 0);
    }
	res = avformat_write_header(formatCtx, &opt);
	if(res < 0){
        print_error(__LINE__,res,"avformat write header failed ! ");
        return -1;
	}
    av_dump_format(formatCtx, 0, obj.get_url(), 1);
    cout << __LINE__ << " init encoder finished!" << endl;
    return res;
}

MPP_RET init_mpp(){
    MPP_RET res = MPP_OK;
    res = mpp_create(&mppCtx,&mppApi);
    res = mpp_init(mppCtx,MPP_CTX_ENC,MPP_VIDEO_CodingAVC);
    res = mpp_enc_cfg_init(&cfg);
    res = mppApi->control(mppCtx,MPP_ENC_GET_CFG,cfg);

    mpp_enc_cfg_set_s32(cfg, "prep:width", width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride * 3);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_BGR888);

    mpp_enc_cfg_set_s32(cfg, "rc:quality", MPP_ENC_RC_QUALITY_BEST);
    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_VBR);

    /* fix input / output frame rate */
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", 60);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm",1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", 60);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);

    /* all frame are I */
    mpp_enc_cfg_set_s32(cfg, "rc:gop", 2);
    /* drop frame or not when bitrate overflow */
    mpp_enc_cfg_set_u32(cfg, "rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED);
    mpp_enc_cfg_set_u32(cfg, "rc:drop_thd", 20);        /* 20% of max bps */
    mpp_enc_cfg_set_u32(cfg, "rc:drop_gap", 1);         /* Do not continuous drop frame */

    /* setup bitrate for different rc_mode */
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", 100*1024*1024 * 17 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", 100*1024*1024 * 15 / 16);

    mpp_enc_cfg_set_s32(cfg,"split:mode", MPP_ENC_SPLIT_NONE);
    // mpp_enc_cfg_set_s32(cfg,"split_arg", 0);
    // mpp_enc_cfg_set_s32(cfg,"split_out", 0);

    mpp_enc_cfg_set_s32(cfg, "h264:profile", 100);
    mpp_enc_cfg_set_s32(cfg, "h264:level", 42);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);
    mpp_enc_cfg_set_s32(cfg, "h264:trans8x8", 1);

    mppApi->control(mppCtx, MPP_ENC_SET_CFG, cfg);

    MppPacket headpacket;
    RK_U8 enc_hdr_buf[1024];
    memset(enc_hdr_buf,0,1024);

    // 获取头数据
    mpp_packet_init(&headpacket,enc_hdr_buf,1024);

    res = mppApi->control(mppCtx, MPP_ENC_GET_HDR_SYNC, headpacket);
    void *ptr   = mpp_packet_get_pos(headpacket);
    size_t len  = mpp_packet_get_length(headpacket);

    extra_data_size = len;
    cExtradata = (uint8_t *)malloc((extra_data_size) * sizeof(uint8_t));

    memcpy(cExtradata,ptr,len); // 拷贝头数据
    mpp_buffer_group_get_external(&group,MPP_BUFFER_TYPE_DRM);
    mpp_packet_deinit(&headpacket);
    return res;
}

int init_data(Command & obj){
    int res = 0;
    // 给packet 分配内存
    packet = av_packet_alloc();

    // thread fifo(fifo_open);  
    // thread live555(create_live555);    
    // live555.detach();
    // fifo.detach();

    fifo_open();
    // create_live555();
    return res;
}

MPP_RET read_frame(cv::Mat & cvframe,void * ptr){
    RK_U32 row = 0;
    RK_U32 read_size = 0;
    RK_U8 *buf_b = (RK_U8 *)ptr;
    RK_U8 *buf_g = buf_b + hor_stride * ver_stride; // NOTE: diff from gen_yuv_image
    RK_U8 *buf_r = buf_g + hor_stride * ver_stride; // NOTE: diff from gen_yuv_image
    // buf_y = cvframe.data;
    cv::Mat bgr[3];
    split(cvframe,bgr);

    for (row = 0; row < height; row++) {
        memcpy(buf_b + row * hor_stride,bgr[0].datastart + read_size,width);
        memcpy(buf_g + row * hor_stride,bgr[1].datastart + read_size,width);
        memcpy(buf_r + row * hor_stride,bgr[2].datastart + read_size,width);
        read_size += width;
    }
    return MPP_OK;
}

MPP_RET read_frame_rgb(cv::Mat & cvframe,void * ptr){
    RK_U32 row = 0;
    RK_U32 read_size = 0;
    RK_U8 *buf = (RK_U8 *)ptr;
    for(row=0; row < height; row++){
        memcpy(buf + row * hor_stride * 3,cvframe.datastart + width * 3 * row , width * 3);    
    }
    return MPP_OK;
}

int send_packet(Command &obj){
    int res = 0;
    // packet->pts = av_rescale_q_rnd(framecount, codecCtx->time_base, stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_NEAR_INF));
    // packet->dts = av_rescale_q_rnd(framecount, codecCtx->time_base, stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_NEAR_INF));
    // packet->duration = av_rescale_q_rnd(packet->duration, codecCtx->time_base, stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_NEAR_INF));
    packet->pts = av_gettime();
    if(!(packet->flags & AV_PKT_FLAG_KEY)){
        // 在每帧非关键帧前面添加PPS SPS头信息
        /**
         * 使用h264_rkmpp编码器时，rtsp/rtmp协议都需要添加PPS
         * libx264只需要在rtsp协议时添加PPS,rtmp会自动加上
        */
        int packet_data_size = packet->size;
        u_char frame_data[packet_data_size];
        memcpy(frame_data,packet->data,packet->size);
        memcpy(packet->data,cExtradata,extra_data_size);
        memcpy(packet->data+extra_data_size,frame_data,packet_data_size);
        packet->size = packet_data_size + extra_data_size;
    }
    // 通过创建输出流的format 输出数据包
    framecount++;
    // res = av_interleaved_write_frame(formatCtx, packet);

    fifo_write(packet);
    // if (res < 0){
    //     print_error(__LINE__,res,"send packet error!");
    //     return -1;
    // }
    return 0;
}
/**
 * 将opencv的帧转换成drm数据帧
 * 并送入mpi解码，转换成AvPacket
*/
MPP_RET convert_cvframe_to_drm(cv::Mat &cvframe,AVFrame *& avframe,Command & obj){
    MPP_RET res = MPP_OK;
    res = mpp_buffer_get(NULL,&buffer,image_size);
    if(res != MPP_OK){
        return res;
    }
    info.fd = mpp_buffer_get_fd(buffer);
    info.ptr = mpp_buffer_get_ptr(buffer);
    info.index = framecount;
    info.size = image_size;
    info.type = MPP_BUFFER_TYPE_DRM;
    // 将数据读入buffer
    // read_frame(cvframe,info.ptr);
    // read_frame_rgb(cvframe,info.ptr);
    // for(int row=0;row<height;row++){
    //     memcpy(info.ptr + row*hor_stride,cvframe.data+row * 3 * width,width * 3);    
    // }
    memcpy(info.ptr,cvframe.datastart,height * width * 3);

    res = mpp_buffer_commit(group,&info);
    if(res != MPP_OK){
        return res;
    }

    res = mpp_buffer_get(group,&commitBuffer,image_size);
    if(res != MPP_OK){
        return res;
    }

    mpp_frame_init(&mppframe);
    mpp_frame_set_width(mppframe,width);
    mpp_frame_set_height(mppframe,height);
    mpp_frame_set_hor_stride(mppframe,hor_stride * 3);
    mpp_frame_set_ver_stride(mppframe,ver_stride);
    mpp_frame_set_buf_size(mppframe,image_size);
    mpp_frame_set_buffer(mppframe,commitBuffer);
    /**
     * 使用mpp可以使用 YUV格式的数据外 还能使用RGB格式的数据
    */
    mpp_frame_set_fmt(mppframe,MPP_FMT_BGR888); // YUV420SP == NV12 
    mpp_frame_set_eos(mppframe,0);

    mpp_packet_init_with_buffer(&mppPacket, commitBuffer);
    mpp_packet_set_length(mppPacket, 0);
    // set frame
    res = mppApi->poll(mppCtx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    res = mppApi->dequeue(mppCtx, MPP_PORT_INPUT, &task);
    res = mpp_task_meta_set_packet(task,KEY_OUTPUT_PACKET,mppPacket);
    res = mpp_task_meta_set_frame(task, KEY_INPUT_FRAME, mppframe);
    res = mppApi->enqueue(mppCtx, MPP_PORT_INPUT, task);

    // get packet
    res = mppApi->poll(mppCtx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    res = mppApi->dequeue(mppCtx, MPP_PORT_OUTPUT, &task);
    res = mpp_task_meta_get_packet(task, KEY_OUTPUT_PACKET, &mppPacket);
    res = mppApi->enqueue(mppCtx, MPP_PORT_OUTPUT, task);

    int is_eoi = mpp_packet_is_eoi(mppPacket);

    const char * temp = ("create time :" +to_string(av_gettime())).c_str();
    if (mppPacket) {
        RKMPPPacketContext *pkt_ctx = (RKMPPPacketContext *)av_mallocz(sizeof(*pkt_ctx));
        pkt_ctx->packet = mppPacket;
        int keyframe = 0;
        // TODO: outside need fd from mppbuffer?
        packet->data = (uint8_t *)mpp_packet_get_data(mppPacket);
        packet->size = mpp_packet_get_length(mppPacket);

        packet->buf = av_buffer_create((uint8_t*)packet->data, packet->size,
            rkmpp_release_packet, pkt_ctx, AV_BUFFER_FLAG_READONLY);
        packet->pts = mpp_packet_get_pts(mppPacket);
        packet->dts = mpp_packet_get_dts(mppPacket);
        packet->opaque = (void *)temp;
        if (packet->pts <= 0)
            packet->pts = packet->dts;
        if (packet->dts <= 0)
            packet->dts = packet->pts;
        meta = mpp_packet_get_meta(mppPacket);
        if (meta)
            mpp_meta_get_s32(meta, KEY_OUTPUT_INTRA, &keyframe);
        if (keyframe){
            packet->flags |= AV_PKT_FLAG_KEY;
        }
    }

    send_packet(obj);
    return res;
}

MPP_RET convert_cvframe_to_drm(unsigned char * imageFrame,unsigned int data_size,Command & obj){
    MPP_RET res = MPP_OK;
    res = mpp_buffer_get(NULL,&buffer,image_size);
    if(res != MPP_OK){
        return res;
    }
    info.fd = mpp_buffer_get_fd(buffer);
    info.ptr = mpp_buffer_get_ptr(buffer);
    info.index = framecount;
    info.size = image_size;
    info.type = MPP_BUFFER_TYPE_DRM;
    // 将数据读入buffer
    // read_frame(cvframe,info.ptr);
    // read_frame_rgb(cvframe,info.ptr);
    // for(int row=0;row<height;row++){
    //     memcpy(info.ptr + row*hor_stride,cvframe.data+row * 3 * width,width * 3);    
    // }
    memcpy(info.ptr,imageFrame,data_size);

    res = mpp_buffer_commit(group,&info);
    if(res != MPP_OK){
        return res;
    }

    res = mpp_buffer_get(group,&commitBuffer,image_size);
    if(res != MPP_OK){
        return res;
    }
    mpp_frame_init(&mppframe);
    mpp_frame_set_width(mppframe,width);
    mpp_frame_set_height(mppframe,height);
    mpp_frame_set_hor_stride(mppframe,hor_stride * 3);
    mpp_frame_set_ver_stride(mppframe,ver_stride);
    mpp_frame_set_buf_size(mppframe,image_size);
    mpp_frame_set_buffer(mppframe,commitBuffer);
    /**
     * 使用mpp可以使用 YUV格式的数据外 还能使用RGB格式的数据
    */
    mpp_frame_set_fmt(mppframe,MPP_FMT_BGR888); // YUV420SP == NV12 
    mpp_frame_set_eos(mppframe,0);

    mpp_packet_init_with_buffer(&mppPacket, commitBuffer);
    mpp_packet_set_length(mppPacket, 0);
    // set frame
    res = mppApi->poll(mppCtx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    res = mppApi->dequeue(mppCtx, MPP_PORT_INPUT, &task);
    res = mpp_task_meta_set_packet(task,KEY_OUTPUT_PACKET,mppPacket);
    res = mpp_task_meta_set_frame(task, KEY_INPUT_FRAME, mppframe);
    res = mppApi->enqueue(mppCtx, MPP_PORT_INPUT, task);

    // get packet
    res = mppApi->poll(mppCtx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    res = mppApi->dequeue(mppCtx, MPP_PORT_OUTPUT, &task);
    res = mpp_task_meta_get_packet(task, KEY_OUTPUT_PACKET, &mppPacket);
    res = mppApi->enqueue(mppCtx, MPP_PORT_OUTPUT, task);

    int is_eoi = mpp_packet_is_eoi(mppPacket);

    if (mppPacket) {
        RKMPPPacketContext *pkt_ctx = (RKMPPPacketContext *)av_mallocz(sizeof(*pkt_ctx));
        pkt_ctx->packet = mppPacket;
        int keyframe = 0;
        // TODO: outside need fd from mppbuffer?
        packet->data = (uint8_t *)mpp_packet_get_data(mppPacket);
        packet->size = mpp_packet_get_length(mppPacket);

        packet->buf = av_buffer_create((uint8_t*)packet->data, packet->size,
            rkmpp_release_packet, pkt_ctx, AV_BUFFER_FLAG_READONLY);
        packet->pts = mpp_packet_get_pts(mppPacket);
        packet->dts = mpp_packet_get_dts(mppPacket);
        if (packet->pts <= 0)
            packet->pts = packet->dts;
        if (packet->dts <= 0)
            packet->dts = packet->pts;
        meta = mpp_packet_get_meta(mppPacket);
        if (meta)
            mpp_meta_get_s32(meta, KEY_OUTPUT_INTRA, &keyframe);
        if (keyframe){
            packet->flags |= AV_PKT_FLAG_KEY;
        }
    }

    send_packet(obj);
    return res;
}

int transfer_frame(cv::Mat &cvframe,Command &obj){
    int res = 0;
    
    convert_cvframe_to_drm(cvframe,frame,obj);

    if(buffer != NULL){
        mpp_buffer_put(buffer); // 清空buffer
        buffer = NULL;
    }
    if(commitBuffer != NULL){
        mpp_buffer_put(commitBuffer); // 清空buffer
        commitBuffer = NULL;
    }
    /**
     * TODO:为什么这里不需要deinit
     * 还是说编码完 对引用的buffer自动释放？
    */
    // mpp_packet_deinit(&mppPacket); 
    mpp_buffer_group_clear(group);
    mpp_frame_deinit(&mppframe);
    av_packet_unref(packet);
    return 0;
}

int transfer_frame(unsigned char * data,unsigned int data_size,Command &obj){
    int res = 0;
    
    convert_cvframe_to_drm(data,data_size,obj);

    if(buffer != NULL){
        mpp_buffer_put(buffer); // 清空buffer
        buffer = NULL;
    }
    if(commitBuffer != NULL){
        mpp_buffer_put(commitBuffer); // 清空buffer
        commitBuffer = NULL;
    }
    /**
     * TODO:为什么这里不需要deinit
     * 还是说编码完 对引用的buffer自动释放？
    */
    // mpp_packet_deinit(&mppPacket); 
    mpp_buffer_group_clear(group);
    mpp_frame_deinit(&mppframe);
    av_packet_unref(packet);
    return 0;
}

void destory_(){
    cout << "释放回收资源：" << endl;
    fifo_close();
    cout << "close fifo " << endl;

    mpp_buffer_group_put(group);
	// fclose(wf);
	if(formatCtx){
        // avformat_close_input(&avFormatCtx);
        avio_close(formatCtx->pb);
        avformat_free_context(formatCtx);
        formatCtx = 0;
        cout << "avformat_free_context(formatCtx)" << endl;
    }
    if(packet){
        av_packet_unref(packet);
        packet = NULL;
        cout << "av_free_packet(packet)" << endl;
    }

    if(frame){
        av_frame_free(&frame);
        frame = 0;
        cout << "av_frame_free(frame)" << endl;
    }
   
    if(codecCtx->hw_device_ctx){
        av_buffer_unref(&codecCtx->hw_device_ctx);
        cout << "av_buffer_unref(&codecCtx->hw_device_ctx)" << endl;
    }

    if(codecCtx){
        avcodec_close(codecCtx);
        codecCtx = 0;
        cout << "avcodec_close(codecCtx);" << endl;
    }
    if(cExtradata){
        free(cExtradata);
        cout << "free cExtradata " << endl;
    }
    if(hwdevice){
        av_buffer_unref(&hwdevice);
        cout << "av_buffer_unref hwdevice " << endl;
    }
    if(hwframe){
        av_buffer_unref(&hwframe);
        cout << "av_buffer_unref encodeHwBufRef " << endl;
    }
}

void create_frame(Mat &cvframe){
    cvframe = cv::Scalar(0,0,0);
    string temp = "frame " + to_string(framecount);
    putText(cvframe,temp,(Point){500,500},1,10,(Scalar){255,255,255},2);
}

void cv_test(){
    VideoCapture videoCap;
    videoCap.set(CAP_PROP_FRAME_WIDTH, 1920);//宽度
    videoCap.set(CAP_PROP_FRAME_HEIGHT, 1080);//高度
    // videoCap.set(CAP_PROP_FPS, 30);//帧率 帧/秒
    videoCap.set(CAP_PROP_FOURCC,VideoWriter::fourcc('M','J','P','G')); // 捕获格式
    // videoCap.set(CAP_PROP_FOURCC,VideoWriter::fourcc('I','4','2','0')); // 捕获格式
    // videoCap.set(CAP_PROP_CONVERT_RGB,0);
    videoCap.open(40);
    if(!videoCap.isOpened()){
        cout << "camera not open !" << endl;
        return;
    }
	int is_init_encoder = 0;
    Mat cvframe;
    struct timeval pre;
	while (videoCap.read(cvframe))
	{
        auto now = av_gettime();
		// imshow("video_show", cvframe);
        // if((waitKey(1) & 0xff) == 27){
        //     break;
        // }
        auto end = av_gettime();
        
        struct timeval start;
        gettimeofday(&start,NULL); //gettimeofday(&start,&tz);结果一样
        // printf("start.tv_sec:%d\n",start.tv_sec);
        // printf("start.tv_usec:%d\n",start.tv_usec);
        // time_t nowtime=time(NULL);
        // char tmp[64];
        // strftime(tmp,sizeof(tmp),"%Y-%m-%d %H:%M:%S",localtime(&nowtime));
        //locatime 将秒转换成 tm结构  //strftime 格式化输出 下面讲给具体解释
        if(framecount > 1){
            cout << framecount << ": ";
            cout << "["<< start.tv_sec << " :" << start.tv_usec <<"] [ "<< start.tv_usec - pre.tv_usec << " micro s] send frame use time [ "<< (end - now) / 1000 <<" ms] " << endl;
        }
        framecount ++ ;
        pre = start;
    }
    videoCap.release();
}

void cv_test_2(Command &obj){
	int is_init_encoder = 0,res = 0;
    struct timeval pre;
    Mat cvframe(1080,1920,CV_8UC3);
	while (true){
        auto now = av_gettime();
        cout << 1/30.0f * 1e9 << endl;
        usleep(1/30.0f * 1e9);
        create_frame(cvframe);
        auto create_frame_time = av_gettime();
        // imshow("video_show", cvframe);
        // if((waitKey(1) & 0xff) == 27){
        //     break;
        // }
        if(!is_init_encoder){
            
            Size size = cvframe.size();
            width = size.width;
            height = size.height;
            
            hor_stride = MPP_ALIGN(width, 16); // 1920
            ver_stride = MPP_ALIGN(height, 16); // 1088

            image_size = sizeof(unsigned char) * hor_stride *  ver_stride * 3;
            cout << " width " << width << endl;
            cout << " height " << height << endl;
            cout << " hor_stride " << hor_stride << endl;
            cout << " ver_stride " << ver_stride << endl;
			res = init_encoder(obj); //初始化解码器
            if(res < 0){
                print_error(__LINE__,res,"init encoder failed!");
                break;
            }
            res = init_data(obj);
            if(res < 0){
                print_error(__LINE__,res,"init data failed!");
                break;
            }
            res = init_mpp();
            if(res < 0){
                print_error(__LINE__,res,"init mpp failed!");
                break;
            }
			is_init_encoder = 1;
		}
		transfer_frame(cvframe,obj);

        auto end = av_gettime();
        struct timeval start;
        gettimeofday(&start,NULL);
        if(framecount > 1){
            cout << framecount << ": ";
            time_t nowtime=time(NULL);
            char tmp[64];
            strftime(tmp,sizeof(tmp),"%Y-%m-%d %H:%M:%S",localtime(&nowtime));
            auto c = (start.tv_usec - pre.tv_usec);
            cout << tmp << " ";
            cout << "["<< start.tv_sec << " :" << start.tv_usec <<"] [ "<<
              (c <0 ? (1000000 + c) :c)/1000.0  << " ms] send frame use time [ "<< (end - now) / 1000 <<" ms] ";
            cout << " create frame use time [ "<< (create_frame_time - now) /1000 << " ms]" << endl;
            
        }else{
            cout << framecount << ": ";
            time_t nowtime=time(NULL);
            char tmp[64];
            strftime(tmp,sizeof(tmp),"%Y-%m-%d %H:%M:%S",localtime(&nowtime));
            cout << tmp << " ";
            cout << "["<< start.tv_sec << " :" << start.tv_usec <<"] "<<
                " send frame use time [ "<< (end - now) / 1000 <<" ms] ";
            cout << " create frame use time [ "<< (create_frame_time - now) /1000 << " ms]" << endl;
            
        }
        pre = start;
    }
    cout << __LINE__ << " cv test 2 finished!" << endl;
}

int opencv_push_video(int argc,char * argv[]){
    Command obj = process_command(argc,argv);
    // cv_test_2(obj);
    int res = 0;
    VideoCapture videoCap;
    videoCap.set(CAP_PROP_FRAME_WIDTH, 1920);//宽度
    videoCap.set(CAP_PROP_FRAME_HEIGHT, 1080);//高度
    // videoCap.set(CAP_PROP_FPS, 30);//帧率 帧/秒
    videoCap.set(CAP_PROP_FOURCC,VideoWriter::fourcc('M','J','P','G')); // 捕获格式
    // videoCap.set(CAP_PROP_FOURCC,VideoWriter::fourcc('I','4','2','0')); // 捕获格式
    // videoCap.set(CAP_PROP_CONVERT_RGB,0);
    videoCap.open(40);
    if(!videoCap.isOpened()){
        cout << "camera not open !" << endl;
        return -1;
    }
	int is_init_encoder = 0;
    Mat cvframe;
	while (videoCap.read(cvframe))
	{
        auto now = av_gettime();
		// imshow("video_show", cvframe);
        // if((waitKey(1) & 0xff) == 27){
        //     break;
        // }
        if(!cvframe.data){
            continue;
        }
        // cvtColor(cvframe,yuvframe,COLOR_RGB2YUV_YV12);
        auto convert = av_gettime();

		if(!is_init_encoder){
            
            Size size = cvframe.size();
            width = size.width;
            height = size.height;
            
            hor_stride = MPP_ALIGN(width, 16); // 1920
            ver_stride = MPP_ALIGN(height, 16); // 1088

            image_size = sizeof(unsigned char) * hor_stride *  ver_stride * 3;
            cout << " width " << width << endl;
            cout << " height " << height << endl;
            cout << " hor_stride " << hor_stride << endl;
            cout << " ver_stride " << ver_stride << endl;
			res = init_encoder(obj); //初始化解码器
            if(res < 0){
                print_error(__LINE__,res,"init encoder failed!");
                break;
            }
            res = init_data(obj);
            if(res < 0){
                print_error(__LINE__,res,"init data failed!");
                break;
            }
            res = init_mpp();
            if(res < 0){
                print_error(__LINE__,res,"init mpp failed!");
                break;
            }
			is_init_encoder = 1;
		}
		transfer_frame(cvframe,obj);

        auto end = av_gettime();
        struct timeval start;

        gettimeofday(&start,NULL); //gettimeofday(&start,&tz);结果一样
        // printf("start.tv_sec:%d\n",start.tv_sec);
        // printf("start.tv_usec:%d\n",start.tv_usec);
        // time_t nowtime=time(NULL);
        // char tmp[64];
        // strftime(tmp,sizeof(tmp),"%Y-%m-%d %H:%M:%S",localtime(&nowtime));
        //locatime 将秒转换成 tm结构  //strftime 格式化输出 下面讲给具体解释
        cout << framecount << ": ";
        cout << "["<< start.tv_sec << " :" << start.tv_usec <<"]send frame use time [ "<< (end - now) / 1000 <<" ms] convert rgb2yuv use time [ "<< (convert - now) / 1000 <<" ms]";
        cout << " encoder use time [ "<< (end - convert) / 1000 <<" ms]" << endl;
        
        cvframe.release(); //释放cvframe 否则会内存泄漏
		av_packet_unref(packet);
	}
FAIL:
    videoCap.release();
    destory_();
}

struct image_buffer_with_plane
{   
	unsigned char ** mmpaddr;
	struct v4l2_plane * plans;
};


int v4l2_read(int argc, char * argv[]){
    Command obj = process_command(argc,argv);
    int res = 0;
    //1.打开摄像头设备
	int fd = open("/dev/video40", O_RDWR);
	if(fd < 0)
	{
		perror("打开设备失败");
		return -1;
	}
    width = 1920;
    height = 1080;
    hor_stride = MPP_ALIGN(width, 16); // 1920
    ver_stride = MPP_ALIGN(height, 16); // 1088
    image_size = sizeof(unsigned char) * hor_stride *  ver_stride * 3;
    cout << " width " << width << endl;
    cout << " height " << height << endl;
    cout << " hor_stride " << hor_stride << endl;
    cout << " ver_stride " << ver_stride << endl;

    res = init_data(obj);
    if(res < 0){
        print_error(__LINE__,res,"init data failed!");
        return -1;
    }
    res = init_mpp();
    if(res < 0){
        print_error(__LINE__,res,"init mpp failed!");
        return -1;
    }
    
	//2.设置摄像头采集格式
	struct v4l2_format vfmt;
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;	//选择视频抓取
	vfmt.fmt.pix.width = width;//设置宽，不能随意设置
	vfmt.fmt.pix.height = height;//设置高
	vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;//设置视频采集像素格式
    
    
	int ret = ioctl(fd, VIDIOC_S_FMT, &vfmt);// VIDIOC_S_FMT:设置捕获格式
	if(ret < 0)
	{
		perror("设置采集格式错误");
	}

	memset(&vfmt, 0, sizeof(vfmt));
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_G_FMT, &vfmt);	
	if(ret < 0)
	{
		perror("读取采集格式失败");
	}
	printf("width = %d\n", vfmt.fmt.pix.width);
	printf("height = %d\n", vfmt.fmt.pix.height);
	unsigned char *p = (unsigned char*)&vfmt.fmt.pix.pixelformat;
	printf("pixelformat = %c%c%c%c\n", p[0],p[1],p[2],p[3]);	
	printf("vfmt.fmt.pix.sizeimage = %d \n",vfmt.fmt.pix.sizeimage);
	printf("vfmt.fmt.pix_mp.num_planes = %d \n",vfmt.fmt.pix_mp.num_planes);
	int image_plans = vfmt.fmt.pix_mp.num_planes;

	//4.申请缓冲队列
	struct v4l2_requestbuffers reqbuffer;
	reqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	reqbuffer.count = 4;	//申请4个缓冲区
	reqbuffer.memory = V4L2_MEMORY_MMAP;	//采用内存映射的方式

	ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuffer);
	if(ret < 0)
	{
		perror("申请缓冲队列失败");
	}
	//映射，映射之前需要查询缓存信息->每个缓冲区逐个映射->将缓冲区放入队列

	struct image_buffer_with_plane bufferPlanes[reqbuffer.count];
	struct v4l2_buffer mapbuffer; //缓存查询buffer
	mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;//初始化type
	mapbuffer.memory = V4L2_MEMORY_MMAP;
	mapbuffer.length = image_plans; // buffer中包含plane的个数
	
	for(int i=0;i<reqbuffer.count;i++){
		bufferPlanes[i].mmpaddr = (unsigned char **)calloc(image_plans,sizeof(unsigned char *));
		bufferPlanes[i].plans = (struct v4l2_plane *)calloc(image_plans,sizeof(* bufferPlanes[i].plans));
		mapbuffer.m.planes = bufferPlanes[i].plans;
		mapbuffer.index = i;
		/**
		 * 对buffer进行查询
		*/
		ret = ioctl(fd, VIDIOC_QUERYBUF, &mapbuffer);	//查询缓存信息
		if(ret < 0)
			perror("查询缓存队列失败");
		/**
		 * 对buffer查到的每一个plane进行映射
		*/
		for(int j=0;j<image_plans;j++){
			printf("plane[%d]: length = %d\n", j, bufferPlanes[i].plans[j].length);
			printf("plane[%d]: offset = %d\n", j, bufferPlanes[i].plans[j].m.mem_offset);
			bufferPlanes[i].mmpaddr[j] = (unsigned char *)mmap(NULL, bufferPlanes[i].plans[j].length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, bufferPlanes[i].plans[j].m.mem_offset);
		}
	}
	/**
	 * 对每个buffer入队
	*/
	for(int i=0;i<reqbuffer.count;i++){
		mapbuffer.m.planes = bufferPlanes[i].plans;
		mapbuffer.index = i;
		ret = ioctl(fd, VIDIOC_QBUF, &mapbuffer);
		if(ret < 0)
			perror("放入队列失败");
	}
	

	for(int i=0;i<reqbuffer.count;i++){
		for(int j=0;j<image_plans;j++){
			printf("address :%x,size : %d \n",bufferPlanes[i].mmpaddr[j],bufferPlanes[i].plans[j].length);
		}
	}


	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMON, &type);
	if(ret < 0)
		perror("打开设备失败");
	//从队列中提取一帧数据
	struct v4l2_buffer readbuffer;
	struct v4l2_plane *temp_plane;
	temp_plane = (struct v4l2_plane *)calloc(image_plans,sizeof(*temp_plane));
	readbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	readbuffer.memory = V4L2_MEMORY_MMAP;
	readbuffer.m.planes = temp_plane;
	readbuffer.length = image_plans;

    unsigned char * image_data;
    image_data = (unsigned char *)malloc(sizeof(*image_data) * image_plans * vfmt.fmt.pix.sizeimage);
    while(1){
        auto now = av_gettime();
        auto convert = av_gettime();
        
        ret = ioctl(fd, VIDIOC_DQBUF, &readbuffer);//从缓冲队列获取一帧数据（出队列）
		//出队列后得到缓存的索引index,得到对应缓存映射的地址mmpaddr[readbuffer.index]
		if(ret < 0)
			perror("获取数据失败");
        int bias = 0;
		for(int x=0;x<image_plans;x++){
            memcpy(image_data + bias,bufferPlanes[readbuffer.index].mmpaddr[x],readbuffer.m.planes[x].bytesused);
            bias += readbuffer.m.planes[x].bytesused;
		}
		//读取数据后将缓冲区放入队列
		ret = ioctl(fd, VIDIOC_QBUF, &readbuffer);
		if(ret < 0)
			perror("放入队列失败");
        
        transfer_frame(image_data,vfmt.fmt.pix.sizeimage,obj);
        
        auto end = av_gettime();
        struct timeval start;

        gettimeofday(&start,NULL); //gettimeofday(&start,&tz);结果一样
        // printf("start.tv_sec:%d\n",start.tv_sec);
        // printf("start.tv_usec:%d\n",start.tv_usec);
        // time_t nowtime=time(NULL);
        // char tmp[64];
        // strftime(tmp,sizeof(tmp),"%Y-%m-%d %H:%M:%S",localtime(&nowtime));
        //locatime 将秒转换成 tm结构  //strftime 格式化输出 下面讲给具体解释
        cout << framecount << ": ";
        cout << "["<< start.tv_sec << " :" << start.tv_usec <<"]send frame use time [ "<< (end - now) / 1000 <<" ms] convert rgb2yuv use time [ "<< (convert - now) / 1000 <<" ms]";
        cout << " encoder use time [ "<< (end - convert) / 1000 <<" ms]" << endl;
        
        av_packet_unref(packet);
    }
	
	//关闭设备
	ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
	if(ret < 0)
		perror("关闭设备失败");
	//取消映射
	for(int i = 0; i < reqbuffer.count; i++){
		for(int j=0;j<image_plans;j++){
			munmap(bufferPlanes[i].mmpaddr[j], bufferPlanes[i].plans[j].length);
		}
	}
	close(fd);
    return 0;
}


int main(int argc, char * argv[]){
	v4l2_read(argc,argv);
    return 0;
}