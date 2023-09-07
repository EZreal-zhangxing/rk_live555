#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <thread>
#include "xf86drm.h"
#include "xf86drmMode.h"
 
#define uint32_t unsigned int 
 
struct framebuffer{
	uint32_t size;
	uint32_t handle;	
	uint32_t fb_id;
	uint32_t *vaddr;	
};
 
static void create_fb(int fd,uint32_t width, uint32_t height, uint32_t color ,struct framebuffer *buf)
{
	struct drm_mode_create_dumb create = {};
 	struct drm_mode_map_dumb map = {};
	uint32_t i;
	uint32_t fb_id;
 
	create.width = width;
	create.height = height;
	create.bpp = 32;
	drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);	//创建显存,返回一个handle
 
	drmModeAddFB(fd, create.width, create.height, 24, 32, create.pitch,create.handle, &fb_id); 
	
	map.handle = create.handle;
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);	//显存绑定fd，并根据handle返回offset
 
	//通过offset找到对应的显存(framebuffer)并映射到用户空间
	uint32_t *vaddr = (uint32_t *)mmap(0, create.size, PROT_READ | PROT_WRITE,MAP_SHARED, fd, map.offset);	
 
	for (i = 0; i < (create.size / 4); i++)
		vaddr[i] = color;
 
	buf->vaddr=vaddr;
	buf->handle=create.handle;
	buf->size=create.size;
	buf->fb_id=fb_id;
 
	return;
}
 
static void release_fb(int fd, struct framebuffer *buf)
{
	struct drm_mode_destroy_dumb destroy = {};
	destroy.handle = buf->handle;
 
	drmModeRmFB(fd, buf->fb_id);
	munmap(buf->vaddr, buf->size);
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}
 

static int terminate;

struct framebuffer buf[2];
static int i = 0;

static void modeset_page_flip_handler(int fd, uint32_t frame,
				    uint32_t sec, uint32_t usec,
				    void *data)
{
	
	uint32_t crtc_id = *(uint32_t *)data;

	i ^= 1;
	drmModePageFlip(fd, crtc_id, buf[i].fb_id,
			DRM_MODE_PAGE_FLIP_EVENT, data);

	// usleep(1000*100);
}

static void sigint_handler(int arg)
{
	terminate = 1;
}
unsigned int color[2] = {0xCDB38B,0xBBFFFF};

int fd;
drmEventContext ev = {};
drmModeConnector *connector;
drmModeRes *resources;
uint32_t conn_id;
uint32_t crtc_id;

void commit_data(){
	for (int j = 0;j < (buf[i^1].size / 4); j++){
		color[i^1] += 1;
		buf[i^1].vaddr[j] = color[i^1];
	}
	drmHandleEvent(fd, &ev);
}

int flip(int argc, char **argv)
{

	/* register CTRL+C terminate interrupt */
	signal(SIGINT, sigint_handler);

	ev.version = DRM_EVENT_CONTEXT_VERSION;
	ev.page_flip_handler = modeset_page_flip_handler;


	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);	//打开card0，card0一般绑定HDMI和LVDS
	resources = drmModeGetResources(fd);	//获取drmModeRes资源,包含fb、crtc、encoder、connector等
	
	crtc_id = resources->crtcs[0];			//获取crtc id
	conn_id = resources->connectors[0];		//获取connector id
 
	connector = drmModeGetConnector(fd, conn_id);	//根据connector_id获取connector资源
	
	create_fb(fd,connector->modes[0].hdisplay,connector->modes[0].vdisplay, 0xCDB38B, &buf[0]);	//创建显存和上色
	create_fb(fd,connector->modes[0].hdisplay,connector->modes[0].vdisplay, 0xBBFFFF, &buf[1]);	
 
	drmModeSetCrtc(fd, crtc_id, buf[0].fb_id,	
			0, 0, &conn_id, 1, &connector->modes[0]);	//初始化和设置crtc，对应显存立即刷新

	drmModePageFlip(fd, crtc_id, buf[0].fb_id,
			DRM_MODE_PAGE_FLIP_EVENT, &crtc_id);

	// while (!terminate) {
		
		
	// 	drmHandleEvent(fd, &ev);
	// 	char temp = getchar();
	// 	printf("you input %c \n",temp);
	// 	if(temp == 'z'){
	// 		terminate = 1;
	// 		break;
	// 	}

	// }
 
	// release_fb(fd, &buf[0]);
	// release_fb(fd, &buf[1]);
 
	// drmModeFreeConnector(connector);
	// drmModeFreeResources(resources);
 
	// close(fd);
 
	return 0;
}

int drm_destroy(){
	release_fb(fd, &buf[0]);
	release_fb(fd, &buf[1]);
 
	drmModeFreeConnector(connector);
	drmModeFreeResources(resources);
 
	close(fd);
 
	return 0;
}

void thread_test(){
	while(!terminate){
		commit_data();
		usleep(1000* (rand() * 1000 % 1000));
	}
	
}

int main(int argc, char **argv)
{
	flip(argc,argv);
	std::thread commit(thread_test);
	commit.join();
	drm_destroy();
	return 0;
	// int fd;
	// struct framebuffer buf[3];
	// drmModeConnector *connector;
	// drmModeRes *resources;
	// uint32_t conn_id;
	// uint32_t crtc_id;
 
 
	// fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);	//打开card0，card0一般绑定HDMI和LVDS
 
	// resources = drmModeGetResources(fd);	//获取drmModeRes资源,包含fb、crtc、encoder、connector等
	
	// crtc_id = resources->crtcs[0];			//获取crtc id
	// conn_id = resources->connectors[0];		//获取connector id
 
	// connector = drmModeGetConnector(fd, conn_id);	//根据connector_id获取connector资源
 
	// printf("hdisplay:%d vdisplay:%d\n",connector->modes[0].hdisplay,connector->modes[0].vdisplay);
 
	// create_fb(fd,connector->modes[0].hdisplay,connector->modes[0].vdisplay, 0xff0000, &buf[0]);	//创建显存和上色
	// create_fb(fd,connector->modes[0].hdisplay,connector->modes[0].vdisplay, 0x00ff00, &buf[1]);	
	// create_fb(fd,connector->modes[0].hdisplay,connector->modes[0].vdisplay, 0x0000ff, &buf[2]);	
 
	// drmModeSetCrtc(fd, crtc_id, buf[0].fb_id,	
	// 		0, 0, &conn_id, 1, &connector->modes[0]);	//初始化和设置crtc，对应显存立即刷新
	// sleep(5);
 
	// drmModeSetCrtc(fd, crtc_id, buf[1].fb_id,
	// 	0, 0, &conn_id, 1, &connector->modes[0]);
	// sleep(5);
 
	// drmModeSetCrtc(fd, crtc_id, buf[2].fb_id,
	// 	0, 0, &conn_id, 1, &connector->modes[0]);
	// sleep(5);
 
	// release_fb(fd, &buf[0]);
	// release_fb(fd, &buf[1]);
	// release_fb(fd, &buf[2]);
 
	// drmModeFreeConnector(connector);
	// drmModeFreeResources(resources);
 
	// close(fd);
 
	// return 0;
}