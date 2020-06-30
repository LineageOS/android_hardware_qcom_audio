#include "audio_hw_con.h"

#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int epollfd = -1;

struct req_client {
	char *name; // v1[1]
	int  *fd; // v1[3]
	int  def_val; // v1[2]
	int  clnt_fd; // v1[4]
};

static struct req_client req_clients[] = {
	{
		.name = "ultrasound-cover1=1",
		.fd   = &,
		.def_val = 0,
		.clnt_fd = -1,
	},
	{
		.name = "ultrasound-cover1=0",
		.fd   = &,
		.def_val = 0,
		.clnt_fd = -1,
	},
	{
		.name = "ultrasound-proximity=1",
		.fd   = &,
		.def_val = 0,
		.clnt_fd = -1,
	},
	{
		.name = "ultrasound-proximity=0",
		.fd   = &,
		.def_val = 0,
		.clnt_fd = -1,
	},
};

struct event_data {
	int fd;
	void *callback;
	int id;
};

static int audio_handle_pipe() {}

static int add_client(){}

static int del_client(){}

static int audio_handle_client_sock(void) {
  struct epoll_event ev;
  struct event_data *data;
	socklen_t client_len;
	int client_fd = -1;
	struct sockaddr_un client_addr;

  while (1) {
    struct epoll_event events[MAX_EPOLL_EVENTS];
	int i;
	struct event_data *data;
	int buf[2], ret = 0;
    int nevents;
    nevents = epoll_wait(epollfd, events, MAX_EPOLL_EVENTS, -1);
    if (nevents == -1) {
      if (errno != EINTR)
        ALOGE("%sepoll wait failed; errno = %d", __func__,errno);
        break;
    }
// TO-DO
  }

error:
  if (epoll_fd >= 0)
    close(epoll_fd);

  return NULL;
}

int audio_hal_con_thread_start() {
  struct epoll_event ev;
  int pipefd[2], listen_sock, ret, sock;
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

  epollfd = epoll_create(MAX_EPOLL_EVENTS);
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
