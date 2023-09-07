#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include<iostream>
#include"write_packet_to_fifo.h"

extern unsigned char * frameBuffer;

static int fd;
const char * FIFO_PATH = "/tmp/FIFO";

TaskScheduler* scheduler;
UsageEnvironment * env;
RTPSink* videoSink;
H264VideoStreamFramer* videoSource;

void fifo_open(){
    if(access(FIFO_PATH, F_OK|W_OK) != 0){
        if(mkfifo(FIFO_PATH, 0777) != 0 ){
            printf("mkfifo error \n");
            return;
        }
    }

    // fd = open(FIFO_PATH,O_RDONLY);
    // if(fd < 0){
    //     printf("open fifo failed! %d \n",fd);
    // }
    // printf(" create fifo finished! %d \n",fd);
}

void announceURL(RTSPServer* rtspServer, ServerMediaSession* sms) {
    if (rtspServer == NULL || sms == NULL) return; // sanity check

    UsageEnvironment & env = rtspServer->envir();

    env << "Play this stream using the URL ";
    if (weHaveAnIPv4Address(env)) {
        char* url = rtspServer->ipv4rtspURL(sms);
        env << "\"" << url << "\"";
        delete[] url;
        if (weHaveAnIPv6Address(env)) env << " or ";
    }
    if (weHaveAnIPv6Address(env)) {
        char* url = rtspServer->ipv6rtspURL(sms);
        env << "\"" << url << "\"";
        delete[] url;
    }
    env << "\n";
}

void create_live555(){
    scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);
    RTSPServer* rtspServer = RTSPServer::createNew(*env, 554, NULL);

    OutPacketBuffer::maxSize = 1920*1080;//防止单帧数据过大
    char const* streamName = "media";
    //char const* inputFileName = "my_fifo";
    char const* inputFileName = FIFO_PATH;
    ServerMediaSession* sms = ServerMediaSession::createNew(*env, streamName, streamName, "this is description");

    sms->addSubsession(H264VideoFileServerMediaSubsession::createNew(*env, inputFileName, False));
    rtspServer->addServerMediaSession(sms);
    announceURL(rtspServer, sms);
    printf("create live555 finished ! stream Name %s\n",streamName);
    env->taskScheduler().doEventLoop();
   
}

/************************************multicast*******************************************/

void afterPlaying(void*) {
    *env << "...done reading from file\n";
    videoSink->stopPlaying();
    Medium::close(videoSource);
}

void play() {
    // Open the input file as a 'byte-stream file source':
    ByteStreamFileSource* fileSource = ByteStreamFileSource::createNew(*env, FIFO_PATH);
    // ByteStreamMemoryBufferSource* fileSource = ByteStreamMemoryBufferSource::createNew(*env,frameBuffer,FRAME_BUFFER_SIZE,True);
    if (fileSource == NULL) {
        *env << "Unable to open file \"" << FIFO_PATH
            << "\" as a byte-stream file source\n";
        exit(1);
    }

    FramedSource* videoES = fileSource;
    // Create a framer for the Video Elementary Stream:
    // videoSource = H264VideoStreamFramer::createNew(*env, videoES,True,True);
    H264VideoStreamDiscreteFramer * videoDSource = H264VideoStreamDiscreteFramer::createNew(*env, videoES);
    // videoSource = H264VideoStreamFramer::createNew(*env, videoES,False,True);

    // Finally, start playing:
    *env << "Beginning to read from file...\n";
    videoSink->startPlaying(*videoDSource, afterPlaying, videoSink);
    // videoSink->startPlaying(*videoSource, afterPlaying, videoSink);
}

void create_live555_multicast(){

    scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    struct sockaddr_storage destinationAddress;
    destinationAddress.ss_family = AF_INET;
    // ((struct sockaddr_in&)destinationAddress).sin_addr.s_addr = chooseRandomIPv4SSMAddress(*env);
    // std::cout << ((struct sockaddr_in&)destinationAddress).sin_addr.s_addr << std::endl;
    ((struct sockaddr_in&)destinationAddress).sin_addr.s_addr = 697889512;

    const unsigned short rtpPortNum = 18888;
    const unsigned short rtcpPortNum = rtpPortNum+1;
    const unsigned char ttl = 255;

    const Port rtpPort(rtpPortNum);
    const Port rtcpPort(rtcpPortNum);

    Groupsock rtpGroupsock(*env, destinationAddress, rtpPort, ttl);
    rtpGroupsock.multicastSendOnly(); // we're a SSM source
    Groupsock rtcpGroupsock(*env, destinationAddress, rtcpPort, ttl);
    rtcpGroupsock.multicastSendOnly(); // we're a SSM source

    // Create a 'H264 Video RTP' sink from the RTP 'groupsock':
    OutPacketBuffer::maxSize = 1920*1080;
    videoSink = H264VideoRTPSink::createNew(*env, &rtpGroupsock, 96);

    // Create (and start) a 'RTCP instance' for this RTP sink:
    const unsigned estimatedSessionBandwidth = 1000 * 1024 * 1024; // in kbps; for RTCP b/w share
    const unsigned maxCNAMElen = 100;
    unsigned char CNAME[maxCNAMElen+1];
    gethostname((char*)CNAME, maxCNAMElen);
    CNAME[maxCNAMElen] = '\0'; // just in case

    RTCPInstance* rtcp = RTCPInstance::createNew(*env, &rtcpGroupsock,estimatedSessionBandwidth, 
        CNAME,videoSink, NULL,True);
    // Note: This starts RTCP running automatically

    RTSPServer* rtspServer = RTSPServer::createNew(*env, 554);
    if (rtspServer == NULL) {
        *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
        exit(1);
    }
    ServerMediaSession* sms = ServerMediaSession::createNew(*env, "media", FIFO_PATH,
        "Session streamed by \"testH264VideoStreamer\"",True);

    sms->addSubsession(PassiveServerMediaSubsession::createNew(*videoSink, rtcp));
    rtspServer->addServerMediaSession(sms);
    announceURL(rtspServer, sms);

    // Start the streaming:
    *env << "Beginning streaming...\n";
    play();

    env->taskScheduler().doEventLoop(); // does not return

    return; // only to prevent compiler warning
}

int main(){
    fifo_open();
    create_live555_multicast(); //组播
    // create_live555(); // 单播服务
    return 0;
}