// #ifndef RTSP_CONNECT
#include "rtsp_connect.h"
// #endif
// 事件监控值，可以监控这个值确认是否有事件到来
char eventLoopWatchVariable = 0;

int main(int argc,char * argv[]){
    // Begin by setting up our usage environment:
    TaskScheduler* scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);
    rtsp_connect rtsp_connect;
    OutPacketBuffer::maxSize = 8000000;
    // There are argc-1 URLs: argv[1] through argv[argc-1].  Open and start streaming each one:
    for (int i = 1; i <= argc-1; ++i) {
        rtsp_connect.openURL(*env, argv[0], argv[i]);
    }

    // All subsequent activity takes place within the event loop:
    env->taskScheduler().doEventLoop(&eventLoopWatchVariable);
    // This function call does not return, unless, at some point in time, 
    // "eventLoopWatchVariable" gets set to something non-zero.

    return 0;

    // If you choose to continue the application past this point (i.e., if you comment out the "return 0;" statement above),
    // and if you don't intend to do anything more with the "TaskScheduler" and "UsageEnvironment" objects,
    // then you can also reclaim the (small) memory used by these objects by uncommenting the following code:
    /*
        env->reclaim(); env = NULL;
        delete scheduler; scheduler = NULL;
    */
}