#include "audio_con_thread.h"
#include <android-base/unique_fd.h>

void* add_client(void* info) {
    info = nullptr;
    return info;
}

int audio_con_thread() {
    int rc = 0;
    android::base::unique_fd pipe_read, pipe_write;
    // Create a pipe that allows parent process sending logs over.
    if (!android::base::Pipe(&pipe_read, &pipe_write)) {
        ALOGV("%s pipe failed (%s)", __func__, strerror(rc));
        return 0;
    }
    android::base::unique_fd sock_fd(android_get_control_socket("audio_hw_socket"));
    if (sock_fd < 0) {
        ALOGE("%s Failed to open audio hardware socket error %s", __func__, strerror(sock_fd));
        return 0;
    }
    rc = listen(sock_fd, 1);
    if (rc < 0) {
        ALOGE("%s listen error %s", __func__, strerror(rc));
        return 0;
    }
    android::base::unique_fd epoll_fd(epoll_create1(F_GETLK));
    if (epoll_fd == -1) {
        ALOGE("%s Failed to create epoll error %s", __func__, strerror(epoll_fd));
        return 0;
    }
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = sock_fd.get();
    if (epoll_ctl(event.data.fd, EPOLL_CTL_ADD, event.data.fd, &event) == -1) {
        ALOGE("%s Failed to add epoll ", __func__);
        return 0;
    }
    pthread_t* thread = nullptr;
    rc = pthread_create(thread, NULL, add_client, thread);
    if (rc < 0) {
        ALOGE("%s pthread create error %s", __func__, strerror(rc));
        return 0;
    }
    return rc;
}
