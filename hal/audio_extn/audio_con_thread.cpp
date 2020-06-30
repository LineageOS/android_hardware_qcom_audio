#include "audio_con_thread.h"
#include <android-base/unique_fd.h>
#include <string>
#define LOG_TAG "audio_hw_con"

#define MAX_EVENTS 5
#define TIMEOUT -1  // No timeout

static pthread_t thread;
static const std::string AUDIO_SOCKET = "audio_hw_socket";

android::base::unique_fd get_sock_fd() {
    android::base::unique_fd sock_fd(android_get_control_socket(AUDIO_SOCKET.c_str()));
    if (sock_fd < 0) {
        ALOGE("%s Failed to open audio hardware socket error %s", __func__, strerror(sock_fd));
    }
    return sock_fd;
}

static android::base::unique_fd sock_fd = get_sock_fd();

void* add_client(void* clnt) {
    struct epoll_event event;
    int rc = 0, num = 0;
    int buf;
    int nevents;
    int total_num = 0;
    char msg;
    android::base::unique_fd connection_fd;

    while (true) {
        nevents = epoll_wait(sock_fd, &event, MAX_EVENTS, TIMEOUT);
        if (nevents == -1) {
            continue;
        }
        do {
            if (sock_fd < 0) {
                ALOGE("%s Failed to open audio hardware socket error %s", __func__,
                      strerror(sock_fd));
                return NULL;
            }
            if (event.events & EPOLLERR) {
                ALOGE("EPOLLERR on event #%d", num);
                return NULL;
            }
            if (event.data.fd == sock_fd) {
                // if (event.events & EPOLLHUP) goto audio_handle_ctl_srv_sock;
                if (event.events & EPOLLIN) {
                    buf = 0;
                    if (total_num < 3) {
                        total_num += 1;
                        connection_fd.reset(accept(sock_fd, nullptr, nullptr));
                        if (connection_fd) {
                            ALOGD("%s accept client connect %d, total connect %d", __func__, rc,
                                  total_num);
                        } else {
                            ALOGE("%s accept failed %d", __func__, rc);
                        }
                    } else {
                        ALOGE("%s client connect out of number%d", __func__, total_num);
                    }
                }
                if (event.events & EPOLLIN) {
                    read(event.data.fd, &buf, 1);
                    if (!buf) goto exit;
                    // ALOGW("%s received client connect %d", __func__, sock_fd);
                    ssize_t ret = TEMP_FAILURE_RETRY(read(connection_fd, &msg, sizeof(msg)));
                    if (ret != sizeof(msg)) {
                        if (msg) {
                            if (msg == -1) {
                                ALOGE("control data socket read failed; errno=%d", *__errno());
                            } else {
                                ALOGE("%s msg too short %d", __func__, msg);
                            }
                        }
                    } else {
                        ALOGE("Got EOF on control data socket");
                    }
                }
            exit:
                ALOGD("thread exit");
                pthread_exit(NULL);
            }
        } while (errno == EINTR);
        ALOGE("%s: epoll wait failed, errno = %d\n", __func__, errno);
    }

    return clnt;
    // audio_handle_ctl_srv_sock:
    // if (event.events & EPOLLHUP) ALOGE("%sEPOLLHUP err in server socket", a func);
    // audio_handle_client_sock:
    // if (event.events & EPOLLHUP) ALOGE("%sEPOLLHUP err in server socket", __func__);
    // audio_handle_pipe:
    // if (event.events & EPOLLHUP) ALOGE("%sEPOLLHUP err in server socket", __func__);
    // del_client:
}

int audio_con_thread() {
    int rc = 0;
    android::base::unique_fd pipe_read, pipe_write;
    // Create a pipe that allows parent process sending logs over.
    if (!android::base::Pipe(&pipe_read, &pipe_write)) {
        ALOGV("%s pipe failed (%s)", __func__, strerror(rc));
        return 0;
    }
    rc = listen(sock_fd, 1);
    if (rc < 0) {
        ALOGE("%s listen error %s", __func__, strerror(rc));
        return 0;
    }
    android::base::unique_fd epoll_fd(epoll_create1(EPOLL_CLOEXEC));
    if (epoll_fd == -1) {
        ALOGE("%s Failed to create epoll error %s", __func__, strerror(epoll_fd));
        return 0;
    }
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = sock_fd;
    if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, event.data.fd, &event) == -1) {
        ALOGE("%s Failed to add epoll ", __func__);
        return 0;
    }
    rc = pthread_create(&thread, NULL, add_client, (void*)thread);
    if (rc != 0) {
        ALOGE("%s pthread create error %s", __func__, strerror(rc));
        return 0;
    }
    return rc;
}
