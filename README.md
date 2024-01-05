# rk_live555

# Abstract

由于之前[rk_ffmpeg](https://github.com/EZreal-zhangxing/rk_ffmpeg)在推拉流上延迟依然有300ms+

观察编解码即使调用硬件，依然存在很长的传输延时，因此考虑接入live555来做推拉流

本项目主要目的，在rk3588上实现硬件编解码，并接入live555进行推拉流

[2023/09/05]在LIVE555上实现了如下的特性:

- [x] 调用OpenCv读取帧并推流
- [x] 仅支持推流协议RTSP
- [x] 推拉流使用MPP硬件加速编解码
- [x] 从网络拉取数据流
- [x] 集成Live555进行推拉流
- [x] 支持单播/组播
- [x] 延迟20ms以下

# 2.配置环境

本项目环境依赖如下：
- [x] libdrm
- [x] rockchip_mpp
- [x] live555
- [x] Opencv
- [x] SDL2 (项目遗留)
- [x] FFmpeg (项目遗留)

## 2.1 LIVE555安装

从[Live555](http://live555.com/mediaServer/#downloading)下载最新版本的源码，然后进入主目录，添加config.rk3588,该文件主要申明编译架构以及编译参数。文件内容如下：

```
CROSS_COMPILE?=         aarch64-linux-gnu-
COMPILE_OPTS =          $(INCLUDES) -I. -O2 -DSOCKLEN_T=socklen_t -DNO_SSTREAM=1 -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64 -std=c++2a -DNO_STD_LIB -I/usr/include/openssl
C =                     c
C_COMPILER =            $(CROSS_COMPILE)gcc
C_FLAGS =               $(COMPILE_OPTS)
CPP =                   cpp
CPLUSPLUS_COMPILER =    $(CROSS_COMPILE)g++
CPLUSPLUS_FLAGS =       $(COMPILE_OPTS) -Wall -DBSD=1
OBJ =                   o
LINK =                  $(CROSS_COMPILE)g++ -o
LINK_OPTS =
CONSOLE_LINK_OPTS =     $(LINK_OPTS)
LIBRARY_LINK =          $(CROSS_COMPILE)ar cr
LIBRARY_LINK_OPTS =     $(LINK_OPTS)
LIB_SUFFIX =                    a
LIBS_FOR_CONSOLE_APPLICATION = -lssl -lcrypto
LIBS_FOR_GUI_APPLICATION =
EXE =
#个人安装目录，自己修改
PREFIX = /usr/local/live555

```

其中需要两个依赖包：
- [x] openssl
- [x] libcrypto

然后执行编译安装即可
```
./genMakefiles rk3588
make -j16 && make install
```

## 2.2 其他组件安装

Opencv/FFmpeg/Mpp 请参考 [环境配置](环境配置.md)

# 3. 程序说明

在当前主目录下创建build文件夹，并运行
```
cd build
cmake .. && make -j16
```

会生成如下结构的可执行程序：
```
    ├── drm_image_test(读取指定图片，通过libdrm显示)
    ├── drm_test (libdrm测试程序)
    ├── rtsp_live555_client (基于live555的rtsp客户端)
    ├── rtsp_send_opencv_ffmpeg (基于ffmpeg的数据编码推流)
    ├── rtsp_send_opencv_mpp (基于mpp的数据[yuv]编码ffmpeg推流)
    ├── rtsp_send_opencv_mpp_rgb (基于mpp的数据[rgb]编码ffmpeg推流)
    ├── rtsp_send_opencv_mpp_rgb_live555 (基于mpp的数据[rgb]编码live555推流)
    ├── rtsp_send_opencv_mpp_yuv_live555 (基于mpp的数据[yuv]编码live555推流)
    ├── rtsp_send_opencv_mpp_yuv_live555_server (基于mpp的数据[yuv]编码live555推流,集成live推流服务)
    └── rtsp_service (live555 服务端，提供组播/单播服务)
```


## 3.1 drm_test
通过libdrm进行数据显示

## 3.2 drm_image_test
读取指定图片，并写入DRM中进行显示

DRM的主要流程如下：

```
 ----------      ---------------------      -----------     -------------
| open(fd) | -> | drmModeGetResources | -> | connector |   | create_dumb |  
 ----------      ---------------------      -----------     -------------
                        |                                          |
                     ------                                      handle
                    | CRTC |<----event handle                      v
                    ------          |                         ---------------
                                    |                        | drmModeAddFB/2| (创建FrameBuffer)
                                    |                         ---------------
                                    v                               |            
             ------------     ----------------      ------     -----------      
            | image Data | -->|virtual address|<-- | mmap |<--| map_dumb  |                                                 
             ------------     ----------------      ------     ----------- 
                                            (内存映射将FrameBuffer 映射到用户空间)       
```

1. 首先打开drm对应的驱动
2. 获取资源信息，包含了CTRC，Connector等信息
3. 在CPU上创建一块dumb区域拿到该块内存的句柄 create_dumb，这块区域应该能刚好保存一帧数据，所以对于YUV数据来说，`bpp(位深) = 8`,RGB数据 `bpp=24`，以`1920x1080`的图像数据为例。YUV在Mpp解码后的大小因为是按照16位对齐，所以解码后的图片大小为 `1920x1088x3/2` 其中Y数据 `1920x1088`,U,V数据各为`1920x1088/4`。其中对于YUV420SP,UV数据是交叉存储。而YUV420P则是分开存储。补充知识：这两个格式而言，数据的排列是不一样的。NV12/NV21属于YUV420SP的一种，因此UV是交叉排列，而YUV420P是按照区域分开
```
-------------------------------------           -------------------------------------  
|                                   |           |                                   |
|                                   |           |                                   |
|                 Y                 |           |                 Y                 |
|              (YUV420P)            |           |               (YUV420SP)           |
|                                   |           |                                   |
|                                   |           |                                   |
-------------------------------------           -------------------------------------
|                 U                 |           |U|V|U|V|                           |
|-----------------|-----------------|           |                                   |
|                 V                 |           |                                   |
-------------------------------------           -------------------------------------
```

1. 根据上一步中得到的句柄handle创建FrameBuffer
2. 将dumb数据映射到用户空间得到虚拟地址
3. 将图像数据写入该虚拟地址
4. 调用CRTC进行数据刷新显示

上述两个程序，主要是对DRM进行测试

[Ref:DRM](https://blog.csdn.net/hexiaolong2009/article/details/83720940)

## 3.3 rtsp_service
live555的服务端
主要实现功能如下
- [x] 从FIFO通道文件中读取数据并发送
- [x] 单播服务
- [x] 组播服务

在`rtsp_service.cpp` 代码中主要有两个方法`create_live555_multicast`，`create_live555`,分别对应组播服务和单播服务。可以修改`main`方法开启对应的服务

create_live555 `[Ref:live555/testProgs/testH264VideoToTransportStream.cpp]`

create_live555_multicast `[Ref:live555/testProgs/testH264VideoStreamer.cpp]`

运行脚本：
```
./rtsp_service
```

## 3.4 rtsp_send_opencv_mpp_yuv_live555
live555的发送端
从Video*中捕获数据，并转换成YUV,送入Mpp编码器编码，拿到编码后的原始码流，交由live555进行传输

其中与live555通信分了两种方式：1. FIFO通道，2. 异步队列queue

Live555提供的样例主要是以文件的方式进行推流，所以使用FIFO管道文件，在编码后将包数据写入FIFO，由LIVE555进行消费发送

FIFO最大为64K，并且涉及IO操作，所以我同时也实现了通过内存队列的方式来进行数据流转，即异步队列的方式。

`rtsp_send_opencv_mpp_yuv_live555`采用的是**FIFO通道**方案

运行脚本：
```
# 确保live555服务已启动
./rtsp_service
# 启动发送服务
./rtsp_send_opencv_mpp_yuv_live555
```

## 3.5 rtsp_send_opencv_mpp_rgb_live555
live555的RGB数据发送端
和3.4唯一区别在于原始输入数据是RGB，不需要转换成YUV。**注：RGB数据量是YUV的一倍，会导致编码效率降低。**，同时这两块的数据拷贝方式不一样，具体参考[rk_ffmpeg](https://github.com/EZreal-zhangxing/rk_ffmpeg)的解析

运行脚本：
```
# 确保live555服务已启动
./rtsp_service
# 启动发送服务
./rtsp_send_opencv_mpp_rgb_live555
```

## 3.6 rtsp_send_opencv_mpp_yuv_live555_server

主要功能同`rtsp_send_opencv_mpp_yuv_live555`
主要区别在于 `init_data()`方法中，将`fifo_open`替换成了 启动组播服务的线程，同时`send_packet()`方法中的`fifo_write()`替换成了`buffer_write()`,并注释了`destory_`方法中的`fifo_close`

```
// init_data()::line:312 

thread live555(create_multicast_live555);    
live555.detach();
// fifo_open();

// send_packet()::line:407

// fifo_write(packet);
buffer_write(packet);

// destroy_()::line:529
// fifo_close();

```

**该方法采用的是异步队列的实现方式。**

该程序主要依赖于`write_packet_to_fifo.cpp`文件

该文件分为四个部分：
1. fifo_x系列
2. buffer_x 系列
3. 单播部分
4. 组播部分

fifo_x系列，是操作FIFO文件的一套接口，buffer_x则是操作异步队列的一套接口

单播部分依然是从文件中读取数据并发送
组播部分则按照live555规则定义了一个新的数据源，该数据源主要目的从异步队列中获取数据。

```
The "test*Streamer" test programs read from a file. Can I modify them so that they take input from a H.264, H.265, or MPEG encoder instead, so I can stream live (rather than prerecorded) video and/or audio?
Yes. The easiest way to do this is to change the appropriate "test*Streamer.cpp" file to read from "stdin" (instead of "test.*"), and then pipe the output of your encoder to (your modified) "test*Streamer" application. (Even simpler, if your operating system represents the encoder device as a file, then you can just use the name of this file (instead of "test.*").)
Alternatively, **if your encoder presents you with a sequence of frames (or 'NAL units'), rather than a sequence of bytes, then a more efficient solution would be to write your own "FramedSource" subclass that encapsulates your encoder**, and delivers audio or video frames directly to the appropriate "*RTPSink" object. **This avoids the need for an intermediate 'framer' filter that parses the input byte stream.** (If, however, you are streaming H.264, H.265, or MPEG-4 (or MPEG-2 video with "B" frames), then you should insert the appropriate "*DiscreteFramer" filter between your source object and your "*RTPSink" object.)

For a model of how to do that, see "liveMedia/DeviceSource.cpp" (and "liveMedia/include/DeviceSource.hh"). You will need to fill in parts of this code to do the actual reading from your encoder.
```
根据官方文档，以字节流数据作为数据源时[`Ref:ByteStreamFileSource.cpp`]，读取数据后会交给[`Ref:H264VideoStreamFramer.cpp`] 进行帧的组合。
然后进行发送，而我们编码器输出就是一个完整的帧编码后的数据，所以我们自定义数据源只需要按帧数据读取然后发送即可,并且可以避免对码流的过滤所带来的额外开销

因此我创建了一个自定义的数据源子类继承FramedSource。该子类主要重载doGetNextFrame方法。该方法用于加载数据,加载后会调用父类方法进行数据处理。

```
class RedefineByteStreamMemoryBufferSource: public FramedSource {
public:
    static RedefineByteStreamMemoryBufferSource* createNew(UsageEnvironment& env,
                            Boolean deleteBufferOnClose = True,
                            unsigned preferredFrameSize = 0,
                            unsigned playTimePerFrame = 0);
        // "preferredFrameSize" == 0 means 'no preference'
        // "playTimePerFrame" is in microseconds

protected:
    RedefineByteStreamMemoryBufferSource(UsageEnvironment& env,
                    Boolean deleteBufferOnClose,
                    unsigned preferredFrameSize,
                    unsigned playTimePerFrame);
        // called only by createNew()

    virtual ~RedefineByteStreamMemoryBufferSource();

private:
    // redefined virtual functions:
    virtual void doGetNextFrame();

private:
    ConcurrenceQueue<FramePacket *> queue;
    u_int64_t fCurIndex;
    Boolean fDeleteBufferOnClose;
    unsigned fPreferredFrameSize;
    unsigned fPlayTimePerFrame;
    unsigned fLastPlayTime;
    Boolean fLimitNumBytesToStream;
    u_int64_t fNumBytesToStream; // used iff "fLimitNumBytesToStream" is True
};

RedefineByteStreamMemoryBufferSource*
RedefineByteStreamMemoryBufferSource::createNew(UsageEnvironment& env,
					Boolean deleteBufferOnClose,
					unsigned preferredFrameSize,
					unsigned playTimePerFrame) {
    return new RedefineByteStreamMemoryBufferSource(env, deleteBufferOnClose, preferredFrameSize, playTimePerFrame);
}

RedefineByteStreamMemoryBufferSource::RedefineByteStreamMemoryBufferSource(UsageEnvironment& env,
							   Boolean deleteBufferOnClose,
							   unsigned preferredFrameSize,
							   unsigned playTimePerFrame)
    : FramedSource(env), fCurIndex(0), fDeleteBufferOnClose(deleteBufferOnClose),
        fPreferredFrameSize(preferredFrameSize), fPlayTimePerFrame(playTimePerFrame), fLastPlayTime(0),
        fLimitNumBytesToStream(False), fNumBytesToStream(0) {
}

RedefineByteStreamMemoryBufferSource::~RedefineByteStreamMemoryBufferSource() {
    if (fDeleteBufferOnClose) delete[] &queue;
}

void RedefineByteStreamMemoryBufferSource::doGetNextFrame() {
    /**
     * TODO:从异步队列中拿到数据，拷贝给fTo对象，并设置fFrameSize
    */
    fFrameSize = fPacket->size;
    memmove(fTo, fPacket->dataPtr, fPacket->size);

    // Do other things 
    // Inform the downstream object that it has data:
    FramedSource::afterGetting(this);
    
}
```

定义好数据源后，与`H264VideoStreamDiscreteFramer`数据流进行绑定,该数据流与`H264VideoStreamFramer`的区别在于后者**会对流进行过滤，过滤出完整的NALU信息**，这个会相当耗时，会导致过多的耗时，因为我们本身是一个完整的帧，所以并不需要对数据进行过滤。
```
 (If, however, you are streaming H.264, H.265, or MPEG-4 (or MPEG-2 video with "B" frames), then you should insert the appropriate "*DiscreteFramer" filter between your source object and your "*RTPSink" object.)
```
注：`H264VideoStreamDiscreteFramer`有两个参数比较重要:1.是否带StartCode.2.是否插入AUD. 

startCode是判断一个帧开始的标识码是四个字节表示`[00,00,00,01]`,根据它解码器会判断流数据的起始和截止，如果没有解码器是无法解码。
AUD是帧分割符

整体流程如下：

```
-------------------------------------------                             ----------------------------------
|   RedefineByteStreamMemoryBufferSource  |  --从queue读取数据写入fTo--> |  H264VideoStreamDiscreteFramer | ---> send
-------------------------------------------                             ----------------------------------

```

注意到：**组播需要一个广播地址,这里我固定成了232.242.152.41**, 可以自行修改广播地址 `write_packet_to_fifo.cpp:line:241`

## 3.7 rtsp_live555_client

基于Live555的RTSP客户端，可以接受单播或者组播的数据。[Ref:live555/testProgs/testRTSPClient.cpp]


用法：
```
./rtsp_live555_client rtsp://xxx
```

该程序主要由三个文件构成:`rtsp_connect_discrete.cpp`，`mpp_decoder_discrete.cpp`,`rtsp_client.cpp`

这里`rtsp_connect_discrete.cpp`,还有一个相似的文件`rtsp_connect.cpp`
`mpp_decoder_discrete.cpp` 同样也有一个相似的文件`mpp_decoder.cpp`，这两套文件分别对应


`rtsp_connect.cpp`,`mpp_decoder.cpp`对应使用`H264VideoStreamFramer` 进行发送的服务端

`rtsp_connect_discrete.cpp`,`mpp_decoder_discrete.cpp`对应使用`H264VideoStreamDiscreteFramer` 进行发送的服务端


### 3.7.1 服务端源码分析

分析以组播为例：
#### 3.7.1.1 反向分析

在我进行发送包抓包时发现，对于过大的包会在网络层进行分包，比且有相同的大小，查看live555的源码发现，在`GroupsockHelper.cpp:447`,有如下代码
```
Boolean writeSocket(UsageEnvironment& env,
		    int socket, struct sockaddr_storage const& addressAndPort,
		    unsigned char* buffer, unsigned bufferSize) {
  do {
    SOCKLEN_T dest_len = addressSize(addressAndPort);
    int bytesSent = sendto(socket, (char*)buffer, bufferSize, MSG_NOSIGNAL,
			   (struct sockaddr const*)&addressAndPort, dest_len);
    if (bytesSent != (int)bufferSize) {
      char tmpBuf[100];
      sprintf(tmpBuf, "writeSocket(%d), sendTo() error: wrote %d bytes instead of %u: ", socket, bytesSent, bufferSize);
      socketErr(env, tmpBuf);
      break;
    }
    return True;
  } while (0);

  return False;
}

```
可以看到该方法主要是将buffer数据通过**sendto系统函数**通过Socket发送出去，其中发送的大小有bufferSize决定, `writeSocket`函数由 `Groupsock.cpp:43 OutputSocket::write` 进行封装。在同一文件中可以看到`Groupsock` 继承于`OutputSocket`并用方法`Groupsock::output`封装了`OutputSocket::write`

```
Boolean Groupsock::output(UsageEnvironment& env, unsigned char* buffer, unsigned bufferSize) {
  do {
    // First, do the datagram send, to each destination:
    Boolean writeSuccess = True;
    for (destRecord* dests = fDests; dests != NULL; dests = dests->fNext) {
      if (!write(dests->fGroupEId.groupAddress(), dests->fGroupEId.ttl(), buffer, bufferSize)) {
	    writeSuccess = False;
	    break;
      }
    }
    if (!writeSuccess) break;
    statsOutgoing.countPacket(bufferSize);
    statsGroupOutgoing.countPacket(bufferSize);

    if (DebugLevel >= 3) {
      env << *this << ": wrote " << bufferSize << " bytes, ttl " << (unsigned)ttl() << "\n";
    }
    return True;
  } while (0);

  if (DebugLevel >= 0) { // this is a fatal error
    UsageEnvironment::MsgString msg = strDup(env.getResultMsg());
    env.setResultMsg("Groupsock write failed: ", msg);
    delete[] (char*)msg;
  }
  return False;
}
```

该方法主要目的是对加入这组的所有组播地址进行数据发送，发送数据为buffer,大小为bufferSize

#### 3.7.1.2 正向分析

参考组播代码 [Ref:live555/testProgs/testH264VideoStreamer.cpp]

在创建`rtcpGroupsock`和`rtpGroupsock`之后，构建了`H264VideoRTPSink`对象，Live555所有`*sink`的对象用于从`*Source`中读取或者写入数据。

其中创建`H264VideoRTPSink` 时的对象创建链如下:

```
--------------------     -----------------------     ----------------     ----------------------     -----------       -------------
| H264VideoRTPSink | --> | H264or5VideoRTPSink | --> | VideoRTPSink | --> | MultiFramedRTPSink | --> | RTPSink | ----> | MediaSink |
--------------------     -----------------------     ----------------     ----------------------     -----------   |   -------------
                                                                                                                Groupsock
                                                                                                                   V
                                                                                                            -----------------
                                                                                                            | fRTPInterface |
                                                                                                            -----------------

```
可以看到`Groupsock` 对象一路传到了`fRTPInterface`,最后被`RTPInterface::sendPacket`调用`Groupsock::output`方法发送数据，而该方法查看引用路径可以发现被`MultiFramedRTPSink::sendPacketIfNecessary`封装，发送`fOutBuf->packet()`的数据，大小为`fOutBuf->curPacketSize()`

而`MultiFramedRTPSink::sendPacketIfNecessary` 被 `MultiFramedRTPSink::afterGettingFrame1`所调用,用于处理获取到的帧数据。所以整个过程的调用链如下：

```

--------------------     ----------------------     ----------------------     -----------------------     ----------------
| H264VideoRTPSink | --> | MultiFramedRTPSink | --> | MultiFramedRTPSink | --> | MultiFramedRTPSink  | --> | RTPInterface |
|   startPlaying   |     |  afterGettingFrame |     | afterGettingFrame1 |     |sendPacketIfNecessary|     |  sendPacket  |
--------------------     ----------------------     ----------------------     -----------------------     ----------------
                                                                                                                    |
                                                                                                                    V
                                                ----------      ---------------       ----------------      ---------------
                                                | sendto | <--  | writeSocket |  <--  | OutputSocket | <--  |  Groupsock  |
                                                |        |      |             |       |     write    |      |    output   |
                                                ----------      ---------------       ----------------      ---------------

```

在`MultiFramedRTPSink::afterGettingFrame1:285`中可以看到，将帧数据塞入`OutPacketBuffer`时，会对帧的大小进行计算，如果整个`Buffer`的剩余空间不够填充整个数据帧，也就是`wouldOverflow() = True`，进入下一步判断，并且帧数据的大小`frameSize`超过`Buffer`的`OutPacketBuffer::fMax`的值那么会对其进行分包 
```

--------------------------------------------------------
|                           |                          |
|                           | fCurOffset               |
|                           |                          |
|                           |                          |
--------------------------------------------------------
                          fMax

if (numFrameBytesToUse > 0) {
    // Check whether this frame overflows the packet
    if (fOutBuf->wouldOverflow(frameSize)) {
        // Don't use this frame now; instead, save it as overflow data, and
        // send it in the next packet instead.  However, if the frame is too
        // big to fit in a packet by itself, then we need to fragment it (and
        // use some of it in this packet, if the payload format permits this.)
        if (isTooBigForAPacket(frameSize)&& (fNumFramesUsedSoFar == 0 || allowFragmentationAfterStart())) {
            // We need to fragment this frame, and use some of it now:
            overflowBytes = computeOverflowForNewFrame(frameSize); // 计算超出包的剩余空间多少字节
            numFrameBytesToUse -= overflowBytes; // 本次处理多少字节下次处理
            fCurFragmentationOffset += numFrameBytesToUse; // 分包偏置
        } else {
            // We don't use any of this frame now:
            overflowBytes = frameSize;
            numFrameBytesToUse = 0;
        }
        fOutBuf->setOverflowData(fOutBuf->curPacketSize() + numFrameBytesToUse,overflowBytes, presentationTime, durationInMicroseconds);
    } else if (fCurFragmentationOffset > 0) {
        // This is the last fragment of a frame that was fragmented over
        // more than one packet.  Do any special handling for this case:
        fCurFragmentationOffset = 0;
        fPreviousFrameEndedFragmentation = True;
    }
}
```


所以可以看到`OutPacketBuffer::fMax`关系到了整个数据包发送的大小，`OutPacketBuffer`对象初始化是初始化`MultiFramedRTPSink`时通过方法`setPacketSizes()`创建的，这也是为什么一些关于优化的文章会提到优化这个地方的参数，该方法有两个参数，第一个是包的`preferredPacketSize`,第二个就是包的`maxPacketSize`,所以我为了加大包一次性发送大小我调整该值到了`8192 * 3`。这样可以减少关键帧的发送次数。同时这个值也不能过大。在Ubuntu下系统函数sendto的最大发送大小和系统的设置有关，一般是不能超过256k，但是UDP包的最大尺寸为65507，如果超出这个尺寸，依然会在网络层进行分包发送和重组。



### 3.7.2 H264VideoStreamFramer 系列

这套是当服务器端调用H264VideoStreamFramer进行发送时，对应客户端代码文件是:`rtsp_connect.cpp,mpp_decoder.cpp`

这一套服务发送延迟大概在`170ms`左右

#### 3.7.2.1 CMakeList.txt
```
add_executable(rtsp_live555_client
${PROJECT_SOURCE_DIR}/src/rtsp_connect.cpp 
${PROJECT_SOURCE_DIR}/src/mpp_decoder.cpp  
${PROJECT_SOURCE_DIR}/src/rtsp_client.cpp)
```

当服务端用这个进行发送时，会对码流进行过滤，根据帧的起始码(`StartCode{00 00 00 01}`),提取NALU单元。
对于我们发送的数据而言，大部分P帧由三部分组成`PPS{00 00 00 01 67}`,`SPS{00 00 00 01 68}`,FrameData`{00 00 00 01 41}`,这里每个部分都由起始码隔开。
而大部分I帧(关键帧)同样由三部分构成，两块自定义信息`{00 00 00 01 06}`,FrameData`{00 00 00 01 65}`

P帧和I帧的三个部分会分成三个独立的包在网络中发送，但是mpp编码器在编码过程中需要输入完整的PPS和SPS信息。因此在客户端收到包后，**需要对其进行拼接**。 `[Ref:rtsp_connect.cpp::concat_buffer_and_send:201]`
根据发包的规则，live555对同一个帧的数据会设置相同的`presentationTime`，所以只需要根据这个对象的值就能判断是否属于同一个帧，拼接好后送入异步队列，交由解码线程进行解码。

**注：网络接收到的包都缺失了起始码，需要手动加上**

### 3.7.3 H264VideoStreamDiscreteFramer 系列

这套是当服务器端调用H264VideoStreamFramer进行发送时，对应客户端代码文件是：`rtsp_connect_discrete.cpp,mpp_decoder_discrete.cpp`

使用这套发送，网络传递(局域网)+解码+显示大概在`10ms`左右

#### 3.7.3.1 CMakeList.txt
```
add_executable(rtsp_live555_client
${PROJECT_SOURCE_DIR}/src/rtsp_connect_discrete.cpp 
# ${PROJECT_SOURCE_DIR}/src/ffmpeg_decode.cpp  # 可选
${PROJECT_SOURCE_DIR}/src/mpp_decoder_discrete.cpp  
${PROJECT_SOURCE_DIR}/src/rtsp_client.cpp)
```

服务端H264VideoStreamFramer这个对象对码流是不会进行过滤提取，会直接转发。对于我们每次写入的都是一帧完整数据直接转发会减少很多处理时间。

同时这个流对象不会去掉起始码，所以接收到的数据就是你发送的数据。同时我们也要注意到，当一个数据包过大的时候发送依然会分包发送，所以同样需要做拼接操作，
但这个时候就需要根据协议的开头码来判断帧的结束，所以当`第五个字节`的值是`0x06`或者`0x67`时，说明上一个帧的包数据发送结束。然后将累计收到的数据送入队列进行后续的解码和显示操作。


## 4. 解码说明

本项目解码主要借用rockchip_mpp进行硬件解码，可参考`mpp_decoder.cpp`或者`mpp_decoder_discrete.cpp`里面的代码

解码流程如下所示：
```
                    ---------------             ---------                 -----------
                    | Packet_data | <--IsNull-- | frame |  ---NotNull-->  | display |
                    ---------------             ---------                 ----------- 
                           |                        ^
                           V                        |
------------     ---------------------     --------------------
| mpp_init | --> | decode_put_packet | --> | decode_get_frame |
------------     ---------------------     --------------------


```

该部分代码主要分为两部分：初始化和编解码

### 4.1 初始化部分
初始化编解码的上下文和Api，然后设置相应的参数，主要是`buffer_group`以及解码输出格式等

### 4.2 编解码部分
解码器在未收到I帧的情况下是无法解码的，因为没有帧高，帧长等信息。
同时我们并不能确保客户端接入网络时，收到的第一帧数据是`I帧`，因此程序会等待`I帧`的到来，然后将收到的第一个**I帧**的数据送入解码器解析帧的相关信息。

在送入`I帧`后MPP解码器触发`mpp_frame_get_info_change`，同时初始化`buffer_group`，此时可以根据帧大小来设置`buffer_group`的缓存大小和帧数限制，然后我们必须调用`MPP_DEC_SET_INFO_CHANGE_READY`，告知`buffer_group`完成设置继续进行解码。

每次送入`Packet`后，调用`decode_get_frame`并不一定会立刻返回帧数据,因此我们需要根据`frame`是否为空来判断下一步。

## 5. drm显示说明

这里同样采取了两种方式用来显示：一种是直接显示，一种是异步队列的方式。

直接显示：将解码后得到的`frame`数据直接写入drm的用户空间中的地址中。这一步同样会等到显示结束然后处理下一个到来的数据包，会有一定的处理延迟。
异步队列：将解码后的`frame`数据送入异步队列，等到`display`线程来消费。

因此以`mpp_decoder_discrete.cpp`为例
这里面包含了三个主要的显示函数：`read_data_and_decode`,`read_data_and_decode_show_directly`,`smart_read_data_and_decode`。

`read_data_and_decode`:采用同步的方式，获取从网络接收到的数据包，送入解码器解码，然后进行同步展示。该方法由`rtsp_connect_*.cpp`来主动调用。
`read_data_and_decode_show_directly`:采用异步的方式获取数据包，同步展示的方法。
`smart_read_data_and_decode`:则是异步获取数据包，并且异步展示。

## 6. 坑与填

### 6.1 DRM显示坑
在3.2章节说明了DRM显示的过程，这里详细补充几点细节以及遇到的坑，具体可参考[Ref:DRM](https://blog.csdn.net/hexiaolong2009/article/details/83720940)

为了避免撕裂，我一开始使用了双buf+flip的操作，写入数据后，调用filpEvent进行通知，等待Vblank时间间隙的到来然后刷新CTRC。同时该方法每次进入时都会`memset`显存地址内的数据`[mpp_decoder_discrete.cpp::write_data_to_buf:line264]`，但是实验结果显示会出现绿屏闪烁。逐帧查看是完全没问题(这里就排除了解码器的输出问题)。逐帧加快播放速度就会出现部分绿屏，整体感觉像是一部分数据没有来得及写入`buffer`。

后来读到撕裂的具体原理是因为读写速度不一致导致。因为采用事件通知机制，必然是异步的。猜想是填入新帧时，我清空了数据，因此导致显示时后面数据被清空，因此在注释memset后该问题消失

### 6.2 v4l2
为了进一步缩小opencv捕获输入设备的数据，我考虑引入v4l2框架进行视频帧的获取,见代码`v4l2_read.cpp`.
该代码为v4l2的读取测试代码。
v4l2与设备驱动进行交互的函数主要是：`ioctl`,通过发送指定指令达到目的
主要流程如下：

1. `open`函数打开设备驱动获得文件句柄`fd`
2. `VIDIOC_S_FMT`设置采集格式，输入是`v4l2_format`格式的数据，对于设备采集的数据类型可以通过:`v4l2-ctl -d /dev/video* -V -D `进行查询驱动所支持的数据格式。对于可捕获的格式主要分两种：`V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`以及`V4L2_BUF_TYPE_VIDEO_CAPTURE`，这两种类别在后面分别对应不同的处理方法，但大致是相同的。主要区别在于`multiplane`相比较与`CAPTURE`会返回多个`plane`，需要迭代去处理,而`CAPTURE`可以直接获取一帧图像。`RK3588`的`HDMI`输入只支持`multiplane`，因此下面以`multiplane`进行介绍
3. 在第2步设置采集格式后，可以通过`VIDIOC_G_FMT`进行查询采集格式是否设置正确，该指令输入和输出均是`v4l2_format`。
4. 申请缓冲队列，`VIDIOC_REQBUFS`通过该指令可以在内核开启对应个数的缓冲区，设备驱动会将数据写入该部分，利用程序对这块进行读写既可以获得视频数据。这里memory有多种方式，采用最多的是`V4L2_MEMORY_MMAP`内存映射方式，即将内核空间的区域映射到用户空间。同时还注意`V4L2_MEMORY_DMABUF`(有待补充)
5. 申请缓存之后，同时要注意，我们的设备捕获的是`multiplane`的模式，一帧数据会返回多个`plane`(具体有多少个`plane`在第3步进行采集格式查询的时候会返回)，因此我们需要创建一块用户空间的内存区域，其中包含了和缓存队列元素个数一致的用户内存区域，用来对缓存队列进行内存映射。因为是`multiplane`模式，所以我们定义一个`v4l2_plane`的数组，同时也定义了一个和`v4l2_plane`一一对应的用户空间地址`mmpaddr`,见：`struct image_buffer_with_plane`，并将`plane`信息与`v4l2_buffer`进行绑定,然后使用`VIDIOC_QUERYBUF`查询缓存信息，然后使用`mmap`函数根据缓存查询出来的`plane`大小和偏移，**逐一映射到用户空间**。缓存队列的长度同时也决定了`struct image_buffer_with_plane`的数组长度。
6. 在第5步映射完毕后，需要将查询到的`plane`进行入队,`VIDIOC_QBUF`指令，这样驱动在获取到数据后，会将数据写入该队列，等待消费，在我们将缓存出队消费掉后进行压队。以达到循环使用的过程

```
				                     data
				                      |
				   ---------------------------------------
				   |                                     |
				  pop                                 	push
				   ^                                     |
				   |                                     V
				-------    -------    -------         -------
				| ele | -- | ele | -- | ... |         | ele |
				-------    -------    -------         -------  

```
7. `VIDIOC_STREAMON` 开始进行数据获取
8. 使用`VIDIOC_DQBUF`指令进行出队操作，主要输入输出也是`v4l2_buffer`,出队后，根据`v4l2_buffer`中的`index`确定是属于队列的第几个元素，与第5步中申请的`image_buffer_with_plane`索引一致。**遍历`plane`，逐个`plane`进行拷出**
9. 出队后的数据，再次放回队列`VIDIOC_QBUF`
10. `VIDIOC_STREAMOFF`关闭数据获取
11. 取消映射`munmap`
12. `close`设备句柄


```   
                                                                                
                      ---------------------                                     
                      |    v4l2_format    |                                     
                      ---------------------                                     
                        |            ^                                          
                        |            |                                          
                        |            |                                          
                        V            |                                          
--------     ----------------     ----------------     -------------------     --------------------
| open | --> |  set_format  | --> |  get_format  | --> | request_buffer  | --> |   query_buffer   |
--------     ----------------     ----------------     -------------------     --------------------
                                                                                          |
                                                                                          V
                                                                                  ----------------           --------------------      ---------------
                                                                                  |   planes[n]  |  -mmap->  |   mmpaddress[n]  |  --> |  read data  |  
                                                                                  ----------------           --------------------      ---------------
                                                                                  |   planes[n]  |  -mmap->  |   mmpaddress[n]  |  --> |  read data  |  
                                                                 -------------->  |---------------           |-------------------      ---------------
                                                                 |                |   planes[n]  |  -mmap->  |   mmpaddress[n]  |  --> |  read data  |  
                                                                 |                |---------------           |-------------------      ---------------
                                                                 |                |   planes[n]  |  -mmap->  |   mmpaddress[n]  |  --> |  read data  |  
                                                                 |                ----------------           --------------------      ---------------
                                                                 |                        |          
                                                        ----------------                  V
                                                        | pop_out_data |          ------------------
                                                        ----------------          |  push_in_queue |
                                                                 |                ------------------
                                                                 |                        |
                                                                 |                  ---------------
                                                                 |                  |  stream on  |
                                                                 |                  ---------------
                                                                 |                        |
                                                                 |                        |
                                                                 --------------------------
                                                                                          |
                                                                                    ----------------
                                                                                    |  stream off  |
                                                                                    ----------------
                                                                                          |
                                                                                      ---------
                                                                                      | close |
                                                                                      ---------


```


### 6.3 待续


