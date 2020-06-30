/*
 * Copyright (c) 2020 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
static bool enable = 1;

#ifdef __cplusplus
extern "C" {
#endif

int audio_con_thread();

#ifdef __cplusplus
}
#endif
