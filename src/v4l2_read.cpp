#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
/**
 * multiplane 自定义结构体
 * mmpaddr:array of plans which map into user kernal memory address
 * plans:array of plans 
 * 每个plans映射一块用户空间的内存区域，因此对于multiplans的捕获方式需要定义成数组。
 * 
*/

struct image_buffer_with_plane
{
	unsigned char ** mmpaddr;
	struct v4l2_plane * plans;
};


int main(int argc, char**argv)
{
	if(argc != 2)
	{
		printf("%s </dev/video0,1...>\n", argv[0]);
		return -1;
	}
	//1.打开摄像头设备
	int fd = open(argv[1], O_RDWR);
	if(fd < 0)
	{
		perror("打开设备失败");
		return -1;
	}
	//2.设置摄像头采集格式
	struct v4l2_format vfmt;
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;	//选择视频抓取
	vfmt.fmt.pix.width = 1920;//设置宽，不能随意设置
	vfmt.fmt.pix.height = 1080;//设置高
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
	printf("width = %d\n", vfmt.fmt.pix.height);
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
	char file_name[20];
	char * file_first = "image_";
	char * hz = ".jpg";
	for(int n=0;n<10;n++){
		memset(file_name,0,20);
		ret = ioctl(fd, VIDIOC_DQBUF, &readbuffer);//从缓冲队列获取一帧数据（出队列）
		//出队列后得到缓存的索引index,得到对应缓存映射的地址mmpaddr[readbuffer.index]
		if(ret < 0)
			perror("获取数据失败");
		char temp[2];
		temp[0] = (char)(n+48);
		temp[1] = '\0';
		strcat(file_name,file_first);
		strcat(file_name,temp);
		strcat(file_name,hz);
		strcat(file_name,"\0");
		printf("write file_name %s \n",file_name);
		FILE *file = fopen(file_name, "w+");//建立文件用于保存一帧数据
		for(int x=0;x<image_plans;x++){
			printf("readbuffer index %d,bytesused %d \n",readbuffer.index,readbuffer.m.planes[x].bytesused);
			fwrite(bufferPlanes[readbuffer.index].mmpaddr[x], readbuffer.m.planes[x].bytesused, 1, file);
		}
		fclose(file);
		//读取数据后将缓冲区放入队列
		ret = ioctl(fd, VIDIOC_QBUF, &readbuffer);
		if(ret < 0)
			perror("放入队列失败");
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