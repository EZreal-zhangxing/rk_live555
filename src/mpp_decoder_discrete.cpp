#include "mpp_decoder_discrete.h"
#include "ffmpeg_head.h"
#include <thread>
#include "safely_queue.h"
#include<SDL2/SDL.h>


#include <signal.h>
#include <fcntl.h>
#include "xf86drm.h"
#include "xf86drmMode.h"
#include <sys/mman.h>
#include <unistd.h>
#include <drm_fourcc.h>

using namespace std;
MppApi *mppApi;
MppCtx mppCtx;
MppBufferGroup group;
MppDecCfg cfg;

// MppTask task;
MppBuffer commitBuffer;
MppPacket packet;
MppFrame frame;
static unsigned frame_buffer_size = 0;
static unsigned packet_size = 0;


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
static struct framebuffer buf[2];

static int terminate_signal;
static void sigint_handler(int arg)
{
	terminate_signal = 1;
}

SDL_Window *win = NULL; //SDL窗口
SDL_Renderer *ren = NULL; // 渲染器
SDL_Texture *texture = NULL; // 纹理
SDL_Surface * surface = NULL; //表面
SDL_Rect rect; // 活动矩形
SDL_Event event; // 事件
void destroy(){
    if (frame) {
        mpp_frame_deinit(&frame);
        printf("deinit frame \n");
        frame = NULL;
    }
    if (group) {
        mpp_buffer_group_put(group);
        printf("clean mpp buffer group \n");
        group = NULL;
    }
    if (mppCtx) {
        mpp_destroy(mppCtx);
        printf("release mpp \n");
        mppCtx = NULL;
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
const long perFrameSleepTime = (1 / 60.0) * 1000 * 1000;
static long need_sleep = 0;
static void modeset_page_flip_handler(int fd, uint32_t frame,
				    uint32_t sec, uint32_t usec,
				    void *data){
    uint32_t crtc_id = *(uint32_t *)data;
	int res = drmModePageFlip(fd, crtc_id, buf[i].fb_id,DRM_MODE_PAGE_FLIP_EVENT, data);

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
    // create_fb(fd,display_h,display_v,buf + 2);

    // cout << "copy finished!" << endl;
	drmModeSetCrtc(fd, crtc_id,buf[0].fb_id,0, 0, &conn_id, 1, &connector->modes[0]);	//初始化和设置crtc，对应显存立即刷新
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

void save_yuv_data(MppBuffer buffer);

int write_data_to_buf(MppBuffer buffer){
    i ^= 1;
    uint8_t * buffer_ptr = (uint8_t *)mpp_buffer_get_ptr(buffer);
    // memset(buf[i].vaddr,0,buf[i].size);
    uint8_t * addr = buf[i].vaddr;
    memmove(addr,buffer_ptr,mpp_buffer_get_size(buffer));
    // mpp_buffer_put(buffer); // 清空Packet的数据
    drmHandleEvent(fd, &ev);
    return 0;
}

// int c = 0;
// FILE* w = fopen("./file.yuv","wb");
// void save_yuv_data(MppBuffer buffer){
//     char name[20];
//     sprintf(name,"./file_%d.yuv",c);
//     FILE* w = fopen(name,"wb");
//     int size = mpp_buffer_get_size(buffer);
//     uint8_t * start = (uint8_t *)mpp_buffer_get_ptr(buffer);// YUV420P-NV12格式的数据
//     // uint8_t * remake = (uint8_t *)malloc(width * height * 3 / 2);
//     // uint8_t * uv_ptr = remake + width*height;
//     // int row ,read_size = 0;

//     // for(row = 0;row<height;row++){
//     //     memcpy(remake + row * width,start + read_size, width);
//     //     read_size += hor_stride;
//     // }
//     // read_size = hor_stride * ver_stride;

//     // for(row = 0;row<height/2;row++){
//     //     memcpy(uv_ptr + row * width,start + read_size, width);
//     //     read_size += hor_stride;
//     // }
//     fwrite(start,width * height * 2,1,w);
//     c++;
// }

MPP_RET init_decoder(){
    MPP_RET res = MPP_OK;
    RK_U32 need_split = 0;
    RK_U32 enable_fast = 1;
    res = mpp_buffer_group_get_internal(&group,MPP_BUFFER_TYPE_DRM);
    
    res = mpp_create(&mppCtx,&mppApi);
    mppApi->control(mppCtx,MPP_DEC_SET_PARSER_SPLIT_MODE,&need_split);

    mppApi->control(mppCtx,MPP_DEC_SET_ENABLE_FAST_PLAY,&enable_fast);
    MppFrameFormat out = MPP_FMT_YUV420P;
    
    res = mpp_init(mppCtx,MPP_CTX_DEC,MPP_VIDEO_CodingAVC);
    
    res = mppApi->control(mppCtx,MPP_DEC_SET_OUTPUT_FORMAT,&out);

    res = mppApi->control(mppCtx,MPP_DEC_SET_EXT_BUF_GROUP,group);
    int immediate_out = 1;
    res = mppApi->control(mppCtx, MPP_DEC_SET_IMMEDIATE_OUT, &immediate_out);

    res = mppApi->control(mppCtx,MPP_DEC_SET_DISABLE_ERROR,NULL);
    
    mpp_dec_cfg_init(&cfg);
    res = mppApi->control(mppCtx,MPP_DEC_GET_CFG,cfg);
    
    // mpp_dec_cfg_set_u32(cfg, "base:split_parse", need_split);

    res = mppApi->control(mppCtx,MPP_DEC_SET_CFG,cfg);
    // sdl_init_process();
    
    printf("************************************* \n");
    printf("*     init mpp decode finished !    * \n");
    printf("************************************* \n");
    return res;
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
        // printf("packet present time [%lu-%lu] bytes [%x]\n",data->presentationTime.tv_sec,data->presentationTime.tv_usec,data->dataPtr[4]);
        printf("packet create time [%ld],packet size [%5lu], arrive time delay [%3.3f],send to memory delay [%3.3f],decode pop out delay[%3.3f] display delay[%3.3f] need sleep[%ld]\n",
            data->create_time,
            data->size,
            (data->arrive_time - data->create_time)/1000.0,
            (data->send_memory_time-data->create_time)/1000.0,
            (data->decode_time - data->create_time)/1000.0,
            (data->display_time - data->create_time)/1000.0,
            (need_sleep > 0 && need_sleep < perFrameSleepTime) ? need_sleep : 0);
    // }
    
}

void print_data(RTSPReceiveData &data){
    unsigned char * ptr = (unsigned char *)data.dataPtr;
    int lines = 0;
    for(int i=0;i<data.size;i++){
        printf("%2x ",ptr[i]);
        if(i> 0 && i%30 == 0){
            lines++;
            printf("\n");
        }
        if(lines > 5){
            break;
        }
    }
    printf("\n");
}

int find_first_i_frame = 0;
int is_init_drm = 0;

/**
 * 该方法顺序执行数据读取，送入解码，然后展示
*/
int read_data_and_decode(RTSPReceiveData * &data){
    int res = 0;
    // print_data(*data);
    
    // printf(" queue lens [%lu] \n",sink->receive_queue.size());
        // 0110 当前数据不是i帧 并且还没送入i帧给解码器
    // printf(" byte 4 %2x & ff = %d \n",data->dataPtr[4],(data->dataPtr[4] & 0x000000ff));
    if(!find_first_i_frame && ((data->dataPtr[4] & 0x000000ff) != 6)){
        delete data;
        return 1;
    }
    find_first_i_frame = 1;
    RK_S64 pts = (data->presentationTime.tv_sec * 1000000 + data->presentationTime.tv_usec) * 1000;

    // mpp_packet_init_with_buffer(&packet,data->dataPtr);
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

            // RK_U32 buf_size = 0;
            if(!is_init_drm){
                drm_init();
                drm_display();
                is_init_drm = 1;
            }else{
                drm_destroy();
                drm_init();
                drm_display();
            }
            printf("%p decode_get_frame get info changed found\n", mppCtx);
            printf("%p decoder require buffer w:h [%d:%d] stride [%d:%d] buf_size %d \n",mppCtx, width, height, hor_stride, ver_stride, buf_size);

            res = mpp_buffer_group_get_external(&group,MPP_BUFFER_TYPE_DRM);
            // res = mpp_buffer_group_limit_config(&group, buf_size, 24);
            mppApi->control(mppCtx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);

        }else{
            RK_U32 err_info = mpp_frame_get_errinfo(frame);
            RK_U32 discard = mpp_frame_get_discard(frame);

            if (err_info || discard) {
                printf("errod %x discard %x \n",err_info,discard);
            }
            MppBuffer decodeBuffer = mpp_frame_get_buffer(frame);
            // cout << " frame buffer " << decodeBuffer << endl;
            // res = display_in_NV12(decodeBuffer);
            write_data_to_buf(decodeBuffer);
            
            data->display_time = av_gettime();
            // int need_sleep = perFrameSleepTime - (data->display_time - data->create_time);
            // printf("need_sleep %d\n",need_sleep);
            // // usleep(need_sleep);
        }
        
    }
    
    print_packet_time_info(data);
    delete data;
    mpp_packet_deinit(&packet);
    if(terminate_signal){
        destroy();
        drm_destroy();
        return -1;
    }
    return res;
}


class DisplayFrame{
public:
    DisplayFrame() = default;
    DisplayFrame(MppBuffer mppbuffer,int64_t create):buffer(mppbuffer),create_time(create) {}
    ~DisplayFrame(){
        // mpp_buffer_put(buffer);
    }
public:
    MppBuffer buffer;
    int64_t create_time;
    int64_t decode_time;
    int64_t display_time;

};
ConcurrenceQueue<DisplayFrame *> frame_queue;

/**
 * 该方法异步读取队列拿取数据包
 * 送入解码器，将解码后的包送入队列，等待展示线程读取
*/
int framecount =0;
int smart_read_data_and_decode(DummySink *sink){
    int res = 0;
    RTSPReceiveData *data = sink->pop_data(0);
    if(data != NULL){
        // print_data(*data);
        // framecount ++;
        // data->display_time = data->create_time;
        // print_packet_time_info(data);
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
        // if(!data->is_key_frame){
        //     memmove(data->dataPtr + 40,data->dataPtr + 64,data->size - 64);
        //     data->size -= 24;
        // }
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
                if(!is_init_drm){
                    drm_init();
                    drm_display();
                    is_init_drm = 1;
                }else{
                    drm_destroy();
                    drm_init();
                    drm_display();
                }
                printf("%p decode_get_frame get info changed found\n", mppCtx);
                printf("%p decoder require buffer w:h [%d:%d] stride [%d:%d] buf_size %d \n",mppCtx, width, height, hor_stride, ver_stride, buf_size);

                // res = mpp_buffer_group_get_internal(&group,MPP_BUFFER_TYPE_DRM);
                // res = mpp_buffer_group_limit_config(&group, buf_size, 24);
                mppApi->control(mppCtx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);

            }else{
                RK_U32 err_info = mpp_frame_get_errinfo(frame);
                RK_U32 discard = mpp_frame_get_discard(frame);

                if (err_info || discard) {
                    printf("errod %x discard %x",err_info,discard);
                }
                MppBuffer decodeBuffer = mpp_frame_get_buffer(frame);
                // int64_t presentTime = data->presentationTime.tv_sec * 1000000 + data->presentationTime.tv_usec;
                data->decode_time = av_gettime();
                DisplayFrame* display_frame_ = new DisplayFrame(decodeBuffer,data->arrive_time);
                frame_queue.push(display_frame_);
                // framecount ++;
                
                long decodeDelay = perFrameSleepTime - (av_gettime() - data->arrive_time);
                if(decodeDelay > 0 && decodeDelay < perFrameSleepTime){
                    // printf("pakcet create time [%lu] decode delay %ld\n",data->create_time,decodeDelay);
                    usleep(decodeDelay);
                }
            }

            mpp_frame_deinit(&frame);
        }
        
        // print_packet_time_info(data);
        delete data;

        mpp_packet_deinit(&packet);
    }
    
    return 0;
}


/**
 * 该方法异步读取队列拿取数据包
 * 送入解码器，并直接显示
*/

int read_data_and_decode_show_directly(DummySink *sink){
    int res = 0;
    RTSPReceiveData *data = sink->pop_data(0);
    if(data != NULL){
        framecount ++;
        // print_data(*data);
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
        // if(res < 0){
        //     return res;
        // }
        
        // int timeout = 0;
        // mppApi->control(mppCtx, MPP_SET_OUTPUT_TIMEOUT, (MppParam)&timeout);
        res = mppApi->decode_get_frame(mppCtx,&frame);

        if(frame){
            if(mpp_frame_get_info_change(frame)){
                width = mpp_frame_get_width(frame);
                height = mpp_frame_get_height(frame);
                hor_stride = mpp_frame_get_hor_stride(frame);
                ver_stride = mpp_frame_get_ver_stride(frame);
                RK_U32 buf_size = mpp_frame_get_buf_size(frame);
                if(!is_init_drm){
                    drm_init();
                    drm_display();
                    is_init_drm = 1;
                }else{
                    drm_destroy();
                    drm_init();
                    drm_display();
                }
                printf("%p decode_get_frame get info changed found\n", mppCtx);
                printf("%p decoder require buffer w:h [%d:%d] stride [%d:%d] buf_size %d \n",mppCtx, width, height, hor_stride, ver_stride, buf_size);

                res = mpp_buffer_group_get_internal(&group,MPP_BUFFER_TYPE_DRM);
                // res = mpp_buffer_group_limit_config(&group, buf_size, 24);
                mppApi->control(mppCtx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);

            }else{
                RK_U32 err_info = mpp_frame_get_errinfo(frame);
                RK_U32 discard = mpp_frame_get_discard(frame);

                if (err_info || discard) {
                    printf("errod %x discard %x \n",err_info,discard);
                }
                MppBuffer decodeBuffer = mpp_frame_get_buffer(frame);
                write_data_to_buf(decodeBuffer);
                // c++;
                // if(c < 10){
                //     save_yuv_data(decodeBuffer);
                // }else{
                //     fclose(w);
                //     terminate_signal = 1;
                // }
                
                data->display_time = av_gettime();
                need_sleep = perFrameSleepTime - (data->display_time - data->create_time);
                if(need_sleep > 0 && need_sleep < perFrameSleepTime){
                    // printf("pakcet create time [%lu] decode delay %ld\n",data->create_time,need_sleep);
                    usleep(need_sleep);
                }
            }

            mpp_frame_deinit(&frame);
        }

        print_packet_time_info(data);
        delete data;
        mpp_packet_deinit(&packet);
        
    }else{
        usleep(1 * 1000);
    }
    return 0;
}

static int encode_close = 0;

void display_(){
    while (!encode_close){
        std::shared_ptr<DisplayFrame *> frame = frame_queue.tryPop();
        if(frame){
            DisplayFrame * temp = *(frame.get());
            write_data_to_buf(temp->buffer);
            long display_delay = (av_gettime() - temp->create_time);
            need_sleep = perFrameSleepTime - display_delay;
            if(need_sleep > 1000 && need_sleep < perFrameSleepTime){
                printf("packet create time [%ld], display time [%6.3f] ms need sleep [%ld]\n",temp->create_time,display_delay / 1000.0,need_sleep);
                usleep(need_sleep);
            }
            delete temp;
        }
    }

    printf("kill display thread \n");
}

void decode_thread(DummySink *sink){
    printf("begin decode thread !\n");
    init_decoder();
    encode_close = 0;
    while(!terminate_signal){
        // read_data_and_decode_show_directly(sink);
        smart_read_data_and_decode(sink);
    }
    encode_close = 1;
    // delete &frame_queue;
    drm_destroy();
    printf("drm destroy \n");
    destroy();
    printf("mpp destroy \n");
    printf("kill decode thread \n");
    // fclose(w);
    sink->stop_stream = 1;
    usleep(1000 *5);
}

void start_thread(DummySink *sink){
    std::thread decode(decode_thread,sink);
    decode.detach();
    std::thread display_t(display_);
    display_t.detach();

    /**
     * TODO:线程安全关闭
    */
}