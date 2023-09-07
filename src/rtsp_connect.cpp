#include "rtsp_connect.h"
#include "mpp_decoder.h"
extern "C"{
#include <libavutil/time.h>
}
// #include "ffmpeg_decode.h"
#include "qos_measure.h"


/*StreamClientState Implementation*/
StreamClientState::StreamClientState() 
    : iter(NULL), session(NULL), subsession(NULL), streamTimerTask(NULL), duration(0.0) {
}

StreamClientState::~StreamClientState() {
    delete iter;
    if (session != NULL) {
        // We also need to delete "session", and unschedule "streamTimerTask" (if set)
        UsageEnvironment& env = session->envir(); // alias
        env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
        Medium::close(session);
    }
}
/*ourRTSPClient Implementation*/
ourRTSPClient* ourRTSPClient::createNew(UsageEnvironment& env, char const* rtspURL,int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum) {
    return new ourRTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum);
}

ourRTSPClient::ourRTSPClient(UsageEnvironment& env, char const* rtspURL,int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum)
    :RTSPClient(env,rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1) {
}

ourRTSPClient::~ourRTSPClient() {
}

/*DummySink Implementation*/
DummySink* DummySink::createNew(UsageEnvironment& env, MediaSubsession& subsession,
    SPropRecord * sPropRecords,unsigned int numsPropRecords,RTSPClient * client,char const* streamId) {
    return new DummySink(env, subsession, streamId,sPropRecords,numsPropRecords,client);
}

DummySink::DummySink(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId,SPropRecord * sPropRecords,
    unsigned int numsPropRecords,RTSPClient * client)
: MediaSink(env),fSubsession(subsession),sPropRecords(sPropRecords),
    numsPropRecords(numsPropRecords),is_exist_head(false),stop_stream(false),hockRtspClient(client){
    fStreamId = strDup(streamId);
    fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
    buffer = (uint8_t *)malloc(BUFFER_SIZE_TO_SAVE);
    create_h264_head(sPropRecords);
    init_decoder(); // 初始化解码器
    // // start_thread_decode(); // 开启解码线程
    // start_thread_decode_get_frame();
    start_thread(this);
    

}

DummySink::~DummySink() {
    delete[] fReceiveBuffer;
    delete[] fStreamId;
    delete[] head;
    delete[] sPropRecords;
}

// If you don't want to see debugging output for each received frame, then comment out the following line:
#define DEBUG_PRINT_EACH_RECEIVED_FRAME 1
#define DEBUG_PRINT_NPT 1
static unsigned iframeNum = 0;
void DummySink::receive_test(void * clientData,unsigned frameSize,struct timeval presentationTime){
    DummySink* sink = (DummySink*)clientData;
    is_i_frame(clientData,frameSize);
    unsigned char * ptr = (unsigned char *)sink->fReceiveBuffer;
    int lines = 0;
    char temp[20];
    sprintf(temp,"first bytes [%2x] \t",ptr[0]);
    envir() << temp;
    if((ptr[0] & 0x000000ff) == 101){
        char ttemp[100];

        sprintf(ttemp,"it's I frame [%d] [%6lu] - [%6lu] ",++iframeNum,presentationTime.tv_sec,presentationTime.tv_usec);
        envir() << ttemp;
    }

#ifdef DEBUG_PRINT_EACH_RECEIVED_FRAME
    if (fStreamId != NULL) envir() << "Stream \"" << fStreamId << "\"; ";
    envir() << fSubsession.mediumName() << "/" << fSubsession.codecName() << ":\tReceived " << frameSize << " bytes";
    char uSecsStr[6+1]; // used to output the 'microseconds' part of the presentation time
    if (fSubsession.rtpSource() != NULL && !fSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP()) {
        envir() << "\tdo not has been synchronized!"; // mark the debugging output to indicate that this presentation time is not RTCP-synchronized
    }
#ifdef DEBUG_PRINT_NPT
    envir() << "\tNPT: " << fSubsession.getNormalPlayTime(presentationTime);
#endif
    envir() << "\n";
#endif
    continuePlaying();
}

void DummySink::afterGettingFrame(void* clientData, unsigned frameSize, unsigned numTruncatedBytes,
    struct timeval presentationTime, unsigned durationInMicroseconds) {
    
    DummySink* sink = (DummySink*)clientData;
    // sink->get_recive_buffer(sink->fReceiveBuffer);
    // sink->smart_concat_and_send(clientData,frameSize,presentationTime);
    sink->concat_buffer_and_send(clientData,frameSize,presentationTime);
   
    // sink->sendBufferdata(clientData,frameSize,presentationTime);
    // sink->receive_test(clientData,frameSize,presentationTime);
    // sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
    
    
}


void DummySink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
    struct timeval presentationTime, unsigned /*durationInMicroseconds*/) {
  // We've just received a frame of data.  (Optionally) print out information about it:
#ifdef DEBUG_PRINT_EACH_RECEIVED_FRAME
    if (fStreamId != NULL) envir() << "Stream \"" << fStreamId << "\"; ";
    envir() << fSubsession.mediumName() << "/" << fSubsession.codecName() << ":\tReceived " << frameSize << " bytes";
    if (numTruncatedBytes > 0) envir() << " (with " << numTruncatedBytes << " bytes truncated)";
    char uSecsStr[6+1]; // used to output the 'microseconds' part of the presentation time
    sprintf(uSecsStr, "%06u", (unsigned)presentationTime.tv_usec);
    envir() << ".\tPresentation time: " << (int)presentationTime.tv_sec << "." << uSecsStr;
    if (fSubsession.rtpSource() != NULL && !fSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP()) {
        envir() << "!"; // mark the debugging output to indicate that this presentation time is not RTCP-synchronized
    }
#ifdef DEBUG_PRINT_NPT
    envir() << "\tNPT: " << fSubsession.getNormalPlayTime(presentationTime);
#endif
    envir() << "\n";
#endif
    // Then continue, to request the next frame of data:
    // printf("buffer_size += frame Size == %d \n",buffer_size); 
    continuePlaying();
}

/**
 * TODO: 将获取的数据写入管道交给 MPP读取
*/
void DummySink::create_h264_head(SPropRecord * sPropRecords){
    unsigned char nalu_header[4] = { 0, 0, 0, 1 }; // 起始码
    // unsigned char nalu_header[4] = { 1, 0, 0, 0 }; // 起始码
    SPropRecord &sps = sPropRecords[0];
    SPropRecord &pps = sPropRecords[1];
    headLens = 8 + sps.sPropLength + pps.sPropLength + 4;
    head = (unsigned char * )malloc(headLens); //视频头的信息 nalu/sps/nalu/pps
    memcpy(head ,nalu_header ,4);
    memcpy(head + 4,sps.sPropBytes,sps.sPropLength);
    memcpy(head + 4 + sps.sPropLength,nalu_header,4);
    memcpy(head + 4 + sps.sPropLength + 4,pps.sPropBytes ,pps.sPropLength);
    memcpy(head + 4 + sps.sPropLength + 4 + pps.sPropLength,nalu_header,4);
    delete[] sPropRecords;
}

/**
 * 检查是否带有SPS或者PPS头
*/
int DummySink::check_head(void * clientData,unsigned index){
    DummySink* sink = (DummySink*)clientData;
    SPropRecord &sps = sPropRecords[index];
    int i = memcmp(sink->fReceiveBuffer,sps.sPropBytes,sps.sPropLength);
    return i;
}
int DummySink::is_i_frame(void * clientData,unsigned buffer_size){
    DummySink* sink = (DummySink*)clientData;
    if((sink->fReceiveBuffer[0] & 0x000000ff) == 101){ // 0x65
        return 1;
    }
    return 0;
}
int64_t* DummySink::get_packet_time(void * clientData,unsigned offset){
    // printf("the packet first byte: %2x \n",sink->fReceiveBuffer[0]);
    // if((sink->fReceiveBuffer[0] & 0x000000ff) == 101){
    //     return 1;
    // }
    unsigned char * ptr = (unsigned char *)clientData;
    
    return NULL;
}
void display_info(void *buffer, unsigned size,struct timeval preNodeOfTime,struct timeval presentationTime){
    printf("packet preNode %lu presentation Time %lu\n ",
        (preNodeOfTime.tv_sec * 1000000 + preNodeOfTime.tv_usec),(presentationTime.tv_sec * 1000000 + presentationTime.tv_usec));
    unsigned char * ptr = (unsigned char *)buffer;
    int lines = 0;
    for(int i=0;i<size;i++){
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

int packet_time_create = 0;
int64_t* packet_time = NULL;
void DummySink::concat_buffer_and_send(void* clientData,unsigned buffer_size,struct timeval presentationTime){
    if(!packet_time_create){
        packet_time = get_packet_time(clientData,buffer_size);
        packet_time_create = 1;
    }
    DummySink * sink = (DummySink *)clientData;
    if(stop_stream){
        sink->stopPlaying();
        Medium::close(sink);
        return;
    }
    
    RTSPReceiveData *receive;
    unsigned char nalu_header[4] = { 0, 0, 0, 1 }; // 起始码
    char temp[50];
    if(size == 0){
        preNodeOfTime = presentationTime;
        memcpy(buffer + size, nalu_header, 4);
        size += 4;
        memcpy(buffer + size,sink->fReceiveBuffer,buffer_size);
        size += buffer_size;
        // printf(" set first packet %d \n",size);
        count +=1;
        printf("frame [%u] bytes %2x \n",count,sink->fReceiveBuffer[0]);
    }else{
        if(presentationTime.tv_sec == preNodeOfTime.tv_sec &&
            presentationTime.tv_usec == preNodeOfTime.tv_usec){
            memcpy(buffer + size, nalu_header, 4);
            size += 4;
            memcpy(buffer + size,sink->fReceiveBuffer,buffer_size);
            size += buffer_size;
            count +=1;
            printf("frame [%u] bytes %2x \n",count,sink->fReceiveBuffer[0]);    
        }else{
            receive = new RTSPReceiveData(buffer,size,preNodeOfTime);
            // printf(" send data %d %ld-%ld\n",size,preNodeOfTime.tv_sec,preNodeOfTime.tv_usec);
            // send_data(receive);
            if(packet_time != NULL){
                receive->create_time = packet_time[0];
                receive->arrive_time = packet_time[1];
                free(packet_time);
            }else{
                receive->create_time = 0;
                receive->arrive_time = presentationTime.tv_sec * 1000000 + presentationTime.tv_usec;
            }
            packet_time = NULL;
            packet_time_create = 0;
            receive->send_memory_time = av_gettime();

            push_data_2_queue(receive);
            memset(buffer,0,size);
            size = 0;
            count = 0;
            // concat_buffer_and_send(clientData,buffer_size,presentationTime);

            preNodeOfTime = presentationTime;
            memcpy(buffer + size, nalu_header, 4);
            size += 4;
            memcpy(buffer + size,sink->fReceiveBuffer,buffer_size);
            size += buffer_size;
            count +=1;
            printf("first packet frame [%u] bytes %2x \n",count,sink->fReceiveBuffer[0]);
        }
    }
    // if (fSubsession.rtpSource() != NULL && !fSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP()) {
    //     printf(" do not has been synchronized! \n");
    // }
    
    continuePlaying();
}

// int64_t* pre_packet_time = (int64_t*)malloc(sizeof(int64_t) * 2);

// void DummySink::smart_concat_and_send(void* clientData,unsigned buffer_size,struct timeval presentationTime){
//     DummySink * sink = (DummySink *)clientData;
//     // display_info(sink->fReceiveBuffer,buffer_size,preNodeOfTime,presentationTime);
//     if(stop_stream){
//         Medium::close(sink);
//         return;
//     }
//     RTSPReceiveData * data = NULL;
//     unsigned char nalu_header[4] = { 0, 0, 0, 1 }; // 起始码
//     if((sink->fReceiveBuffer[0] & 0x000000ff) == 6 || (sink->fReceiveBuffer[0] & 0x000000ff) == 101){
//         // 关键帧
//         memcpy(buffer,nalu_header,4);
//         size += 4;
//         memcpy(buffer,sink->fReceiveBuffer,buffer_size);
//         size += buffer_size;
//         if((sink->fReceiveBuffer[0] & 0x000000ff) == 101){
//             data = new RTSPReceiveData(buffer,size,presentationTime);
//             data->is_key_frame = 1;
//             push_data_2_queue(data);
//         }
//     }else{
//         if((sink->fReceiveBuffer[0] & 0x000000ff) == 104){//0x68
//             // PPS
//             memcpy(&pre_packet_time[0],sink->fReceiveBuffer + 5 + 8,8);
//             int64_t now_time = av_gettime();
//             pre_packet_time[1] = now_time;
//         }
//         if((sink->fReceiveBuffer[0] & 0x000000ff) == 65){//0x41
//             data = new RTSPReceiveData(sink->fReceiveBuffer,buffer_size,presentationTime);
//             data->create_time = pre_packet_time[0];
//             data->arrive_time = pre_packet_time[1];
//             push_data_2_queue(data);
            
//         }
//     }
//     continuePlaying();
// }


void DummySink::sendBufferdata(void* clientData,unsigned buffer_size,struct timeval presentationTime){
    DummySink* sink = (DummySink*)clientData;
    RTSPReceiveData *receive;
    unsigned char nalu_header[4] = { 0, 0, 0, 1 }; // 起始码
    // unsigned size = buffer_size + 4;
    // unsigned char *  temp = (unsigned char * )malloc(size); // 添加起始码的NAL-U
    // memcpy(temp,nalu_header,4);
    // memcpy(temp + 4,sink->fReceiveBuffer,buffer_size);
    // receive = RTSPReceiveData(temp,size,presentationTime);
    printf("receive buffer_Size %u \n",buffer_size);
    unsigned char * ptr = (unsigned char *)sink->fReceiveBuffer;
    int lines = 0;
    for(int i=0;i<buffer_size;i++){
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
    int64_t* time = (int64_t*)malloc(sizeof(int64_t) * 2);
    memset(time,0,sizeof(int64_t));
    if(buffer_size == 27 || buffer_size == 29){
        if(buffer_size == 29){
            memcpy(&time[0],sink->fReceiveBuffer + 13,8);
            int64_t now_time = av_gettime();
            time[1] = now_time;
        }
    }else{
        unsigned size;
        unsigned char *  temp;
        // is_exist_head = true;
        // is_i_frame(clientData);
        if(!is_exist_head){
            size = buffer_size + headLens;
            temp = (unsigned char * )malloc(size); // 添加起始码的NAL-U
            memcpy(temp ,head ,headLens);
            memcpy(temp + headLens,sink->fReceiveBuffer,buffer_size);
            is_exist_head = true;
        }else{
            size = buffer_size + 4;
            temp = (unsigned char * )malloc(size); // 添加起始码的NAL-U
            memcpy(temp,nalu_header,4);
            memcpy(temp + 4,sink->fReceiveBuffer,buffer_size);
        }
        receive = new RTSPReceiveData(temp,size,presentationTime);
        receive->create_time = time[0];
        receive->arrive_time = time[1];
        push_data_2_queue(receive);
    }
    continuePlaying();
}

RTSPReceiveData * DummySink::get_head(){
    RTSPReceiveData * data = new RTSPReceiveData();
    data->dataPtr = head;
    data->size = headLens;
    return data;
}

Boolean DummySink::continuePlaying() {
    if (fSource == NULL) return False; // sanity check (should not happen)

    // Request the next frame of data from our input source.  "afterGettingFrame()" will get called later, when it arrives:
    fSource->getNextFrame(fReceiveBuffer, DUMMY_SINK_RECEIVE_BUFFER_SIZE,
        afterGettingFrame, this,onSourceClosure, this);
    return True;
}

#define DEBUG_QUEUE 0
void DummySink::push_data_2_queue(RTSPReceiveData *data){
#if DEBUG_QUEUE
    printf("push data [%d,%lu]  \n",size,data->create_time);
    // printf("push data [%d,%ld-%ld]  to queue size [%lu] \n",
    //     size,data->presentationTime.tv_sec,
    //     data->presentationTime.tv_usec,receive_queue.size());
#endif
    receive_queue.push(data);
    // if(tail == NULL || head == NULL){
    //     tail = data;
    //     head = data;
    // }else{
    //     tail->next = data;
    //     tail = data;
    // }
}

RTSPReceiveData * DummySink::pop_data(int deep = 0){
    std::shared_ptr<RTSPReceiveData *> temp = receive_queue.tryPop();
    return temp ? *temp.get() : NULL;
    // RTSPReceiveData * temp = NULL;
    // if(head == tail){
    //     temp = head;
    //     head = NULL;
    //     tail = NULL;
    // }else{
    //     temp = head;
    //     head = head->next;
    // }
    // return temp;
//     if(!receive_queue.empty()){
//         RTSPReceiveData * temp = receive_queue.front();
//         receive_queue.pop();
#if DEBUG_QUEUE
    printf("pop data out deep [%d,%lu] \n",deep,temp->create_time);
#endif
//         return temp;
//     }else{
//         usleep(1000*1);
//         if(deep > 3){
//             return NULL;
//         }else{
//             return pop_data(++deep);
//         }
//     }
    // return temp ? temp : NULL;
}

/**
 * 自定义H264VideoRTPSink 实现
*/

SELF_H264VideoRTPSink::SELF_H264VideoRTPSink(UsageEnvironment& env,char const* StreamId,MediaSubsession& subsession, Groupsock* RTPgs, unsigned char rtpPayloadFormat,
		   u_int8_t const* sps, unsigned spsSize, u_int8_t const* pps, unsigned ppsSize)
  : H264or5VideoRTPSink(264, env, RTPgs, rtpPayloadFormat,
			NULL, 0, sps, spsSize, pps, ppsSize),fSubsession(subsession) {
    fStreamId = strDup(StreamId);
    // fSubsession = subsession;
    fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
}

SELF_H264VideoRTPSink::~SELF_H264VideoRTPSink() {
}

SELF_H264VideoRTPSink* SELF_H264VideoRTPSink::createNew(UsageEnvironment& env,char const* StreamId,
    MediaSubsession& subsession, Groupsock* RTPgs, unsigned char rtpPayloadFormat,
	    char const* sPropParameterSetsStr) {
    u_int8_t* sps = NULL; unsigned spsSize = 0;
    u_int8_t* pps = NULL; unsigned ppsSize = 0;

    unsigned numSPropRecords;
    SPropRecord* sPropRecords = parseSPropParameterSets(sPropParameterSetsStr, numSPropRecords);
    for (unsigned i = 0; i < numSPropRecords; ++i) {
        if (sPropRecords[i].sPropLength == 0) continue; // bad data
        u_int8_t nal_unit_type = (sPropRecords[i].sPropBytes[0])&0x1F;
        if (nal_unit_type == 7/*SPS*/) {
            sps = sPropRecords[i].sPropBytes;
            spsSize = sPropRecords[i].sPropLength;
        } else if (nal_unit_type == 8/*PPS*/) {
            pps = sPropRecords[i].sPropBytes;
            ppsSize = sPropRecords[i].sPropLength;
        }
    }

    SELF_H264VideoRTPSink* result = new SELF_H264VideoRTPSink(env,StreamId,subsession,RTPgs, rtpPayloadFormat, sps, spsSize, pps, ppsSize);
    delete[] sPropRecords;

    return result;
}

Boolean SELF_H264VideoRTPSink::sourceIsCompatibleWithUs(MediaSource& source) {
    // Our source must be an appropriate framer:
    return source.isH264VideoStreamFramer();
}

char const* SELF_H264VideoRTPSink::auxSDPLine() {
    // Generate a new "a=fmtp:" line each time, using our SPS and PPS (if we have them),
    // otherwise parameters from our framer source (in case they've changed since the last time that
    // we were called):
    H264or5VideoStreamFramer* framerSource = NULL;
    u_int8_t* vpsDummy = NULL; unsigned vpsDummySize = 0;
    u_int8_t* sps = fSPS; unsigned spsSize = fSPSSize;
    u_int8_t* pps = fPPS; unsigned ppsSize = fPPSSize;
    if (sps == NULL || pps == NULL) {
        // We need to get SPS and PPS from our framer source:
        if (fOurFragmenter == NULL) return NULL; // we don't yet have a fragmenter (and therefore not a source)
        framerSource = (H264or5VideoStreamFramer*)(fOurFragmenter->inputSource());
        if (framerSource == NULL) return NULL; // we don't yet have a source

        framerSource->getVPSandSPSandPPS(vpsDummy, vpsDummySize, sps, spsSize, pps, ppsSize);
        if (sps == NULL || pps == NULL) return NULL; // our source isn't ready
    }

    // Set up the "a=fmtp:" SDP line for this stream:
    u_int8_t* spsWEB = new u_int8_t[spsSize]; // "WEB" means "Without Emulation Bytes"
    unsigned spsWEBSize = removeH264or5EmulationBytes(spsWEB, spsSize, sps, spsSize);
    if (spsWEBSize < 4) { // Bad SPS size => assume our source isn't ready
        delete[] spsWEB;
        return NULL;
    }
    u_int32_t profileLevelId = (spsWEB[1]<<16) | (spsWEB[2]<<8) | spsWEB[3];
    delete[] spsWEB;

    char* sps_base64 = base64Encode((char*)sps, spsSize);
    char* pps_base64 = base64Encode((char*)pps, ppsSize);

    char const* fmtpFmt =
        "a=fmtp:%d packetization-mode=1"
        ";profile-level-id=%06X"
        ";sprop-parameter-sets=%s,%s\r\n";
    unsigned fmtpFmtSize = strlen(fmtpFmt)
        + 3 /* max char len */
        + 6 /* 3 bytes in hex */
        + strlen(sps_base64) + strlen(pps_base64);
    char* fmtp = new char[fmtpFmtSize];
    sprintf(fmtp, fmtpFmt,
            rtpPayloadType(),
            profileLevelId,
            sps_base64, pps_base64);

    delete[] sps_base64;
    delete[] pps_base64;

    delete[] fFmtpSDPLine; fFmtpSDPLine = fmtp;
    return fFmtpSDPLine;
}

void SELF_H264VideoRTPSink::test(){
    printf("at here \n");
}

void SELF_H264VideoRTPSink::afterGettingFrame(void* clientData, unsigned frameSize, unsigned numTruncatedBytes,
    struct timeval presentationTime, unsigned durationInMicroseconds) {
    
    SELF_H264VideoRTPSink* sink = (SELF_H264VideoRTPSink*)clientData;
    // sink->get_recive_buffer(sink->fReceiveBuffer);
    sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
    sink->test();
    
}
Boolean SELF_H264VideoRTPSink::continuePlaying() {
	if (fSource == NULL) return False; // sanity check (should not happen)
	fSource->getNextFrame(fReceiveBuffer, DUMMY_SINK_RECEIVE_BUFFER_SIZE,
		afterGettingFrame, this,
		onSourceClosure, this);
	return True;
}
void test_2(){
    printf("at here 2\n");
}
void SELF_H264VideoRTPSink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
    struct timeval presentationTime, unsigned durationInMicroseconds){
    test_2();
    continuePlaying();
}

/**
 * 重载运算符，输出需要的信息
*/
// A function that outputs a string that identifies each stream (for debugging output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env, const RTSPClient& rtspClient) {
    return env << "[URL:\"" << rtspClient.url() << "\"]: ";
}

// A function that outputs a string that identifies each subsession (for debugging output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env, const MediaSubsession& subsession) {
    return env << subsession.mediumName() << "/" << subsession.codecName();
}

void rtsp_connect::openURL(UsageEnvironment& env, char const* progName, char const* rtspURL){
    // Begin by creating a "RTSPClient" object.  Note that there is a separate "RTSPClient" object for each stream that we wish
    // to receive (even if more than stream uses the same "rtsp://" URL).
    RTSPClient* rtspClient = ourRTSPClient::createNew(env, rtspURL, RTSP_CLIENT_VERBOSITY_LEVEL, progName);
    if (rtspClient == NULL) {
        env << "Failed to create a RTSP client for URL \"" << rtspURL << "\": " << env.getResultMsg() << "\n";
        return;
    }

    ++rtspClientCount;

    // Next, send a RTSP "DESCRIBE" command, to get a SDP description for the stream.
    // Note that this command - like all RTSP commands - is sent asynchronously; we do not block, waiting for a response.
    // Instead, the following function call returns immediately, and we handle the RTSP response later, from within the event loop:
    rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
}

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) {
    do {
        UsageEnvironment & env = rtspClient->envir(); // alias
        StreamClientState & scs = ((ourRTSPClient*)rtspClient)->scs; // alias

        if (resultCode != 0) {
            env << *rtspClient << "Failed to get a SDP description: " << resultString << "\n";
            delete[] resultString;
            break;
        }

        char* const sdpDescription = resultString;
        env << *rtspClient << "Got a SDP description:\n" << sdpDescription << "\n";

        // Create a media session object from this SDP description:
        scs.session = MediaSession::createNew(env, sdpDescription);
        delete[] sdpDescription; // because we don't need it anymore
        if (scs.session == NULL) {
            env << *rtspClient << "Failed to create a MediaSession object from the SDP description: " << env.getResultMsg() << "\n";
            break;
        } else if (!scs.session->hasSubsessions()) {
            env << *rtspClient << "This session has no media subsessions (i.e., no \"m=\" lines)\n";
            break;
        }
        
        // Then, create and set up our data source objects for the session.  We do this by iterating over the session's 'subsessions',
        // calling "MediaSubsession::initiate()", and then sending a RTSP "SETUP" command, on each one.
        // (Each 'subsession' will have its own data source.)
        scs.iter = new MediaSubsessionIterator(*scs.session);
        setupNextSubsession(rtspClient);
        return;
    } while (0);
    // An unrecoverable error occurred with this stream.
    shutdownStream(rtspClient);
}

void continueAfterSETUP2(RTSPClient* rtspClient, int resultCode, char* resultString) {
    do {
        UsageEnvironment& env = rtspClient->envir(); // alias
        StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

        if (resultCode != 0) {
            env << *rtspClient << "Failed to set up the \"" << *scs.subsession << "\" subsession: " << resultString << "\n";
            break;
        }

        env << *rtspClient << "Set up the \"" << *scs.subsession << "\" subsession (";
        if (scs.subsession->rtcpIsMuxed()) {
            env << "client port " << scs.subsession->clientPortNum();
        } else {
            env << "client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum()+1;
        }
        env << ")\n";

        // Having successfully setup the subsession, create a data sink for it, and call "startPlaying()" on it.
        // (This will prepare the data sink to receive data; the actual flow of data from the client won't start happening until later,
        // after we've sent a RTSP "PLAY" command.)
        // FramedSource* videoSource = scs.subsession->readSource();
        // H264VideoStreamFramer* h264FrameStream = H264VideoStreamFramer::createNew(env,videoSource);
        // scs.subsession->sink = h264FrameStream;
        // H264VideoRTPSink::createNew()
        // H264VideoStreamFramer::createNew(env,)
        // plan 1 use this Sink write data to fifo
        unsigned int numSPropRecords = 0;
        SPropRecord * record = parseSPropParameterSets(scs.subsession->fmtp_spropparametersets(),numSPropRecords);
        scs.subsession->sink = DummySink::createNew(env, *scs.subsession,record,numSPropRecords,rtspClient,rtspClient->url());
        // scs.subsession->sink = DummySink::createNew(env, *scs.subsession, rtspClient->url());
        // perhaps use your own custom "MediaSink" subclass instead
        if (scs.subsession->sink == NULL) {
            env << *rtspClient << "Failed to create a data sink for the \"" << *scs.subsession
            << "\" subsession: " << env.getResultMsg() << "\n";
            break;
        }

        env << *rtspClient << "Created a data sink for the \"" << *scs.subsession << "\" subsession\n";
        scs.subsession->miscPtr = rtspClient; // a hack to let subsession handler functions get the "RTSPClient" from the subsession 

        scs.subsession->sink->startPlaying(*(scs.subsession->readSource()),
                        subsessionAfterPlaying, scs.subsession);
        // Also set a handler to be called if a RTCP "BYE" arrives for this subsession:
        if (scs.subsession->rtcpInstance() != NULL) {
            scs.subsession->rtcpInstance()->setByeWithReasonHandler(subsessionByeHandler, scs.subsession);
        }
    } while (0);
    delete[] resultString;

    // Set up the next subsession, if any:
    setupNextSubsession(rtspClient);
}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) {
    do {
        UsageEnvironment& env = rtspClient->envir(); // alias
        StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

        if (resultCode != 0) {
            env << *rtspClient << "Failed to set up the \"" << *scs.subsession << "\" subsession: " << resultString << "\n";
            break;
        }

        env << *rtspClient << "Set up the \"" << *scs.subsession << "\" subsession (";
        if (scs.subsession->rtcpIsMuxed()) {
            env << "client port " << scs.subsession->clientPortNum();
        } else {
            env << "client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum()+1;
        }
        env << ")\n";
        // unsigned const thresh = 1000000; // 1 second
	    // scs.subsession->rtpSource()->setPacketReorderingThresholdTime(thresh);

        // Having successfully setup the subsession, create a data sink for it, and call "startPlaying()" on it.
        // (This will prepare the data sink to receive data; the actual flow of data from the client won't start happening until later,
        // after we've sent a RTSP "PLAY" command.)
        FramedSource* videoSource = scs.subsession->readSource();
        // RTPSource* videoSource = scs.subsession->rtpSource();
        // H264VideoStreamFramer* h264FrameStream = H264VideoStreamFramer::createNew(env,videoSource);
        // scs.subsession->sink = h264FrameStream;
        // H264VideoRTPSink::createNew()
        // H264VideoStreamFramer::createNew(env,)
        // plan 1 use this Sink write data to fifo

        OutPacketBuffer::maxSize = 1920*1080*4;
        // H264VideoStreamFramer * videoStreamFramer = H264VideoStreamFramer::createNew(env,videoSource);
        // H264VideoStreamDiscreteFramer* dframer = H264VideoStreamDiscreteFramer::createNew(env,videoSource);        
        // H264VideoFileSink* videoSink = H264VideoFileSink::createNew(env, "./h264videoFileSink.h264",&(*scs.subsession->fmtp_spropparametersets()),1920*1080*4,true);
        // scs.subsession->sink = videoSink;
        //**SPS & PPS**//
        unsigned int numSPropRecords = 0;
        SPropRecord * record = parseSPropParameterSets(scs.subsession->fmtp_spropparametersets(),numSPropRecords);
        printf("\n***********************head[%d]**********************\n",numSPropRecords);
        for(int j=0;j<numSPropRecords;j++){
            for(int i=0;i<record[j].sPropLength;i++){
                printf("%2x ",record[j].sPropBytes[i]);
                if(i> 0 && i%20 == 0){
                    printf("\n");
                }
            }
            printf("\n*********************************************\n");
        }
        printf("\n**********************head-end***********************\n");
        // H264VideoRTPSource* h264Source =  H264VideoRTPSource::createNew(env,&(*scs.subsession->rtpSource()->RTPgs()),(unsigned char)96);
        // SELF_H264VideoRTPSink * h264RTPSink = SELF_H264VideoRTPSink::createNew(env,rtspClient->url(),*scs.subsession,
        //     &(*scs.subsession->rtpSource()->RTPgs()),
        //     (unsigned char)96,&(*scs.subsession->fmtp_spropparametersets()));
        // MPEG2TransportStreamFromESSource* tsFrames = MPEG2TransportStreamFromESSource::createNew(env);
        // tsFrames->addNewVideoSource(videoSource, 5/*mpegVersion: H.264*/);
        scs.subsession->sink = DummySink::createNew(env, *scs.subsession,record,numSPropRecords,rtspClient,rtspClient->url());
        // scs.subsession->sink = H264VideoFileSink::createNew(env, "./zx.h264",&(*scs.subsession->fmtp_spropparametersets()),1920*1080*4);
        // scs.subsession->sink = h264RTPSink;
        
        // perhaps use your own custom "MediaSink" subclass instead
        if (scs.subsession->sink == NULL) {
            env << *rtspClient << "Failed to create a data sink for the \"" << *scs.subsession
            << "\" subsession: " << env.getResultMsg() << "\n";
            break;
        }

        env << *rtspClient << "Created a data sink for the \"" << *scs.subsession << "\" subsession\n";
        scs.subsession->miscPtr = rtspClient; // a hack to let subsession handler functions get the "RTSPClient" from the subsession 

        // scs.subsession->sink->startPlaying(*tsFrames,subsessionAfterPlaying, scs.subsession);
        scs.subsession->sink->startPlaying(*videoSource,subsessionAfterPlaying, scs.subsession);
        // Also set a handler to be called if a RTCP "BYE" arrives for this subsession:
        if (scs.subsession->rtcpInstance() != NULL) {
            scs.subsession->rtcpInstance()->setByeWithReasonHandler(subsessionByeHandler, scs.subsession);
        }
    } while (0);
    delete[] resultString;

    // Set up the next subsession, if any:
    setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString) {
    
    Boolean success = False;
    do {
        UsageEnvironment& env = rtspClient->envir(); // alias
        StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias
        
        if (resultCode != 0) {
            env << *rtspClient << "Failed to start playing session: " << resultString << "\n";
            break;
        }

        // Set a timer to be handled at the end of the stream's expected duration (if the stream does not already signal its end
        // using a RTCP "BYE").  This is optional.  If, instead, you want to keep the stream active - e.g., so you can later
        // 'seek' back within it and do another RTSP "PLAY" - then you can omit this code.
        // (Alternatively, if you don't want to receive the entire stream, you could set this timer for some shorter value.)
        if (scs.duration > 0) {
            unsigned const delaySlop = 2; // number of seconds extra to delay, after the stream's expected duration.  (This is optional.)
            scs.duration += delaySlop;
            unsigned uSecsToDelay = (unsigned)(scs.duration*1000000);
            scs.streamTimerTask = env.taskScheduler().scheduleDelayedTask(uSecsToDelay, (TaskFunc*)streamTimerHandler, rtspClient);
        }

        env << *rtspClient << "Started playing session";
        if (scs.duration > 0) {
            env << " (for up to " << scs.duration << " seconds)";
        }
        env << "...\n";

        success = True;
    } while (0);
    delete[] resultString;

    if (!success) {
        // An unrecoverable error occurred with this stream.
        shutdownStream(rtspClient);
    }
}

void subsessionAfterPlaying(void* clientData) {
    MediaSubsession* subsession = (MediaSubsession*)clientData;
    RTSPClient* rtspClient = (RTSPClient*)(subsession->miscPtr);

    // Begin by closing this subsession's stream:
    Medium::close(subsession->sink);
    subsession->sink = NULL;

    // Next, check whether *all* subsessions' streams have now been closed:
    MediaSession& session = subsession->parentSession();
    MediaSubsessionIterator iter(session);
    while ((subsession = iter.next()) != NULL) {
        if (subsession->sink != NULL) return; // this subsession is still active
    }

    // All subsessions' streams have now been closed, so shutdown the client:
    shutdownStream(rtspClient);
}

void subsessionByeHandler(void* clientData, char const* reason) {
    MediaSubsession* subsession = (MediaSubsession*)clientData;
    RTSPClient* rtspClient = (RTSPClient*)subsession->miscPtr;
    UsageEnvironment& env = rtspClient->envir(); // alias

    env << *rtspClient << "Received RTCP \"BYE\"";
    if (reason != NULL) {
        env << " (reason:\"" << reason << "\")";
        delete[] (char*)reason;
    }
    env << " on \"" << *subsession << "\" subsession\n";

    // Now act as if the subsession had closed:
    subsessionAfterPlaying(subsession);
}

void streamTimerHandler(void* clientData) {
    ourRTSPClient* rtspClient = (ourRTSPClient*)clientData;
    StreamClientState& scs = rtspClient->scs; // alias

    scs.streamTimerTask = NULL;

    // Shut down the stream:
    shutdownStream(rtspClient);
}

void setupNextSubsession(RTSPClient* rtspClient) {
    UsageEnvironment& env = rtspClient->envir(); // alias
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias
    // 迭代流
    scs.subsession = scs.iter->next();
    if (scs.subsession != NULL) {
        if (!scs.subsession->initiate()) {
            env << *rtspClient << "Failed to initiate the \"" << *scs.subsession << "\" subsession: " << env.getResultMsg() << "\n";
            setupNextSubsession(rtspClient); // give up on this subsession; go to the next one
        } else {
            env << *rtspClient << "Initiated the \"" << *scs.subsession << "\" subsession (";
            if (scs.subsession->rtcpIsMuxed()) {
                env << "client port " << scs.subsession->clientPortNum();
            } else {
                env << "client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum()+1;
            }
            env << ")\n";
            // Continue setting up this subsession, by sending a RTSP "SETUP" command:
            // rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False, REQUEST_STREAMING_OVER_TCP);
            rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False, REQUEST_STREAMING_OVER_TCP,true);
            // struct sockaddr_storage * addr;
            // scs.subsession->getConnectionEndpointAddress(*addr);
            // scs.subsession->setDestinations(*addr);
        }
        return;
    }

    // We've finished setting up all of the subsessions.  Now, send a RTSP "PLAY" command to start the streaming:
    if (scs.session->absStartTime() != NULL) {
        // Special case: The stream is indexed by 'absolute' time, so send an appropriate "PLAY" command:
        rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY, scs.session->absStartTime(), scs.session->absEndTime());
    } else {
        scs.duration = scs.session->playEndTime() - scs.session->playStartTime();
        rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
    }
}

void shutdownStream(RTSPClient* rtspClient, int exitCode) {
    UsageEnvironment& env = rtspClient->envir(); // alias
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

    // First, check whether any subsessions have still to be closed:
    if (scs.session != NULL) { 
        Boolean someSubsessionsWereActive = False;
        MediaSubsessionIterator iter(*scs.session);
        MediaSubsession* subsession;

        while ((subsession = iter.next()) != NULL) {
            if (subsession->sink != NULL) {
                Medium::close(subsession->sink);
                subsession->sink = NULL;

                if (subsession->rtcpInstance() != NULL) {
                    subsession->rtcpInstance()->setByeHandler(NULL, NULL); // in case the server sends a RTCP "BYE" while handling "TEARDOWN"
                }

                someSubsessionsWereActive = True;
            }
        }

        if (someSubsessionsWereActive) {
            // Send a RTSP "TEARDOWN" command, to tell the server to shutdown the stream.
            // Don't bother handling the response to the "TEARDOWN".
            rtspClient->sendTeardownCommand(*scs.session, NULL);
        }
    }

    env << *rtspClient << "Closing the stream.\n";
    Medium::close(rtspClient);
    // Note that this will also cause this stream's "StreamClientState" structure to get reclaimed.

    if (--rtspClientCount == 0) {
        // The final stream has ended, so exit the application now.
        // (Of course, if you're embedding this code into your own application, you might want to comment this out,
        // and replace it with "eventLoopWatchVariable = 1;", so that we leave the LIVE555 event loop, and continue running "main()".)
        exit(exitCode);
    }
}
