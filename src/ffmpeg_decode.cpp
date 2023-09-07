#include "ffmpeg_decode.h"
#include<SDL2/SDL.h>
#include<SDL2/SDL_thread.h>
#include<SDL2/SDL_error.h>
#include <thread>
#include<iostream>


#include <signal.h>
#include <fcntl.h>
#include "xf86drm.h"
#include "xf86drmMode.h"
#include <sys/mman.h>
#include <unistd.h>
#include <drm_fourcc.h>

using namespace std;
const AVCodec *codec;
AVFormatContext *avFormatCtx = NULL;
AVCodecContext *codecCtx;
AVFrame *frameYUV,*frameNV12,*frameHD;
AVPacket * packet;
SwsContext * swsCtx;

void hardwave_init(AVCodecContext *codecCtx,const AVCodec * codec){
    //设置硬解码器
    AVHWDeviceType deviceType = AV_HWDEVICE_TYPE_DRM; // 根据架构设置硬解码类型
    // 根据硬解码类型创建硬解码器
    // AVBufferRef * bufferRef = NULL;
    AVBufferRef * bufferRef = av_hwdevice_ctx_alloc(deviceType);
    // 初始化硬解码器
    // int ret = av_hwdevice_ctx_create(&bufferRef,deviceType,NULL,NULL,0);
    int ret = av_hwdevice_ctx_init(bufferRef);
    // 将硬解码器关联到解码器上
    codecCtx->hw_device_ctx = av_buffer_ref(bufferRef);
}
SDL_Window *win = NULL; //SDL窗口
SDL_Renderer *ren = NULL; // 渲染器
SDL_Texture *texture = NULL; // 纹理
SDL_Surface * surface = NULL; //表面
SDL_Rect rect; // 活动矩形
SDL_Event event; // 事件

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
    texture = SDL_CreateTexture(ren,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,width,height);
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


static int fd;
static drmEventContext ev = {};
static drmModeConnector *connector;
static drmModeRes *resources;
uint32_t conn_id;
uint32_t crtc_id;
struct framebuffer{
	uint32_t size;
	uint32_t handle;	
	uint32_t fb_id;
	uint8_t *vaddr;	
};
struct framebuffer buf[2];

static int terminate_signal;
static void sigint_handler(int arg)
{
	terminate_signal = 1;
}


void create_fb(int fd,uint32_t display_width, uint32_t display_height,struct framebuffer *buf)
{
	struct drm_mode_create_dumb create = {};
 	struct drm_mode_map_dumb map = {};
    create.width = hor_stride;
	create.height = ver_stride * 2;
	create.bpp = 8;

    drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);	//创建显存,返回一个handle

    uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	uint32_t fb_id;

    handles[0] = create.handle;
    pitches[0] = hor_stride;
    offsets[0] = 0;

    handles[1] = create.handle;
    pitches[1] = hor_stride;
    offsets[1] = hor_stride * ver_stride;

	// drmModeAddFB(fd, create.width, create.height, 24, 32, create.pitch,create.handle, &fb_id); 
	drmModeAddFB2(fd,display_width,display_height,DRM_FORMAT_NV12,handles,pitches,offsets,&fb_id,0);

	map.handle = create.handle;
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);	//显存绑定fd，并根据handle返回offset
 
	//通过offset找到对应的显存(framebuffer)并映射到用户空间
	uint8_t *vaddr = (uint8_t *)mmap(0, create.size, PROT_READ | PROT_WRITE,MAP_SHARED, fd, map.offset);	

 
	// for (i = 0; i < (create.size / 4); i++)
	// 	vaddr[i] = color;

	buf->vaddr=vaddr;
	buf->fb_id=fb_id;
    buf->size = create.size;
    buf->handle = create.handle;
	return;
}

void release_fb(int fd, struct framebuffer *buf)
{
	struct drm_mode_destroy_dumb destroy = {};
	destroy.handle = buf->handle;
 
	drmModeRmFB(fd, buf->fb_id);
	munmap(buf->vaddr, buf->size);
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}
static int i = 0;
const int perFrameSleepTime = (1 / 60.0) * 1000 * 1000;
static int need_sleep = 0;
static void modeset_page_flip_handler(int fd, uint32_t frame,
				    uint32_t sec, uint32_t usec,
				    void *data){
	drmModePageFlip(fd, crtc_id, buf[i].fb_id,DRM_MODE_PAGE_FLIP_EVENT, data);
    // if(need_sleep > 0){
    //     usleep(need_sleep);
    // }
}

void drm_init(){
    signal(SIGINT, sigint_handler);

	ev.version = DRM_EVENT_CONTEXT_VERSION;
	ev.page_flip_handler = modeset_page_flip_handler;

    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);	//打开card0，card0一般绑定HDMI和LVDS
	resources = drmModeGetResources(fd);	//获取drmModeRes资源,包含fb、crtc、encoder、connector等
	
	crtc_id = resources->crtcs[0];			//获取crtc id
	conn_id = resources->connectors[0];		//获取connector id
 
	connector = drmModeGetConnector(fd, conn_id);	//根据connector_id获取connector资源
}

int drm_display(){
    int display_h = connector->modes[0].hdisplay;
    int display_v = connector->modes[0].vdisplay;

    create_fb(fd,display_h,display_v,buf);
    create_fb(fd,display_h,display_v,buf + 1);

    

    // cout << "copy finished!" << endl;
	drmModeSetCrtc(fd, crtc_id,buf[i].fb_id,0, 0, &conn_id, 1, &connector->modes[0]);	//初始化和设置crtc，对应显存立即刷新
    
    // drmModePageFlip(fd, crtc_id, buf[0].fb_id,DRM_MODE_PAGE_FLIP_EVENT, &crtc_id);

	// while (!terminate) {
	// drmHandleEvent(fd, &ev);
	// }
    // usleep(1/30 * 1000 * 1000);
    drmModePageFlip(fd, crtc_id, buf[i].fb_id,DRM_MODE_PAGE_FLIP_EVENT, &crtc_id);

    // usleep(1/30);
    // while (!terminate_signal) {
	// 	drmHandleEvent(fd, &ev);
	// }
	
	return 0;
}

void drm_destroy(){
    release_fb(fd, &buf[0]);
    release_fb(fd, &buf[1]);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
	close(fd);
}

int write_data_to_buf(AVFrame * aframe){
    i ^= 1;
    uint8_t * buffer_ptr = (uint8_t *)aframe->data[0];
    uint8_t * uv_buffer_ptr = (uint8_t *)aframe->data[1];
    // memset(buf[i].vaddr,0,buf[i].size);
    uint8_t * addr = buf[i].vaddr;
    memmove(addr,buffer_ptr,aframe->linesize[0] * height);
    memmove(addr + aframe->linesize[0] * ver_stride,uv_buffer_ptr,aframe->linesize[1] * height);
    drmHandleEvent(fd, &ev);
    return 0;
}

FILE* write_file;


void init_decoder(){
    int res = 0;
    // write_file = fopen("./receive.h264","wb");
    // codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    codec = avcodec_find_decoder_by_name("h264_rkmpp");
    // codec = avcodec_find_decoder_by_name("h264");
    if(!codec){
        cout << __LINE__ << " codec not find " << endl;
    }
    cout << " find codec name " << codec->name << endl;
    codecCtx = avcodec_alloc_context3(codec);
    if(!codecCtx){
        cout << __LINE__ << " codecCtx not find " << endl;
    }
    hardwave_init(codecCtx,codec);
    res = avcodec_open2(codecCtx,codec,NULL);
    // 分配内存空间，此处并不会分配buffer的空间
    frameYUV = av_frame_alloc();
    frameNV12 = av_frame_alloc();
    frameHD = av_frame_alloc();
    // 分配buffer内存空间
    int frameYUVsize = av_image_alloc(frameYUV->data,frameYUV->linesize,
        width,height,AV_PIX_FMT_YUV420P,1);
    int frameNV12size = av_image_alloc(frameNV12->data,frameNV12->linesize,
        width,height,AV_PIX_FMT_NV12,1);
    int frameHDsize = av_image_alloc(frameHD->data,frameHD->linesize,
        width,height,AV_PIX_FMT_NV12,1);
    swsCtx = sws_getContext(width,height,
        AV_PIX_FMT_YUV420P,
        width,height,
        // 1920,1080,
        AV_PIX_FMT_NV12,
        SWS_BICUBIC,NULL,NULL,NULL);
    packet = (AVPacket *)av_malloc(sizeof(AVPacket));
    packet = av_packet_alloc();
    
    // res = av_packet_make_writable(packet);
    cout << __LINE__ << " " << res << endl;
    // sdl_init_process();
    drm_init();
    drm_display();

    cout << " init finished! " << endl;
}

void destroy(){
    cout << "释放资源 " << endl;
    if(packet){
        // av_packet_unref(packet);
        av_packet_free(&packet);
        packet = NULL;
        cout << "av_packet_free(packet)" << endl;
    }
    if(frameYUV){
        av_frame_free(&frameYUV);
        frameYUV = 0;
        cout << "av_frame_free(frameYUV)" << endl;
    }
    if(frameHD){
        av_frame_free(&frameHD);
        frameHD = 0;
        cout << "av_frame_free(frameHD)" << endl;
    }
    cout << __LINE__ << " " << frameNV12 << endl;
    if(frameNV12){
        av_frame_free(&frameNV12);
        frameNV12 = 0;
        cout << "av_frame_free(frameNV12)" << endl;
    }
    if(codecCtx && codecCtx->hw_device_ctx){
        av_buffer_unref(&codecCtx->hw_device_ctx);
        cout << "av_buffer_unref(&codecCtx->hw_device_ctx)" << endl;
    }
    if(codecCtx){
        cout << "avcodec_close(codecCtx);" << endl;
        avcodec_close(codecCtx);
        codecCtx = 0;
    }
    // if(texture){
    //     SDL_DestroyTexture(texture);
    //     cout << "SDL_DestroyTexture(texture)" << endl;
    // }
    // SDL_free(&rect);
    // cout << "SDL_free(&rect)" << endl;
    // SDL_free(&event);
    // cout << "SDL_free(&event);" << endl;
    // if(ren){
    //     SDL_DestroyRenderer(ren);
    //     cout << "SDL_DestroyRenderer(ren)" << endl;
    // }
    // if(win){
    //     SDL_DestroyWindow(win);
    //     cout << "SDL_DestroyWindow(win)" << endl;
    // }
    // SDL_Quit();
}
static int iframeNum =0;
void print_data(RTSPReceiveData &d){
    unsigned char * ptr = (unsigned char *)d.dataPtr;
    int lines = 0;
    if((ptr[4] & 0x000000ff) == 101){
        printf("\n*************************it's I frame[%d]**********************************\n",++iframeNum);
    }
    for(int i=0;i<d.size;i++){
        printf("%2x ",ptr[i]);
        if(i> 0 && i%30 == 0){
            lines++;
            printf("\n");
        }
        if(lines > 1){
            break;
        }
    }
    printf("\n");
}
#define MAX_PACKET_NUM 8
struct timeval preTime;
void * packet_concat[MAX_PACKET_NUM] = {};
unsigned packet_size[MAX_PACKET_NUM] = {};
unsigned packet_len = 0;
unsigned all_size = 0;
void send_data_concat(RTSPReceiveData &data){
    int res = 0;
    if(packet_len == 0){
        preTime = data.presentationTime;
        packet_concat[packet_len] = data.dataPtr;
        packet_size[packet_len] = data.size; 
        all_size += data.size;
        packet_len ++;
    }else{
        if(preTime.tv_sec == data.presentationTime.tv_sec 
            && preTime.tv_usec == data.presentationTime.tv_usec){
            // 相同的处理时间
            packet_concat[packet_len] = data.dataPtr;
            packet_size[packet_len] = data.size; 
            all_size += data.size;
            packet_len++;
        }else{
            av_new_packet(packet,all_size);
            unsigned offset = 0;
            for(int i=0;i<packet_len;i++){
                memcpy(packet->data + offset,packet_concat[i],packet_size[i]); 
                free(packet_concat[i]);
                packet->size += packet_size[i];
                offset += packet_size[i];
            }
            cout << " packet " << packet_len << " and size " << all_size << endl;
            fwrite(packet->data,all_size,1,write_file);

            packet->duration = (preTime.tv_sec * 1000000 + preTime.tv_usec) * 1000;
            res = avcodec_send_packet(codecCtx,packet);
            res = avcodec_receive_frame(codecCtx,frameNV12);
            packet_len = 0;
            all_size = 0;
            memset(packet_size,0,MAX_PACKET_NUM);
            memset(packet_concat,0,MAX_PACKET_NUM);

            preTime = data.presentationTime;
            packet_concat[packet_len] = data.dataPtr;
            packet_size[packet_len] = data.size; 
            all_size += data.size;
            packet_len ++;
            cout << __LINE__ << " " << res << endl;
            if(res < 0){
                return;
            }
            av_hwframe_transfer_data(frameHD, frameNV12, 0);
            sws_scale(swsCtx,(const uint8_t* const*)frameHD->data,frameHD->linesize,
                0,height,frameYUV->data,frameYUV->linesize);
            rect.x = 0;
            rect.y = 0;
            rect.w = width;
            rect.h = height;
            SDL_UpdateYUVTexture(texture,&rect,
                frameYUV->data[0],frameYUV->linesize[0],
                frameYUV->data[1],frameYUV->linesize[1],
                frameYUV->data[2],frameYUV->linesize[2]);

            // 清空渲染器
            SDL_RenderClear(ren);
            // 将纹理拷贝至渲染器
            SDL_RenderCopy(ren, texture,NULL, &rect);
            SDL_RenderPresent(ren);
            // 释放包数据
            av_packet_unref(packet);
            // SDL事件处理
            SDL_PollEvent(&event);
            
            int ext = event_process(&event);
            // ext = 1;
            if(ext){
                fclose(write_file);
                destroy();
                return;
            }
        }
    }
}

void send_data2(RTSPReceiveData &data){
    print_data(data);
    
    if(all_size <= 500){
        fwrite(data.dataPtr,data.size,1,write_file);
    }
    if(all_size > 500){
        fclose(write_file);
    }
    all_size +=1;
}

void send_data(RTSPReceiveData &data){
    print_data(data);
    int res = 0, init_decode_sps = 0;   
    fwrite(data.dataPtr,data.size,1,write_file);
    // cout << __LINE__ << "data size " << data.size << endl;
    // if(!init_decode_sps){
    //     codecCtx->extradata = (uint8_t *)av_malloc(40);
    //     memcpy(codecCtx->extradata,data.dataPtr, 40);
    //     codecCtx->extradata_size = 40;
    //     init_decode_sps = 1;
    // }
    av_new_packet(packet,data.size*2);
    // cout << __LINE__ << " " << res << endl;
    // memcpy(packet->data,data.dataPtr,data.size);
    packet->data = (uint8_t *)data.dataPtr;
    packet->size = data.size;
    // packet->time_base = (AVRational){1,60};
    // packet->duration = (data.presentationTime.tv_sec * 1000000 + data.presentationTime.tv_usec) * 1000;
    packet->pts = (data.presentationTime.tv_sec * 1000000 + data.presentationTime.tv_usec) * 1000;
    res = avcodec_send_packet(codecCtx,packet);
    // cout << __LINE__ << " " << res << endl;
    // res = avcodec_receive_frame(codecCtx,frameNV12);
    res = avcodec_receive_frame(codecCtx,frameYUV);
    cout << __LINE__ << " " << res << endl;
    
    if(res < 0){
        av_packet_free(&packet);
        return;
    }
    
    // av_hwframe_transfer_data(frameHD, frameNV12, 0);
    // sws_scale(swsCtx,(const uint8_t* const*)frameHD->data,frameHD->linesize,
    //     0,height,frameYUV->data,frameYUV->linesize);
    // EAGAIN
    // cout << __LINE__ << " " << res << endl;
    rect.x = 0;
    rect.y = 0;
    rect.w = width;
    rect.h = height;
    SDL_UpdateYUVTexture(texture,&rect,
        frameYUV->data[0],frameYUV->linesize[0],
        frameYUV->data[1],frameYUV->linesize[1],
        frameYUV->data[2],frameYUV->linesize[2]);

    // 清空渲染器
    SDL_RenderClear(ren);
    // 将纹理拷贝至渲染器
    SDL_RenderCopy(ren, texture,NULL, &rect);
    SDL_RenderPresent(ren);
    // 释放包数据
    // av_packet_unref(packet);
    av_packet_free(&packet);
     // SDL事件处理
    SDL_PollEvent(&event);
    
    int ext = event_process(&event);
    // ext = 1;
    if(ext){
        fclose(write_file);
        destroy();
        return;
    }
}

void print_packet_time_info(RTSPReceiveData *data){
    int64_t now_time = av_gettime();
    data->decode_time = now_time;
    // if(data->create_time != 0){
        // printf("packet present time [%lu-%lu] bytes [%x]\n",data->presentationTime.tv_sec,data->presentationTime.tv_usec,data->dataPtr[4]);
        printf("packet create time [%ld],packet size [%5lu], arrive time delay [%3.3f],send to memory delay [%3.3f],decode pop out delay[%3.3f] display delay[%3.3f]\n",
            data->create_time,
            data->size,
            (data->arrive_time - data->create_time)/1000.0,
            (data->send_memory_time-data->create_time)/1000.0,
            (data->decode_time - data->create_time)/1000.0,
            (data->display_time - data->create_time)/1000.0);
    // }
    
}
int find_first_i_frame = 0;
int read_data_and_decode(DummySink *sink){
    RTSPReceiveData *data = sink->pop_data(0);
    int res = 0, init_decode_sps = 0;  
    if(data != NULL){
        // print_data(*data);
         
        if(!find_first_i_frame && ((data->dataPtr[4] & 0x000000ff) != 6)){
            delete data;
            return 1;
        }
        find_first_i_frame = 1;
        // cout << " begin custom data " <<  data->dataPtr << " " << data->size << endl;
        // fwrite(data->dataPtr,data->size,1,write_file);
        av_new_packet(packet,data->size);
        packet->data = (uint8_t *)data->dataPtr;
        packet->size = data->size;
        packet->pts = (data->presentationTime.tv_sec * 1000000 + data->presentationTime.tv_usec) * 1000;
        res = avcodec_send_packet(codecCtx,packet);
        // cout << __LINE__ << " " << res << endl;
        res = avcodec_receive_frame(codecCtx,frameNV12);
        data->decode_time = av_gettime();
        // res = avcodec_receive_frame(codecCtx,frameYUV);
        // cout << __LINE__ << " " << res << endl;
        av_packet_unref(packet);
        if(res < 0){
            return 0;
        }
        // cout << __LINE__ << " " << res << endl;
        av_hwframe_transfer_data(frameHD, frameNV12, 0);
        // cout << __LINE__ << " " << res << endl;
        write_data_to_buf(frameHD);
        
        data->display_time = av_gettime();
        // sws_scale(swsCtx,(const uint8_t* const*)frameYUV->data,frameYUV->linesize,
        //     0,height,frameNV12->data,frameNV12->linesize);
        // write_data_to_buf(frameNV12);
        // rect.x = 0;
        // rect.y = 0;
        // rect.w = width;
        // rect.h = height;
        // SDL_UpdateYUVTexture(texture,&rect,
        //     frameYUV->data[0],frameYUV->linesize[0],
        //     frameYUV->data[1],frameYUV->linesize[1],
        //     frameYUV->data[2],frameYUV->linesize[2]);

        // // 清空渲染器
        // SDL_RenderClear(ren);
        // // 将纹理拷贝至渲染器
        // SDL_RenderCopy(ren, texture,NULL, &rect);
        // SDL_RenderPresent(ren);
        // // 释放包数据
        // av_packet_unref(packet);
        // free(data);
        // // SDL事件处理
        // SDL_PollEvent(&event);
        
        // int ext = event_process(&event);
        // // ext = 1;
        // if(ext){
        //     fclose(write_file);
        //     destroy();
        //     return 0;
        // }
        // print_packet_time_info(data);
        int cal_need_sleep = perFrameSleepTime - (av_gettime() - data->create_time);
        // printf("display_ packet create time [%lu], display end ! [%lu] need_sleep [%d]\n"
        //     ,data->create_time,(av_gettime() - data->create_time),cal_need_sleep);
        if(cal_need_sleep < perFrameSleepTime && cal_need_sleep > 5000){
            need_sleep = cal_need_sleep;
        }else{
            need_sleep = 0;
        }
        // av_frame_unref(frameNV12);
        // av_frame_unref(frameHD);
        delete data;
    }
    
    if(terminate_signal){
        // fclose(write_file);
        destroy();
        cout << "destroy avcode ==== " << endl;
        drm_destroy();
        cout << "destroy drm ==== " << endl;
        return -1;
    }

    return 0;
}

int directly_read_data_and_decode(RTSPReceiveData * data){
    int res = 0, init_decode_sps = 0;   
    // cout << " begin custom data " <<  data->dataPtr << " " << data->size << endl;
    // fwrite(data->dataPtr,data->size,1,write_file);
    av_new_packet(packet,data->size);
    packet->data = (uint8_t *)data->dataPtr;
    packet->size = data->size;
    packet->pts = (data->presentationTime.tv_sec * 1000000 + data->presentationTime.tv_usec) * 1000;
    res = avcodec_send_packet(codecCtx,packet);
    res = avcodec_receive_frame(codecCtx,frameNV12);
    data->decode_time = av_gettime();
    // res = avcodec_receive_frame(codecCtx,frameYUV);
    // cout << __LINE__ << " " << res << endl;
    
    if(res < 0){
        return 0;
    }
    av_hwframe_transfer_data(frameHD, frameNV12, 0);
    write_data_to_buf(frameHD);

    data->display_time = av_gettime();
    print_packet_time_info(data);
    av_packet_free(&packet);
    if(terminate_signal){
        // fclose(write_file);
        destroy();
        drm_destroy();
        return -1;
    }

    return 0;
}

void decode_thread(DummySink *sink){
    // usleep(1000*500); // 20 ms
    printf("begin decode thread !\n");
    init_decoder();
    while(1){
        // cout << " queue size " << sink->receive_queue.size() << endl;
        if(read_data_and_decode(sink) < 0){
            sink->stop_stream = 1;
            return;
        }

    }
}

void start_thread(DummySink *sink){
    thread decode(decode_thread,sink);
    decode.detach();
}