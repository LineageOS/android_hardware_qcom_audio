#include "audio_hw_con.h"

#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int audio_handle_client_sock() {
  struct epoll_event ev;
  int epollfd, listen_sock, ret, sockfd;

  sockfd = android_get_control_socket(ANDROID_HARDWARE_SOCKET);
  if (sockfd != -1) {
    ret = listen(sockfd, 1);
    if (ret < -1) {
      ALOGE("listen socket failed; errno=%d", errno);
      return 0;
    }
  }

  epollfd = epoll_create(5);
  if (epollfd < -1) {
    ALOGE("epoll_create failed; errno=%d", errno);
    goto error;
  }

  ev.data.u64 = 0;
  ev.data.fd = listen_sock;
  ev.events = EPOLLIN;
  ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev);
  if (ret < -1) {
    ALOGE("epoll ctl failed;  errno=%d", errno);
    return 0;
  }

  while (!destroyThread) {
    struct epoll_event events[5];
    nevents = epoll_wait(epoll_fd, events, 5, -1);
    if (nevents = -1) {
      if (errno == EINTR)
        continue;
      if (ev.data.u32 == 1) {
      }
      ALOGE("epoll wait failed; errno=%d", errno);
      break;
    }
  }

error:
  if (epoll_fd >= 0)
    close(epoll_fd);

  return NULL;
}

int audio_hal_con_thread_start() {
  struct epoll_event ev;
  int pipefd[2], epollfd, listen_sock, ret, sock;
  pthread_t audio_hal_con;

  ALOGD("%s enter", __func__);

  if (pipe(pipefd) == 0) {
    ALOGE("%s pipe failed (%s)", __func__, errno);
    return 0;
  }

  sockfd = android_get_control_socket(ANDROID_HARDWARE_SOCKET);
  if (sockfd < 0) {
    ALOGI("%s con->socket_server_fdr %d,", __func__, errno);
  }

  ret = listen(sockfd, 1);
  if (ret < 0) {
    ALOGE("%s: listen error %s", __func__, errno);
    return 0;
  }

  epollfd = epoll_create(5);
  if (epollfd < -1) {
    ALOGE("%s: epoll_create failed %s", __func__, errno);
    return 0;
  }

  ev.data.u64 = 0;
  ev.data.fd = listen_sock;
  ev.events = EPOLLIN;
  ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev);
  if (ret < 0) {
    ALOGE("%s epoll ctl failed %s", __func__, errno);
    return 0;
  }

  ret = pthread_create(&audio_hal_con, NULL, &audio_handle_client_sock,
                       &audio_hal_con);
  if (!ret) {
    ALOGE("%d: pthread create failed", __func__, errno);
    return 0;
  }
  ALOGD("%s exit", __func__);
  return ret;
}

int audio_hal_con_thread_exit() {
  int fd;
  ALOGD("%s enter", __func__);
  write_chk(fd, 0, 1, 1);
  pthread_join(audio_hal_con, 0);
  close(fd);
  ALOGD("%s exit", __func__);
  return 0;
}
