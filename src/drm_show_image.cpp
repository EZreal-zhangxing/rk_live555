#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "xf86drm.h"
#include "xf86drmMode.h"

struct framebuffer{
	uint32_t size;
	uint32_t handle;	
	uint32_t fb_id;
	uint32_t *vaddr;	
};
 
static void create_fb(int fd,uint32_t width, uint32_t height ,struct framebuffer *buf)
{
	struct drm_mode_create_dumb create = {};
 	struct drm_mode_map_dumb map = {};
	uint32_t i;
	uint32_t fb_id;
 
	create.width = width;
	create.height = height;
	create.bpp = 32;
	drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);	//创建显存,返回一个handle
	
	std::cout << " create size is " << create.size << std::endl;
	drmModeAddFB(fd, create.width, create.height, 24, 32, create.pitch,create.handle, &fb_id); 

	map.handle = create.handle;
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);	//显存绑定fd，并根据handle返回offset
 
	//通过offset找到对应的显存(framebuffer)并映射到用户空间
	uint32_t *vaddr = (uint32_t *)mmap(0, create.size, PROT_READ | PROT_WRITE,MAP_SHARED, fd, map.offset);	
 
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

int main(int argc, char * argv[]){
    int fd;
	struct framebuffer buf;
	drmModeConnector *connector;
	drmModeRes *resources;
	uint32_t conn_id;
	uint32_t crtc_id;

    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    resources = drmModeGetResources(fd);	//获取drmModeRes资源,包含fb、crtc、encoder、connector等
	
	crtc_id = resources->crtcs[0];			//获取crtc id
	conn_id = resources->connectors[0];		//获取connector id
 
	connector = drmModeGetConnector(fd, conn_id);	//根据connector_id获取connector资源
 
	printf("hdisplay:%d vdisplay:%d\n",connector->modes[0].hdisplay,connector->modes[0].vdisplay);
 
	
    

    cv::Mat image_mat = cv::imread("/home/firefly/mpp/hdwave_stream/src/image.png",1);
    // std::cout << cv::format(image_mat,cv::Formatter::FMT_PYTHON) << std::endl;
    int display_h = connector->modes[0].hdisplay;
    int display_v = connector->modes[0].vdisplay;
    // cv::resize(image_mat,image_mat,cv::Size(display_h,display_v),0,0,cv::INTER_LINEAR);
	cv::resize(image_mat,image_mat,cv::Size(display_h,display_v),0,0,cv::INTER_LINEAR);

    create_fb(fd,display_h,display_v,&buf);	//创建显存和上色
    
    std::cout << image_mat.size() << std::endl;
    int index = 0;
    for(int i=0;i<display_v;i++){
        for(int j=0;j<display_h;j++){
            uchar *ptr = image_mat.ptr<uchar>(i , j) ;
            // printf("%d %d %d %x \n",ptr[0],ptr[1],ptr[2],((ptr[2]<< 16) + (ptr[1] << 8) +ptr[0]));
            buf.vaddr[index++] = (ptr[2]<< 16) + (ptr[1] << 8) +ptr[0];
        }
    }
    while(1)
	    drmModeSetCrtc(fd, crtc_id, buf.fb_id,0, 0, &conn_id, 1, &connector->modes[0]);	//初始化和设置crtc，对应显存立即刷新
 
	release_fb(fd, &buf);
 
	drmModeFreeConnector(connector);
	drmModeFreeResources(resources);
 
	close(fd);
 
    return 0;
}