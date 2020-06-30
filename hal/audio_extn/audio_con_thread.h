#include <bits/epoll_event.h>
#include <cutils/sockets.h>
#include <log/log.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#define LOG_TAG "audio_hw_con"

#ifdef __cplusplus
extern "C" {
#endif

int audio_con_thread();

#ifdef __cplusplus
}
#endif
