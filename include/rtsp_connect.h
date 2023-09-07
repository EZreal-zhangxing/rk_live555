#ifndef RTSP_CONNECT
#define RTSP_CONNECT
// #pragma once
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <H264VideoRTPSink.hh>
#include <H264VideoStreamFramer.hh>
#include <Base64.hh>
#include <H264VideoRTPSource.hh>
#include <H264or5VideoStreamFramer.hh>

#include <fcntl.h>
#include<sys/stat.h>
// #include <queue>
#include "safely_queue.h"
#include <list>

/*false 使用rtp/udp True 使用rtp/tcp*/
#define REQUEST_STREAMING_OVER_TCP False
#define RTSP_CLIENT_VERBOSITY_LEVEL 1 //1 输出日志 0 不输出
#define DUMMY_SINK_RECEIVE_BUFFER_SIZE 1920*1080*3 // 接受buffer大小
/*客户端计数*/
static unsigned rtspClientCount = 0;

/**
 * 流的数据结构，可以更具需要自定义多个数据流来自不同的URL 关联至ourRTSPClient的
*/
class StreamClientState {
public:
    StreamClientState();
    virtual ~StreamClientState();
    MediaSubsessionIterator* iter;
    MediaSession* session;
    MediaSubsession* subsession;
    TaskToken streamTimerTask;
    double duration;
};

/**
 * 自定义RTSP客户端
*/
class ourRTSPClient: public RTSPClient {
public:
    static ourRTSPClient* createNew(UsageEnvironment& env, 
        char const* rtspURL,int verbosityLevel = 0,
        char const* applicationName = NULL,portNumBits tunnelOverHTTPPortNum = 0);
    StreamClientState scs; // 单一数据流的结构 如果多个数据流可以定义成数组或者Vector
protected:
    ourRTSPClient(UsageEnvironment& env, char const* rtspURL,
        int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum);
    // called only by createNew();
    virtual ~ourRTSPClient();
};

class RTSPReceiveData{
public:
    uint8_t * dataPtr;
    unsigned long size;
    struct timeval presentationTime;
    int is_key_frame;

    int64_t create_time;
    int64_t arrive_time;
    int64_t send_memory_time;
    int64_t decode_time;
    int64_t display_time;

    RTSPReceiveData * next;
    RTSPReceiveData() = default;
    RTSPReceiveData(uint8_t * add,unsigned long bufferSize,struct timeval presentationTime) 
        :size(bufferSize),is_key_frame(0),presentationTime(presentationTime),next(NULL){
        dataPtr = (uint8_t *)malloc(bufferSize);
        // printf(" copy data into dataptr %lu \n",bufferSize);
        // memcpy(dataPtr,add,bufferSize);      
        memmove(dataPtr,add,bufferSize);
    }
    ~RTSPReceiveData(){
        free(dataPtr);
    }
};

/**
 * 媒体输出对象
*/
#define BUFFER_SIZE_TO_SAVE 1920*1080*3
class DummySink: public MediaSink {
public:
    static DummySink* createNew(UsageEnvironment& env,MediaSubsession& subsession,
        SPropRecord * sPropRecords,unsigned int numsPropRecords,RTSPClient * client,char const* streamId = NULL);
public:
    void sendBufferdata(void* clientData,unsigned buffer_size,struct timeval);

    void create_h264_head(SPropRecord * sPropRecords);

    int check_head(void * clientData,unsigned index);

    int is_i_frame(void * clientData,unsigned buffer_size);

    int64_t* get_packet_time(void * clientData,unsigned offset);

    void receive_test(void * clientData,unsigned frameSize,struct timeval presentationTime);

    void concat_buffer_and_send(void* clientData,unsigned buffer_size,struct timeval presentationTime);

    void push_data_2_queue(RTSPReceiveData *data);

    RTSPReceiveData * pop_data(int deep);

    RTSPReceiveData * get_head();

    // void smart_concat_and_send(void* clientData,unsigned buffer_size,struct timeval presentationTime);
private:
    DummySink(UsageEnvironment& env, MediaSubsession& subsession, 
        char const* streamId,SPropRecord * sPropRecords,unsigned int numsPropRecords,RTSPClient * client);
    // called only by "createNew()"
    virtual ~DummySink();
    // this function will be called by continuePlaying()
    static void afterGettingFrame(void* clientData, unsigned frameSize,
        unsigned numTruncatedBytes,struct timeval presentationTime,
        unsigned durationInMicroseconds);

    void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
        struct timeval presentationTime, unsigned durationInMicroseconds);
private:
    // redefined virtual functions: and this function will be called by startPlaying()
    virtual Boolean continuePlaying();

private:
    u_int8_t* fReceiveBuffer;
    MediaSubsession& fSubsession;
    char* fStreamId;
    RTSPClient * hockRtspClient;

    SPropRecord * sPropRecords; // [0] sps [1] pps
    unsigned int numsPropRecords; // 2

    bool is_exist_head;
    unsigned char * head; // h264视频帧头信息
    unsigned headLens; // 头信息的长度
public:
    unsigned count = 0;
private:
    struct timeval preNodeOfTime;
    uint8_t * buffer;
    unsigned size;

public:
    bool stop_stream;
    ConcurrenceQueue<RTSPReceiveData *> receive_queue;
    RTSPReceiveData * keyFrameData;
};


class SELF_H264VideoRTPSink: public H264or5VideoRTPSink {
public:
    static SELF_H264VideoRTPSink* createNew(UsageEnvironment& env,char const* StreamId,MediaSubsession& subsession, Groupsock* RTPgs, unsigned char rtpPayloadFormat,
	    char const* sPropParameterSetsStr);
    void test();
protected:
    SELF_H264VideoRTPSink(UsageEnvironment& env,char const* StreamId,MediaSubsession& subsession, Groupsock* RTPgs, unsigned char rtpPayloadFormat,
            u_int8_t const* sps = NULL, unsigned spsSize = 0,
            u_int8_t const* pps = NULL, unsigned ppsSize = 0);
    // called only by createNew()
    virtual ~SELF_H264VideoRTPSink();

protected: 
    // redefined virtual functions:
    virtual char const* auxSDPLine();

private: 
    // redefined virtual functions:
    virtual Boolean sourceIsCompatibleWithUs(MediaSource& source);
    static void afterGettingFrame(void* clientData, unsigned frameSize,
		unsigned numTruncatedBytes, struct timeval presentationTime,
		unsigned durationInMicroseconds);

	void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
		struct timeval presentationTime, unsigned durationInMicroseconds);
    virtual Boolean continuePlaying();

private:
    u_int8_t* fReceiveBuffer;
    MediaSubsession& fSubsession;
    char* fStreamId;
};

/**
 * 建立rtcp链接的主要类别
*/
class rtsp_connect{
public:
    rtsp_connect() = default;
    /*rtsp 入口*/ 
    void openURL(UsageEnvironment& env, char const* progName, char const* rtspURL);
};

/*RTSP 三次握手*/ 
void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString);
/* 其他事件 */
void subsessionAfterPlaying(void* clientData);
void subsessionByeHandler(void* clientData, char const* reason);
void streamTimerHandler(void* clientData);
/*处理每个流的事件*/
void setupNextSubsession(RTSPClient* rtspClient);
/*关闭流*/
void shutdownStream(RTSPClient* rtspClient, int exitCode = 1);

#endif