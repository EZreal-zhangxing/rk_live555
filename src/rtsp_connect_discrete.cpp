#include "rtsp_connect.h"
#include "mpp_decoder_discrete.h"
extern "C"{
#include <libavutil/time.h>
}
// #include "ffmpeg_decode.h"

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
    numsPropRecords(numsPropRecords),is_exist_head(false),keyFrameData(NULL),stop_stream(false),hockRtspClient(client){
    fStreamId = strDup(streamId);
    fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
    buffer = (uint8_t *)malloc(BUFFER_SIZE_TO_SAVE);
    // create_h264_head(sPropRecords);
    // init_decoder(); // 初始化解码器
    // // start_thread_decode(); // 开启解码线程
    // start_thread_decode_get_frame();
    start_thread(this);
}

DummySink::~DummySink() {
    delete[] fReceiveBuffer;
    delete[] fStreamId;
    // delete[] head;
    free(buffer);
    delete[] sPropRecords;
    delete[] keyFrameData;
}

// If you don't want to see debugging output for each received frame, then comment out the following line:
#define DEBUG_PRINT_EACH_RECEIVED_FRAME 1
#define DEBUG_PRINT_NPT 1
static unsigned iframeNum = 0;
void DummySink::receive_test(void * clientData,unsigned frameSize,struct timeval presentationTime){
    DummySink* sink = (DummySink*)clientData;
    unsigned char * ptr = (unsigned char *)sink->fReceiveBuffer;
    int lines = 0;
    char temp[20];
    sprintf(temp,"first bytes [%2x] \t",ptr[0]);
    envir() << temp;
    for(int i=0;i<frameSize;i++){
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
    // sink->concat_buffer_and_send(clientData,frameSize,presentationTime);
   
    sink->sendBufferdata(clientData,frameSize,presentationTime);
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
int64_t* DummySink::get_packet_time(void * clientData,unsigned buffer_size){
    DummySink* sink = (DummySink*)clientData;
    int64_t* time = (int64_t*)malloc(sizeof(int64_t) * 2);
    memcpy(&time[0],sink->fReceiveBuffer + 48,8);
    int64_t now_time = av_gettime();
    time[1] = now_time;
    // printf("packet time [%ld] ,[%.3f] ms net work time delay, receive_time [%ld] \n",time[0],(now_time - time[0])/1000.0,now_time);
    return time;
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


void DummySink::sendBufferdata(void* clientData,unsigned buffer_size,struct timeval presentationTime){
    DummySink* sink = (DummySink*)clientData;
    int is_start_code = 0;
    char fourBytes[4] = {sink->fReceiveBuffer[0],sink->fReceiveBuffer[1],sink->fReceiveBuffer[2],sink->fReceiveBuffer[3]};
    if(sink->fReceiveBuffer[0] == 0x00 &&
        sink->fReceiveBuffer[1] == 0x00 &&
        sink->fReceiveBuffer[2] == 0x00 &&
        sink->fReceiveBuffer[3] == 0x01){
        is_start_code = 1;
    }
    int fourByteV = sink->fReceiveBuffer[4] & 0x000000ff;
    // display_info(sink->fReceiveBuffer,buffer_size,presentationTime,presentationTime);
    sink->count ++ ;
    if(stop_stream){
        shutdownStream(sink->hockRtspClient);
        printf("shutdownStream \n");
        return;
    }
    if(is_start_code && (fourByteV == 6 || fourByteV == 103)){
        if(sink->keyFrameData != NULL){
            int64_t * ptimes = get_packet_time(clientData,buffer_size);
            sink->keyFrameData->create_time = ptimes[0];
            sink->keyFrameData->arrive_time = ptimes[1];
            sink->keyFrameData->is_key_frame = (fourByteV == 6);
            free(ptimes);
            // RTSPReceiveData * copyKeyFrame = new RTSPReceiveData(sink->keyFrameData->dataPtr,sink->keyFrameData->size,presentationTime);
            push_data_2_queue(sink->keyFrameData);
            // free(sink->keyFrameData);
        }
        RTSPReceiveData *receive = new RTSPReceiveData(sink->fReceiveBuffer,buffer_size,sink->preNodeOfTime);
        sink->keyFrameData = receive;
    }else{
        uint8_t * temp = (uint8_t *)malloc(buffer_size + sink->keyFrameData->size);
        memmove(temp,sink->keyFrameData->dataPtr,sink->keyFrameData->size);
        free(sink->keyFrameData->dataPtr);
        memmove(temp + sink->keyFrameData->size ,sink->fReceiveBuffer,buffer_size);
        sink->keyFrameData->dataPtr = temp;
        sink->keyFrameData->size += buffer_size;
    }
    sink->preNodeOfTime = presentationTime;
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

void DummySink::push_data_2_queue(RTSPReceiveData *data){
    receive_queue.push(data);
    data->send_memory_time = av_gettime();

    // int res = directly_read_data_and_decode(data);
    // int res = read_data_and_decode(data);
    // if(res < 0){
    //     DummySink::stop_stream = 1;
    // }
}

RTSPReceiveData * DummySink::pop_data(int deep = 0){
    std::shared_ptr<RTSPReceiveData *> temp = receive_queue.tryPop();
    return temp ? *temp.get() : NULL;
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
    // RTSPClient::responseBufferSize = 200000;
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
            printf("\n***********************lens[%d]**********************\n",record[j].sPropLength);
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
            rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False, REQUEST_STREAMING_OVER_TCP,True);
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
                // Medium::close(subsession->sink);
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
