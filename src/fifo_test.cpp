#include "write_packet_to_fifo.h"
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include<iostream>

#ifndef BYTE_FILE_READ
#define BYTE_FILE_READ

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
    FramePacket * fPacket  = buffer_get();
    if(fPacket == NULL){
        envir() << "fPacket send size 0\n";
        fFrameSize = 0;
    }else{
        envir() << "fPacket send size " << fPacket->size << "\n";
        fFrameSize = fPacket->size;
        memmove(fTo, fPacket->dataPtr, fPacket->size);
        fCurIndex += fFrameSize;
        fNumBytesToStream -= fFrameSize;

        // Set the 'presentation time':
        if (fPlayTimePerFrame > 0 && fPreferredFrameSize > 0) {
            if (fPresentationTime.tv_sec == 0 && fPresentationTime.tv_usec == 0) {
                // This is the first frame, so use the current time:
                gettimeofday(&fPresentationTime, NULL);
            } else {
                // Increment by the play time of the previous data:
                unsigned uSeconds	= fPresentationTime.tv_usec + fLastPlayTime;
                fPresentationTime.tv_sec += uSeconds/1000000;
                fPresentationTime.tv_usec = uSeconds%1000000;
            }

            // Remember the play time of this data:
            fLastPlayTime = (fPlayTimePerFrame*fFrameSize)/fPreferredFrameSize;
            fDurationInMicroseconds = fLastPlayTime;
        } else {
            // We don't know a specific play time duration for this data,
            // so just record the current time as being the 'presentation time':
            gettimeofday(&fPresentationTime, NULL);
        }
        
    }
    // Inform the downstream object that it has data:
    FramedSource::afterGetting(this);
    
}

#endif

TaskScheduler* scheduler;
UsageEnvironment * env;
RTPSink* videoSink;
H264VideoStreamFramer* videoSource;

void afterPlaying(void*) {
    *env << "...done reading from file\n";
    videoSink->stopPlaying();
    Medium::close(videoSource);
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

void play() {
    // Open the input file as a 'byte-stream file source':
    // ByteStreamFileSource* fileSource = ByteStreamFileSource::createNew(*env, FIFO_PATH);
    RedefineByteStreamMemoryBufferSource* fileSource = 
        RedefineByteStreamMemoryBufferSource::createNew(*env,True);

    FramedSource* videoES = fileSource;

    // Create a framer for the Video Elementary Stream:
    // videoSource = H264VideoStreamFramer::createNew(*env, videoES,True,True);
    videoSource = H264VideoStreamFramer::createNew(*env, videoES);

    // Finally, start playing:
    *env << "Beginning to read from file...\n";
    videoSink->startPlaying(*videoSource, afterPlaying, videoSink);
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
    ServerMediaSession* sms = ServerMediaSession::createNew(*env, "media", "read from queue",
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
    // fifo_open();
    create_live555_multicast();
    return 0;
}