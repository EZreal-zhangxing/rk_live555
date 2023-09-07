cmake_minimum_required(VERSION 3.10)

project(rtspApp)

set(CMAKE_CXX_COMPILER /usr/bin/g++)
set(CMAKE_C_COMPILER /usr/bin/gcc)
set(CXX_STANDARD 11)

message(STATUS "Source_dir:${SOURCE_DIR}")
message(STATUS "Project_source_dir: ${PROJECT_SOURCE_DIR}")

find_package(OpenCV REQUIRED)

message(STATUS "OpenCV library status:")
message(STATUS "    config: ${OpenCV_DIR}")
message(STATUS "    version: ${OpenCV_VERSION}")
message(STATUS "    libraries: ${OpenCV_LIBS}")
message(STATUS "    include path: ${OpenCV_INCLUDE_DIRS}")

set(SYS_INCLUDE /usr/local/include/)
set(SYS_LIB /usr/local/lib/)
set(LIVE555_INCLUDE /usr/local/live555/include/)
set(LIVE555_LIB /usr/local/live555/lib/)

message(STATUS "Live555: ${LIVE555_INCLUDE}")

include_directories(./include ${OpenCV_DIR} ${SYS_INCLUDE} ${LIVE555_INCLUDE}/BasicUsageEnvironment/
    ${LIVE555_INCLUDE}/groupsock/ ${LIVE555_INCLUDE}/liveMedia/ ${LIVE555_INCLUDE}/UsageEnvironment/)

# ffmpeg 编码并发送由Opencv捕获的数据 ,封装成drm 送个编码器编码并发送
add_executable(rtsp_send_opencv_ffmpeg ${PROJECT_SOURCE_DIR}/src/rtsp_with_ffmpeg_using_opencv.cpp)

# ffmpeg 发送由Mpp编码的从Opencv捕获的数据(yuv)
add_executable(rtsp_send_opencv_mpp ${PROJECT_SOURCE_DIR}/src/rtsp_with_mpp_using_opencv.cpp)

# ffmpeg 发送由Mpp编码的从Opencv捕获的数据(rgb)
add_executable(rtsp_send_opencv_mpp_rgb ${PROJECT_SOURCE_DIR}/src/rtsp_with_mpp_using_opencv_rgb.cpp)

# drm 显示测试
add_executable(drm_test ${PROJECT_SOURCE_DIR}/src/drm_test.cpp)

# drm 读取图片并显示测试
add_executable(drm_image_test ${PROJECT_SOURCE_DIR}/src/drm_show_image.cpp)

# live rtsp服务
add_executable(rtsp_service ${PROJECT_SOURCE_DIR}/src/rtsp_service.cpp)

# live555 发送由mpp编码的rgb数据
add_executable(rtsp_send_opencv_mpp_rgb_live555 ${PROJECT_SOURCE_DIR}/src/write_packet_to_fifo.cpp 
    ${PROJECT_SOURCE_DIR}/src/rtsp_with_mpp_using_opencv_live555_rgb.cpp)

# live555 发送由mpp编码的yuv数据
add_executable(rtsp_send_opencv_mpp_yuv_live555 ${PROJECT_SOURCE_DIR}/src/write_packet_to_fifo.cpp 
    ${PROJECT_SOURCE_DIR}/src/rtsp_with_mpp_using_opencv_live555.cpp)

# live555 发送由mpp编码的yuv数据 集成组播和单播服务于一体
add_executable(rtsp_send_opencv_mpp_yuv_live555_server ${PROJECT_SOURCE_DIR}/src/write_packet_to_fifo.cpp 
    ${PROJECT_SOURCE_DIR}/src/rtsp_with_mpp_using_opencv_live555_server.cpp)

# live555 客户端
add_executable(rtsp_live555_client
# ${PROJECT_SOURCE_DIR}/src/rtsp_connect.cpp 
${PROJECT_SOURCE_DIR}/src/rtsp_connect_discrete.cpp 
# ${PROJECT_SOURCE_DIR}/src/ffmpeg_decode.cpp  
# ${PROJECT_SOURCE_DIR}/src/mpp_decoder.cpp  
${PROJECT_SOURCE_DIR}/src/mpp_decoder_discrete.cpp  
${PROJECT_SOURCE_DIR}/src/rtsp_client.cpp)

# fifo 读取测试
# add_executable(fifo_test ${PROJECT_SOURCE_DIR}/src/write_packet_to_fifo.cpp ${PROJECT_SOURCE_DIR}/src/fifo_test.cpp)

target_link_libraries(rtsp_send_opencv_ffmpeg 
-lavformat -lavcodec -lavutil -lswscale -lavdevice -lz -lpthread -lswresample -llzma -lm ${OpenCV_LIBS} -lrockchip_mpp)

target_link_libraries(rtsp_send_opencv_mpp 
-lavformat -lavcodec -lavutil -lswscale -lavdevice -lz -lpthread -lswresample -llzma -lm ${OpenCV_LIBS} -lrockchip_mpp)

target_link_libraries(rtsp_send_opencv_mpp_rgb
-lavformat -lavcodec -lavutil -lswscale -lavdevice -lz -lpthread -lswresample -llzma -lm ${OpenCV_LIBS} -lrockchip_mpp)

target_link_libraries(drm_test -ldrm -lpthread)

target_link_libraries(drm_image_test -ldrm ${OpenCV_LIBS})

target_link_libraries(rtsp_service 
-lliveMedia -lBasicUsageEnvironment -lgroupsock -lUsageEnvironment 
-lssl -lcrypto)

target_link_libraries(rtsp_send_opencv_mpp_rgb_live555 
-lavformat -lavcodec -lavutil -lswscale -lavdevice -lz -lpthread -lswresample -llzma -lm ${OpenCV_LIBS} -lrockchip_mpp
-lliveMedia -lBasicUsageEnvironment -lgroupsock -lUsageEnvironment 
-lssl -lcrypto)

target_link_libraries(rtsp_send_opencv_mpp_yuv_live555 
-lavformat -lavcodec -lavutil -lswscale -lavdevice 
-lz -lpthread -lswresample -llzma -lm ${OpenCV_LIBS} -lrockchip_mpp
-lliveMedia -lBasicUsageEnvironment -lgroupsock -lUsageEnvironment 
-lssl -lcrypto)


target_link_libraries(rtsp_live555_client 
-lavformat -lavcodec -lavutil -lswscale -lavdevice
-lSDL2 -lSDL2_ttf
-lrockchip_mpp -lpthread -ldrm
-lliveMedia -lBasicUsageEnvironment -lgroupsock -lUsageEnvironment 
-lssl -lcrypto)


target_link_libraries(rtsp_send_opencv_mpp_yuv_live555_server 
-lavformat -lavcodec -lavutil -lswscale -lavdevice 
-lz -lpthread -lswresample -llzma -lm ${OpenCV_LIBS} -lrockchip_mpp
-lliveMedia -lBasicUsageEnvironment -lgroupsock -lUsageEnvironment 
-lssl -lcrypto)

# target_link_libraries(fifo_test
# -lavcodec
# -lliveMedia -lBasicUsageEnvironment -lgroupsock -lUsageEnvironment 
# -lssl -lcrypto)