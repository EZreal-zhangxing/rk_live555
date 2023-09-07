#include "mpp_decoder.h"
#include "ffmpeg_head.h"
#include "safely_queue.h"
#include <thread>
#include<SDL2/SDL.h>

using namespace std;
MppApi *mppApi;
MppCtx mppCtx;
MppBufferGroup group;
MppDecCfg cfg;

MppTask task;
MppBuffer commitBuffer;
MppPacket packet;
MppFrame frame;
static unsigned frame_buffer_size = 0;
static unsigned packet_size = 0;
AVFrame *avframe,*frameYUV;

typedef struct {
    MppFrame frame;
    AVBufferRef *decoder_ref;
} RKMPPFrameContext;


void rkmpp_release_frame(void *opaque, uint8_t *data){
    AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)data;
    AVBufferRef *framecontextref = (AVBufferRef *)opaque;
    RKMPPFrameContext *framecontext = (RKMPPFrameContext *)framecontextref->data;

    mpp_frame_deinit(&framecontext->frame);
    av_buffer_unref(&framecontext->decoder_ref);
    av_buffer_unref(&framecontextref);

    av_free(desc);
}


SDL_Window *win = NULL; //SDL窗口
SDL_Renderer *ren = NULL; // 渲染器
SDL_Texture *texture = NULL; // 纹理
SDL_Surface * surface = NULL; //表面
SDL_Rect rect; // 活动矩形
SDL_Event event; // 事件
void destroy(){
    if (mppCtx) {
        mpp_destroy(mppCtx);
        mppCtx = NULL;
    }
    if (frame) {
        mpp_frame_deinit(&frame);
        frame = NULL;
    }
    if (group) {
        mpp_buffer_group_put(group);
        group = NULL;
    }
    
    if(avframe){
        av_frame_free(&avframe);
        avframe = 0;
        cout << "av_frame_free(avframe)" << endl;
    }
    if(texture){
        SDL_DestroyTexture(texture);
        cout << "SDL_DestroyTexture(texture)" << endl;
    }
    // SDL_free(&rect);
    // cout << "SDL_free(&rect)" << endl;
    // SDL_free(&event);
    // cout << "SDL_free(&event);" << endl;
    if(ren){
        SDL_DestroyRenderer(ren);
        cout << "SDL_DestroyRenderer(ren)" << endl;
    }
    if(win){
        SDL_DestroyWindow(win);
        cout << "SDL_DestroyWindow(win)" << endl;
    }
    SDL_Quit();
}

int width = 1920,height = 1080;
int hor_stride = 1920,ver_stride = 1088;
int sdl_init_process(){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)){
        cout << "can not initialize sdl" << SDL_GetError();
        return -1;
    }
    //在(0,SDL_WINDOWPOS_CENTERED)处创建窗口，
    win = SDL_CreateWindow("video",50,SDL_WINDOWPOS_CENTERED,width,height,SDL_WINDOW_SHOWN);
    if(!win){
        cout << "create sdl win failed !" << SDL_GetError() << endl;
        return -1;
    }
    // 创建渲染器
    ren = SDL_CreateRenderer(win,-1,0);
    if(!ren){
        cout << "create sdl renderer failed !" << SDL_GetError() << endl;
        return -1;
    }
    // 创建指定像素格式的纹理
    // texture = SDL_CreateTexture(ren,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,width,height);
    texture = SDL_CreateTexture(ren,SDL_PIXELFORMAT_NV12,SDL_TEXTUREACCESS_STREAMING,width,height);
    if(!texture){
        cout << "create sdl texture failed !" << SDL_GetError() << endl;
        return -1;
    }
    // if(!TTF_Init()){
    //     cout << "sdl ttf initial failed !" <<  SDL_GetError() << endl;
    // }
    // font = TTF_OpenFont("/home/firefly/mpp/test_video/SimHei.ttf",30);
    return 0;
}

int event_process(SDL_Event *event){
    int is_exit = 0;
    switch (event->type)
		{
		case SDL_KEYDOWN:			//键盘事件
			switch (event->key.keysym.sym)
			{
			case SDLK_q:
				std::cout << "key down q, ready to exit" << std::endl;
                is_exit = 1;
				break;
			default:
				// printf("key down 0x%x\n", event->key.keysym.sym);
				break;
			}
			break;
        }
    return is_exit;
}


MPP_RET init_decoder(){
    MPP_RET res = MPP_OK;
    RK_U32 need_split = 0;
    RK_U32 enable_fast = 1;
    // res = mpp_buffer_group_get_external(&group,MPP_BUFFER_TYPE_DRM);
    
    res = mpp_create(&mppCtx,&mppApi);
    // mppApi->control(mppCtx,MPP_DEC_SET_PARSER_SPLIT_MODE,&need_split);
    mppApi->control(mppCtx,MPP_DEC_SET_ENABLE_FAST_PLAY,&enable_fast);
    MppFrameFormat out = MPP_FMT_YUV420P;
    
    res = mpp_init(mppCtx,MPP_CTX_DEC,MPP_VIDEO_CodingAVC);
    
    res = mppApi->control(mppCtx,MPP_DEC_SET_OUTPUT_FORMAT,&out);

    // res = mppApi->control(mppCtx,MPP_DEC_SET_EXT_BUF_GROUP,group);

    // res = mppApi->control(mppCtx,MPP_DEC_SET_DISABLE_ERROR,NULL);
    
    mpp_dec_cfg_init(&cfg);
    res = mppApi->control(mppCtx,MPP_DEC_GET_CFG,cfg);
    
    mpp_dec_cfg_set_u32(cfg, "base:split_parse", need_split);
    // cfg->base.type = MPP_CTX_BUTT;
    // cfg->base.coding = MPP_VIDEO_CodingUnused;
    // cfg->base.hw_type = -1;
    // cfg->base.fast_parse = 1;
    // cfg->base.enable_fast_play = MPP_ENABLE_FAST_PLAY;
    // mpp_dec_cfg_set_u32(cfg, "base:type", MPP_CTX_BUTT);
    // mpp_dec_cfg_set_u32(cfg, "base:hw_type", -1);
    // mpp_dec_cfg_set_u32(cfg, "base:fast_parse", 1);
    // mpp_dec_cfg_set_u32(cfg, "base:enable_fast_play", 1);

    res = mppApi->control(mppCtx,MPP_DEC_SET_CFG,cfg);
    // mpp_frame_init(&frame);
    // mpp_frame_set_width(frame,width);
    // mpp_frame_set_height(frame,height);
    // mpp_frame_set_hor_stride(frame,hstride);
    // mpp_frame_set_ver_stride(frame,vstride);
    // mpp_frame_set_buf_size(frame,vstride * hstride * 2);
    // RK_U32 width = mpp_frame_get_width(frame);
    // RK_U32 height = mpp_frame_get_height(frame);
    // RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
    // RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);
    // RK_U32 buf_size = mpp_frame_get_buf_size(frame);

    // printf("%p decode_get_frame get info changed found\n", mppCtx);
    // printf("%p decoder require buffer w:h [%d:%d] stride [%d:%d] buf_size %d \n",mppCtx, width, height, hor_stride, ver_stride, buf_size);

    // res = mpp_buffer_group_get_internal(&group,MPP_BUFFER_TYPE_DRM);
    // res = mpp_buffer_group_limit_config(&group, buf_size, 24);
    // mppApi->control(mppCtx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);

    avframe = av_frame_alloc();
    av_image_alloc(avframe->data,avframe->linesize,width,height,AV_PIX_FMT_YUV420P,1);
    // frameYUV = av_frame_alloc();
    // int frameYUVsize = av_image_alloc(frameYUV->data,frameYUV->linesize,width,height,AV_PIX_FMT_YUV420P,1);
    

    printf("************************************* \n");
    printf("*       init decode finished !      * \n");
    printf("************************************* \n");
    return res;
}

MPP_RET send_data_to_decoder(RTSPReceiveData &data){
    MPP_RET res = MPP_OK;
    // MppBuffer buffer;
    // frame_buffer_size = 1920*1088*2;
    packet_size = data.size;
    // res = mpp_buffer_get(NULL,&buffer,frame_buffer_size);
    
    // MppBufferInfo info;
    // info.fd = mpp_buffer_get_fd(buffer);
    // info.ptr = mpp_buffer_get_ptr(buffer);
    // info.index = 0;
    // info.size = packet_size;
    // info.type = MPP_BUFFER_TYPE_DRM;

    mpp_packet_init(&packet,NULL,0);
    mpp_packet_set_data(packet, data.dataPtr);
    mpp_packet_set_size(packet, packet_size);
    mpp_packet_set_pos(packet, data.dataPtr);
    mpp_packet_set_length(packet, packet_size);
    // mpp_packet_set_eos(packet);
    // memcpy(info.ptr,data.dataPtr,data.size);

    // unsigned char * ptr = (unsigned char *)data.dataPtr;
    // for(int i=0;i<data.size;i++){
    //     printf("%2x ",ptr[i]);
    //     if(i > 0 && i % 29 == 0){
    //         printf("\n");
    //     }
    // }
    // res = mpp_buffer_commit(group,&info);

    // mpp_frame_init(&frame);
    
    // res = mpp_buffer_get(group,&commitBuffer,frame_buffer_size);

   
    // std::cout << " mpp_buffer_commit " << res << std::endl;
    // printf("write data into info and commit !\n");
    return res;
}

MPP_RET decode_process(){
    // std::cout << "start decode processing ...." << std::endl;
    MPP_RET res = MPP_OK;
    
    // mpp_buffer_get(group,&commitBuffer,packet_size);

    // mpp_frame_init(&frame);
    // mpp_frame_set_height(frame,1080);
    // mpp_frame_set_fmt(frame,MPP_FMT_YUV420SP);
    // mpp_frame_set_width(frame,1920);
    // mpp_frame_set_eos(frame,0);

    // std::cout << " commit buffer " << mpp_buffer_get_ptr(commitBuffer) << " " << mpp_buffer_get_size(commitBuffer)<< std::endl;
    // res = mpp_packet_init_with_buffer(&packet,commitBuffer);
    // mpp_packet_set_data(packet, mpp_buffer_get_ptr(commitBuffer));
    // mpp_packet_set_size(packet, packet_size);
    // mpp_packet_set_pos(packet, mpp_buffer_get_ptr(commitBuffer));
    // mpp_packet_set_length(packet, packet_size);
    // mpp_packet_set_pts(packet,0);
    // mpp_packet_set_dts(packet,0);
    // mpp_packet_set_buffer(packet,commitBuffer);
    // mpp_packet_set_length(packet,frame_buffer_size);
    // RK_U32 immeditate_out = 1;
    // mppApi->control(mppCtx,MPP_DEC_SET_FRAME_INFO,frame);
    // mppApi->control(mppCtx,MPP_DEC_SET_IMMEDIATE_OUT,&immeditate_out);

    // std::cout << " mpp_packet_init_with_buffer " << res << " " << mpp_packet_get_buffer(packet) << std::endl;
    res = mppApi->decode_put_packet(mppCtx,packet);
    // std::cout << " commit to decode!" << mpp_packet_get_buffer(packet) << std::endl;
    // std::cout << " decode_put_packet! " << res <<  std::endl;
    do{
        res = mppApi->decode_get_frame(mppCtx,&frame);
        std::cout << " decode_get_frame! " << res << " " << frame <<  std::endl;
        // printf("%s\n", log_buf);
        if(frame){
            char log_buf[256];
            RK_S32 log_size = sizeof(log_buf) - 1;
            RK_S32 log_len = 0;
            RK_U32 err_info = mpp_frame_get_errinfo(frame);
            RK_U32 discard = mpp_frame_get_discard(frame);
            printf(" err %x discard %x \n", err_info, discard);
            break;
        }
    }while(1);
    
    // // // set frame
    // // res = mppApi->poll(mppCtx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    // // res = mppApi->dequeue(mppCtx, MPP_PORT_INPUT, &task);
    // // res = mpp_task_meta_set_packet(task,KEY_INPUT_PACKET,packet);
    // // res = mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, frame);
    // // res = mppApi->enqueue(mppCtx, MPP_PORT_INPUT, task);
    
    // // std::cout << " decode_get_frame " << res <<  std::endl;
    // // // get packet
    // // res = mppApi->poll(mppCtx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    // // res = mppApi->dequeue(mppCtx, MPP_PORT_OUTPUT, &task);
    // // res = mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &frame);
    // // res = mppApi->enqueue(mppCtx, MPP_PORT_OUTPUT, task);

    // std::cout << " decode " << res << std::endl;
    // std::cout << " decode frame is " << frame << std::endl;
    // std::cout << " decode frame width is " << mpp_frame_get_width(frame) << std::endl;
    MppBuffer decodeBuffer = mpp_packet_get_buffer(frame);
    // // data.end_loop = 1;

    AVDRMFrameDescriptor* desc = (AVDRMFrameDescriptor*)av_mallocz(sizeof(AVDRMFrameDescriptor));
    if (!desc) {
        return MPP_NOK;
    }
    desc->nb_objects = 1;
    desc->objects[0].fd = mpp_buffer_get_fd(decodeBuffer);
    desc->objects[0].size = mpp_buffer_get_size(decodeBuffer);

    desc->nb_layers = 1;
    AVDRMLayerDescriptor *layer = &desc->layers[0];
    layer->format = DRM_FORMAT_YUV420;
    layer->nb_planes = 2;

    // Y 分量
    layer->planes[0].object_index = 0;
    layer->planes[0].offset = 0;
    layer->planes[0].pitch = mpp_frame_get_hor_stride(frame); // 1920

    // 第二层分量
    layer->planes[1].object_index = 0;
    layer->planes[1].offset = layer->planes[0].pitch * mpp_frame_get_ver_stride(frame); // 1920 * 1088
    layer->planes[1].pitch = layer->planes[0].pitch;

    avframe->reordered_opaque = avframe->pts;

    avframe->color_range      = (AVColorRange)mpp_frame_get_color_range(frame);
    avframe->color_primaries  = (AVColorPrimaries)mpp_frame_get_color_primaries(frame);
    avframe->color_trc        = (AVColorTransferCharacteristic)mpp_frame_get_color_trc(frame);
    avframe->colorspace       = (AVColorSpace)mpp_frame_get_colorspace(frame);

    auto mode = mpp_frame_get_mode(frame);
    avframe->interlaced_frame = ((mode & MPP_FRAME_FLAG_FIELD_ORDER_MASK) == MPP_FRAME_FLAG_DEINTERLACED);
    avframe->top_field_first  = ((mode & MPP_FRAME_FLAG_FIELD_ORDER_MASK) == MPP_FRAME_FLAG_TOP_FIRST);

    AVBufferRef * framecontextref = (AVBufferRef *)av_buffer_allocz(sizeof(AVBufferRef));
    if (!framecontextref) {
        return MPP_NOK;
    }

    // MPP decoder needs to be closed only when all frames have been released.
    RKMPPFrameContext * framecontext = (RKMPPFrameContext *)framecontextref->data;
    framecontext->frame = frame;
    
    avframe->data[0]  = (uint8_t *)desc;
    avframe->buf[0]   = av_buffer_create((uint8_t *)desc, sizeof(*desc), rkmpp_release_frame,
                                       framecontextref, AV_BUFFER_FLAG_READONLY);
    mpp_packet_deinit(&packet);
    // mpp_frame_deinit(&frame);
    // mpp_buffer_put(commitBuffer);
    // // mpp_buffer_group_put(group);
    // std::cout << " encoder finished!" << std::endl;
    return res;
}

void print_data(RTSPReceiveData &d){
    unsigned char * ptr = (unsigned char *)d.dataPtr;
    int lines = 0;
    for(int i=0;i<d.size;i++){
        printf("%2x ",ptr[i]);
        if(i> 0 && i%50 == 0){
            lines++;
            printf("\n");
        }
        if(lines > 5){
            break;
        }
    }
    printf("\n");
}

MPP_RET decode_get_frame(){
    MPP_RET res = MPP_OK;
    res = mppApi->decode_get_frame(mppCtx,&frame);
    // std::cout << " decode_get_frame! " << res << " " << frame <<  std::endl;
    // printf("%s\n", log_buf);
    if(!frame){
        std::cout << "frame is null" << std::endl;
        return MPP_NOK;
    }
    
    // // set frame
    // res = mppApi->poll(mppCtx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    // res = mppApi->dequeue(mppCtx, MPP_PORT_INPUT, &task);
    // res = mpp_task_meta_set_packet(task,KEY_INPUT_PACKET,packet);
    // res = mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, frame);
    // res = mppApi->enqueue(mppCtx, MPP_PORT_INPUT, task);
    
    // std::cout << " decode_get_frame " << res <<  std::endl;
    // // get packet
    // res = mppApi->poll(mppCtx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    // res = mppApi->dequeue(mppCtx, MPP_PORT_OUTPUT, &task);
    // res = mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &frame);
    // res = mppApi->enqueue(mppCtx, MPP_PORT_OUTPUT, task);

    std::cout << " decode " << res << std::endl;
    std::cout << " decode frame is " << frame << std::endl;
    std::cout << " decode frame width is " << mpp_frame_get_width(frame) << std::endl;
    MppBuffer decodeBuffer = mpp_packet_get_buffer(frame);
    // data.end_loop = 1;

    AVDRMFrameDescriptor* desc = (AVDRMFrameDescriptor*)av_mallocz(sizeof(AVDRMFrameDescriptor));
    if (!desc) {
        return MPP_NOK;
    }
    desc->nb_objects = 1;
    desc->objects[0].fd = mpp_buffer_get_fd(decodeBuffer);
    desc->objects[0].size = mpp_buffer_get_size(decodeBuffer);

    desc->nb_layers = 1;
    AVDRMLayerDescriptor *layer = &desc->layers[0];
    layer->format = DRM_FORMAT_YUV420;
    layer->nb_planes = 2;

    // Y 分量
    layer->planes[0].object_index = 0;
    layer->planes[0].offset = 0;
    layer->planes[0].pitch = mpp_frame_get_hor_stride(frame); // 1920

    // 第二层分量
    layer->planes[1].object_index = 0;
    layer->planes[1].offset = layer->planes[0].pitch * mpp_frame_get_ver_stride(frame); // 1920 * 1088
    layer->planes[1].pitch = layer->planes[0].pitch;

    avframe->reordered_opaque = avframe->pts;

    avframe->color_range      = (AVColorRange)mpp_frame_get_color_range(frame);
    avframe->color_primaries  = (AVColorPrimaries)mpp_frame_get_color_primaries(frame);
    avframe->color_trc        = (AVColorTransferCharacteristic)mpp_frame_get_color_trc(frame);
    avframe->colorspace       = (AVColorSpace)mpp_frame_get_colorspace(frame);

    auto mode = mpp_frame_get_mode(frame);
    avframe->interlaced_frame = ((mode & MPP_FRAME_FLAG_FIELD_ORDER_MASK) == MPP_FRAME_FLAG_DEINTERLACED);
    avframe->top_field_first  = ((mode & MPP_FRAME_FLAG_FIELD_ORDER_MASK) == MPP_FRAME_FLAG_TOP_FIRST);

    AVBufferRef * framecontextref = (AVBufferRef *)av_buffer_allocz(sizeof(AVBufferRef));
    if (!framecontextref) {
        return MPP_NOK;
    }

    // MPP decoder needs to be closed only when all frames have been released.
    RKMPPFrameContext * framecontext = (RKMPPFrameContext *)framecontextref->data;
    framecontext->frame = frame;
    
    avframe->data[0]  = (uint8_t *)desc;
    avframe->buf[0]   = av_buffer_create((uint8_t *)desc, sizeof(*desc), rkmpp_release_frame,
                                       framecontextref, AV_BUFFER_FLAG_READONLY);
    mpp_packet_deinit(&packet);
    mpp_frame_deinit(&frame);
    mpp_buffer_put(commitBuffer);
    // mpp_buffer_group_put(group);
    std::cout << " encoder finished!" << std::endl;
    return res;

}


MPP_RET display(){
    MPP_RET res = MPP_OK;
    int timeout = 0;
    mppApi->control(mppCtx, MPP_SET_OUTPUT_TIMEOUT, (MppParam)&timeout);
    res = mppApi->decode_get_frame(mppCtx,&frame);
    std::cout << __LINE__ << " " << res << " " << frame << std::endl;
    if(frame){
        std::cout << __LINE__ << " ******************************************************************************* " << std::endl;

        avframe->format           = AV_PIX_FMT_DRM_PRIME;
        avframe->width            = mpp_frame_get_width(frame);
        avframe->height           = mpp_frame_get_height(frame);
        avframe->pts              = mpp_frame_get_pts(frame);
        
        MppBuffer decodeBuffer = mpp_frame_get_buffer(frame);
        AVDRMFrameDescriptor* desc = (AVDRMFrameDescriptor*)av_mallocz(sizeof(AVDRMFrameDescriptor));
        if (!desc) {
            return MPP_NOK;
        }
        desc->nb_objects = 1;
        desc->objects[0].fd = mpp_buffer_get_fd(decodeBuffer);
        desc->objects[0].size = mpp_buffer_get_size(decodeBuffer);
        desc->nb_layers = 1;
        AVDRMLayerDescriptor *layer = &desc->layers[0];
        layer->format = DRM_FORMAT_YUV420;
        layer->nb_planes = 2;

        // Y 分量
        layer->planes[0].object_index = 0;
        layer->planes[0].offset = 0;
        layer->planes[0].pitch = mpp_frame_get_hor_stride(frame); // 1920

        // 第二层分量
        layer->planes[1].object_index = 0;
        layer->planes[1].offset = layer->planes[0].pitch * mpp_frame_get_ver_stride(frame); // 1920 * 1088
        layer->planes[1].pitch = layer->planes[0].pitch;

        avframe->reordered_opaque = avframe->pts;

        avframe->color_range      = (AVColorRange)mpp_frame_get_color_range(frame);
        avframe->color_primaries  = (AVColorPrimaries)mpp_frame_get_color_primaries(frame);
        avframe->color_trc        = (AVColorTransferCharacteristic)mpp_frame_get_color_trc(frame);
        avframe->colorspace       = (AVColorSpace)mpp_frame_get_colorspace(frame);

        auto mode = mpp_frame_get_mode(frame);
        avframe->interlaced_frame = ((mode & MPP_FRAME_FLAG_FIELD_ORDER_MASK) == MPP_FRAME_FLAG_DEINTERLACED);
        avframe->top_field_first  = ((mode & MPP_FRAME_FLAG_FIELD_ORDER_MASK) == MPP_FRAME_FLAG_TOP_FIRST);

        AVBufferRef * framecontextref = (AVBufferRef *)av_buffer_allocz(sizeof(AVBufferRef));
        if (!framecontextref) {
            return MPP_NOK;
        }

        // MPP decoder needs to be closed only when all frames have been released.
        RKMPPFrameContext * framecontext = (RKMPPFrameContext *)framecontextref->data;
        framecontext->frame = frame;
        
        avframe->data[0]  = (uint8_t *)desc;
        avframe->buf[0]   = av_buffer_create((uint8_t *)desc, sizeof(*desc), rkmpp_release_frame,
                                        framecontextref, AV_BUFFER_FLAG_READONLY);
        
        // mpp_frame_deinit(&frame);
        // mpp_buffer_put(commitBuffer);
        // // mpp_buffer_group_put(group);
        // std::cout << " encoder finished!" << std::endl;
        // display(avframe);

        rect.x = 0;
        rect.y = 0;
        rect.w = width;
        rect.h = height;
        SDL_UpdateYUVTexture(texture,&rect,
            avframe->data[0],avframe->linesize[0],
            avframe->data[1],avframe->linesize[1],
            avframe->data[2],avframe->linesize[2]);

        // 清空渲染器
        SDL_RenderClear(ren);
        // 将纹理拷贝至渲染器
        SDL_RenderCopy(ren, texture,NULL, &rect);
        SDL_RenderPresent(ren);
        // SDL事件处理
        SDL_PollEvent(&event);
        
        int ext = event_process(&event);
        // ext = 1;
        if(ext){
            // fclose(write_file);
            destroy();
            return MPP_NOK;
        }
    }
    return res;
    // mpp_frame_deinit(frame);
}

int find_first_i_frame = 0;


MPP_RET display_in_YUV(MppBuffer &decodeBuffer){
    unsigned row = 0;
    unsigned read_size = 0;
    /* YYYY...UV|UV|UV....**/
    uint8_t * start = (uint8_t *)mpp_buffer_get_ptr(decodeBuffer);// YUV420P-NV12格式的数据
    uint8_t * start_u = start + hor_stride * ver_stride;
    uint8_t *buf_y = avframe->data[0];
    uint8_t *buf_u = avframe->data[1];
    uint8_t *buf_v = avframe->data[2];
    avframe->linesize[0] = width;
    avframe->linesize[1] = width/2;
    avframe->linesize[2] = width/2;

    for(row = 0;row<height;row++){
        memcpy(buf_y + row * width,start + read_size, width);
        read_size += hor_stride;
    }

    for (int i = 0; i < height / 2; i++) {
        for (int j = 0; j < width; j++) {
            buf_u[j] = start_u[2 * j + 0];
            buf_v[j] = start_u[2 * j + 1];
        }
        buf_u += width/2;
        buf_v += width/2;
        start_u += hor_stride;
    }

    // av_hwframe_transfer_data(frameYUV, avframe, 0);
    rect.x = 0;
    rect.y = 0;
    rect.w = width;
    rect.h = height;
    // SDL_UpdateTexture(texture,&rect,start,1920);
    // SDL_UpdateNVTexture(texture,&rect,avframe->data[0],avframe->linesize[0],avframe->data[1],1920);

    SDL_UpdateYUVTexture(texture,&rect,
        avframe->data[0],avframe->linesize[0],
        avframe->data[1],avframe->linesize[1],
        avframe->data[2],avframe->linesize[2]);

    // 清空渲染器
    SDL_RenderClear(ren);
    // 将纹理拷贝至渲染器
    SDL_RenderCopy(ren, texture,NULL, &rect);
    SDL_RenderPresent(ren);
    mpp_frame_deinit(&frame);
    // SDL事件处理
    SDL_PollEvent(&event);
    
    int ext = event_process(&event);
    // ext = 1;
    if(ext){
        // fclose(write_file);
        destroy();
        return MPP_NOK;
    }
    return MPP_OK;
}


MPP_RET display_in_NV12(MppBuffer &decodeBuffer){
    unsigned row = 0;
    unsigned read_size = 0;
    /* YYYY...UV|UV|UV....**/
    uint8_t * start = (uint8_t *)mpp_buffer_get_ptr(decodeBuffer);// YUV420P-NV12格式的数据
    uint8_t * remake = (uint8_t *)malloc(width * height * 3/2);
    uint8_t * uv_ptr = remake + width*height;

    for(row = 0;row<height;row++){
        memcpy(remake + row * width,start + read_size, width);
        read_size += hor_stride;
    }
    read_size = hor_stride * ver_stride;

    for(row = 0;row<height/2;row++){
        memcpy(uv_ptr + row * width,start + read_size, width);
        read_size += hor_stride;
    }

    // av_hwframe_transfer_data(frameYUV, avframe, 0);
    rect.x = 0;
    rect.y = 0;
    rect.w = width;
    rect.h = height;
    SDL_UpdateTexture(texture,&rect,remake,width);
    // SDL_UpdateNVTexture(texture,&rect,avframe->data[0],avframe->linesize[0],avframe->data[1],1920);
    // 清空渲染器
    SDL_RenderClear(ren);
    // 将纹理拷贝至渲染器
    SDL_RenderCopy(ren, texture,NULL, &rect);
    SDL_RenderPresent(ren);

    free(remake);
    mpp_frame_deinit(&frame);
    // SDL事件处理
    SDL_PollEvent(&event);
    
    int ext = event_process(&event);
    // ext = 1;
    if(ext){
        // fclose(write_file);
        destroy();
        return MPP_NOK;
    }
    return MPP_OK;
}

void check_data_time(RTSPReceiveData *data){
    if((data->dataPtr[4] & 0x000000ff) == 103){
        int64_t time;
        memcpy(&time,data->dataPtr + 48,8);
        int64_t now_time = av_gettime();
        printf("packet time [%ld] ,[%.3f] ms send to decode delay, send to decode time [%ld]\n",time,(now_time - time)/1000.0,now_time);
    }
}

void print_packet_time_info(RTSPReceiveData *data){
    int64_t now_time = av_gettime();
    data->decode_time = now_time;
    // if(data->create_time != 0){
        printf("packet present time [%lu-%lu] bytes [%x]\n",data->presentationTime.tv_sec,data->presentationTime.tv_usec,data->dataPtr[4]);
        printf("packet create time [%ld],packet size [%5lu], arrive time delay [%3.3f],send to memory delay [%3.3f],decode pop out delay[%3.3f] \n",
            data->create_time,
            data->size,
            (data->arrive_time - data->create_time)/1000.0,
            (data->send_memory_time-data->create_time)/1000.0,
            (data->decode_time - data->create_time)/1000.0);
    // }
    
}

int read_data_and_decode(DummySink *sink){
    int res = 0;
    RTSPReceiveData *data = sink->pop_data(0);
    if(data != NULL){
        // print_data(*data);
        print_packet_time_info(data);
        // printf(" queue lens [%lu] \n",sink->receive_queue.size());
         // 0110 当前数据不是i帧 并且还没送入i帧给解码器
        // printf(" byte 4 %2x & ff = %d \n",data->dataPtr[4],(data->dataPtr[4] & 0x000000ff));
        if(!find_first_i_frame && ((data->dataPtr[4] & 0x000000ff) != 6)){
            delete data;
            return 1;
        }
        find_first_i_frame = 1;
        RK_S64 pts = (data->presentationTime.tv_sec * 1000000 + data->presentationTime.tv_usec) * 1000;

        mpp_packet_init(&packet,NULL,0);
        
        mpp_packet_set_data(packet, data->dataPtr);
        mpp_packet_set_size(packet, data->size);
        mpp_packet_set_pos(packet, data->dataPtr);
        mpp_packet_set_length(packet, data->size);
        mpp_packet_set_pts(packet,pts);
        // res = mppApi->control(mppCtx, MPP_DEC_SET_FRAME_INFO, (MppParam) frame);
        // int ret = 1;
        // res = mppApi->control(mppCtx, MPP_DEC_SET_IMMEDIATE_OUT, &ret);
        // res = mppApi->decode(mppCtx,packet,&frame);

        // cout << __LINE__ << " decode " << res << " frame " << frame << endl;
        res = mppApi->decode_put_packet(mppCtx,packet);
        // std::cout << __LINE__ << " send data to mpp " << res << std::endl;
        
        // int timeout = 0;
        // mppApi->control(mppCtx, MPP_SET_OUTPUT_TIMEOUT, (MppParam)&timeout);
        res = mppApi->decode_get_frame(mppCtx,&frame);
        // std::cout << __LINE__ << " " << res << " " << frame << std::endl;

        if(frame){
            if(mpp_frame_get_info_change(frame)){
                width = mpp_frame_get_width(frame);
                height = mpp_frame_get_height(frame);
                hor_stride = mpp_frame_get_hor_stride(frame);
                ver_stride = mpp_frame_get_ver_stride(frame);
                RK_U32 buf_size = mpp_frame_get_buf_size(frame);

                sdl_init_process();
                
                printf("%p decode_get_frame get info changed found\n", mppCtx);
                printf("%p decoder require buffer w:h [%d:%d] stride [%d:%d] buf_size %d \n",mppCtx, width, height, hor_stride, ver_stride, buf_size);

                res = mpp_buffer_group_get_internal(&group,MPP_BUFFER_TYPE_DRM);
                res = mpp_buffer_group_limit_config(&group, buf_size, 24);
                mppApi->control(mppCtx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);

            }else{
                RK_U32 err_info = mpp_frame_get_errinfo(frame);
                RK_U32 discard = mpp_frame_get_discard(frame);

                if (err_info || discard) {
                    printf("errod %x discard %x",err_info,discard);
                }

                // std::cout << __LINE__ << " ******************************************************************************* " << std::endl;

                avframe->format           = AV_PIX_FMT_NV12;
                avframe->width            = mpp_frame_get_width(frame);
                avframe->height           = mpp_frame_get_height(frame);
                avframe->pts              = mpp_frame_get_pts(frame);

                // cout << " frame width " << avframe->width << endl;
                // cout << " frame height " << avframe->height << endl;
                // cout << " frame hor_stride " << mpp_frame_get_hor_stride(frame) << endl;
                // cout << " frame ver_stride " << mpp_frame_get_ver_stride(frame) << endl;
                // cout << " frame pts " << avframe->pts/(long)1e9 << "-"<< avframe->pts % (long)1e9 << endl;
                // cout << " frame format " << mpp_frame_get_fmt(frame) << endl;
                // cout << " frame size " << mpp_frame_get_buf_size(frame) << endl;

                MppBuffer decodeBuffer = mpp_frame_get_buffer(frame);

                // cout << " frame buffer " << decodeBuffer << endl;
                res = display_in_NV12(decodeBuffer);
                printf("display packet create time [%lu], display end ! [%.3f]\n",data->create_time,(av_gettime() - data->create_time)/1000.0);
            }
            
           
            // mpp_buffer_put(commitBuffer);
            // // mpp_buffer_group_put(group);
            // std::cout << " encoder finished!" << std::endl;
            // display(avframe);
            
        }
        delete data;
        mpp_packet_deinit(&packet);
    }
    return res;
}

class DisplayFrame{
public:
    DisplayFrame() = default;
    DisplayFrame(MppBuffer mppbuffer,int64_t create):buffer(mppbuffer),create_time(create) {}
public:
    MppBuffer buffer;
    int64_t create_time;
    int64_t display_time;

};

ConcurrenceQueue<DisplayFrame *> frame_queue;

int smart_read_data_and_decode(DummySink *sink){
    int res = 0;
    RTSPReceiveData *data = sink->pop_data(0);
    if(data != NULL){
        // print_data(*data);
        print_packet_time_info(data);
        // if((data->dataPtr[4] & 0x000000ff) != 103){
        //     return -1;
        // }
        // printf(" queue lens [%lu] \n",sink->receive_queue.size());
         // 0110 当前数据不是i帧 并且还没送入i帧给解码器
        // printf(" byte 4 %2x & ff = %d \n",data->dataPtr[4],(data->dataPtr[4] & 0x000000ff));
        if(!find_first_i_frame && ((data->dataPtr[4] & 0x000000ff) != 6)){
            delete data;
            return 1;
        }
        find_first_i_frame = 1;

        mpp_packet_init(&packet,NULL,0);
        RK_S64 pts = (data->presentationTime.tv_sec * 1000000 + data->presentationTime.tv_usec) * 1000;
        mpp_packet_set_data(packet, data->dataPtr);
        mpp_packet_set_size(packet, data->size);
        mpp_packet_set_pos(packet, data->dataPtr);
        mpp_packet_set_length(packet, data->size);
        mpp_packet_set_pts(packet,pts);
        // res = mppApi->control(mppCtx, MPP_DEC_SET_FRAME_INFO, (MppParam) frame);
        // int ret = 1;
        // res = mppApi->control(mppCtx, MPP_DEC_SET_IMMEDIATE_OUT, &ret);
        // res = mppApi->decode(mppCtx,packet,&frame);

        // cout << __LINE__ << " decode " << res << " frame " << frame << endl;
        res = mppApi->decode_put_packet(mppCtx,packet);
        // std::cout << __LINE__ << " send data to mpp " << res << std::endl;
        
        // int timeout = 0;
        // mppApi->control(mppCtx, MPP_SET_OUTPUT_TIMEOUT, (MppParam)&timeout);
        res = mppApi->decode_get_frame(mppCtx,&frame);
        // std::cout << __LINE__ << " " << res << " " << frame << std::endl;

        if(frame){
            if(mpp_frame_get_info_change(frame)){
                width = mpp_frame_get_width(frame);
                height = mpp_frame_get_height(frame);
                hor_stride = mpp_frame_get_hor_stride(frame);
                ver_stride = mpp_frame_get_ver_stride(frame);
                RK_U32 buf_size = mpp_frame_get_buf_size(frame);

                sdl_init_process();
                
                printf("%p decode_get_frame get info changed found\n", mppCtx);
                printf("%p decoder require buffer w:h [%d:%d] stride [%d:%d] buf_size %d \n",mppCtx, width, height, hor_stride, ver_stride, buf_size);

                res = mpp_buffer_group_get_internal(&group,MPP_BUFFER_TYPE_DRM);
                res = mpp_buffer_group_limit_config(&group, buf_size, 24);
                mppApi->control(mppCtx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);

            }else{
                RK_U32 err_info = mpp_frame_get_errinfo(frame);
                RK_U32 discard = mpp_frame_get_discard(frame);

                if (err_info || discard) {
                    printf("errod %x discard %x",err_info,discard);
                }

                // std::cout << __LINE__ << " ******************************************************************************* " << std::endl;

                avframe->format           = AV_PIX_FMT_NV12;
                avframe->width            = mpp_frame_get_width(frame);
                avframe->height           = mpp_frame_get_height(frame);
                avframe->pts              = mpp_frame_get_pts(frame);

                // cout << " frame width " << avframe->width << endl;
                // cout << " frame height " << avframe->height << endl;
                // cout << " frame hor_stride " << mpp_frame_get_hor_stride(frame) << endl;
                // cout << " frame ver_stride " << mpp_frame_get_ver_stride(frame) << endl;
                // cout << " frame pts " << avframe->pts/(long)1e9 << "-"<< avframe->pts % (long)1e9 << endl;
                // cout << " frame format " << mpp_frame_get_fmt(frame) << endl;
                // cout << " frame size " << mpp_frame_get_buf_size(frame) << endl;

                MppBuffer decodeBuffer = mpp_frame_get_buffer(frame);
                DisplayFrame* display_frame_ = new DisplayFrame(decodeBuffer,data->create_time);
                frame_queue.push(display_frame_);
                // cout << " frame buffer " << decodeBuffer << endl;
                // res = display_in_NV12(decodeBuffer);
                // printf("packet create time [%lu], display end ! [%.3f]\n",data->create_time,(av_gettime() - data->create_time)/1000.0);
            }
            
           
            // mpp_buffer_put(commitBuffer);
            // // mpp_buffer_group_put(group);
            // std::cout << " encoder finished!" << std::endl;
            // display(avframe);

            
        }
        delete data;
        mpp_packet_deinit(&packet);
    }
    return res;
}

void display_(){
    while (1){
        std::shared_ptr<DisplayFrame *> frame = frame_queue.tryPop();
        if(frame){
            DisplayFrame * temp = *(frame.get());
            if(display_in_NV12(temp->buffer) < 0){
                break;
            }
            printf("display_ packet create time [%lu], display end ! [%.3f]\n",temp->create_time,(av_gettime() - temp->create_time)/1000.0);
            delete temp;
        }else{
            printf("display so fast wait 5 ms\n");
            usleep(1000*5);
        }
    }
    
}

void decode_thread(DummySink *sink){
    printf("begin decode thread !\n");
    // usleep(100*5); // 0.5 ms
    while(1){
        if(smart_read_data_and_decode(sink) < 0){
            sink->stop_stream = 1;
            break;
        }
    }
}

void display_thread(){
    while(1){
        if(display() < 0){
            break;
        }
    }
}

void start_thread(DummySink *sink){
    std::thread decode(decode_thread,sink);
    decode.detach();
    std::thread display_t(display_);
    display_t.detach();

}