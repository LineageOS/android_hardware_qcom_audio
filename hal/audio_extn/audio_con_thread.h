#include <bits/epoll_event.h>
#include <cutils/sockets.h>
#include <log/log.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#define MAX_EVENTS 5
#define TIMEOUT -1  // No timeout

static int nevents;
static pthread_t thread;
static struct epoll_event event;

#ifdef __cplusplus
extern "C" {
#endif

int audio_con_thread();

#ifdef __cplusplus
}
#endif
