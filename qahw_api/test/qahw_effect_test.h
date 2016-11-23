/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <pthread.h>

#define EFFECT_NUM 5
typedef void* (*thread_func_t)(void *);
extern thread_func_t effect_thread_funcs[EFFECT_NUM];

void *bassboost_thread_func(void*);   // thread main of bassboost effect
void *virtualizer_thread_func(void*); // thread main of virtualizer effect
void *equalizer_thread_func(void*);   // thread main of equalizer effect
void *visualizer_thread_func(void*);  // thread main of visualizer effect
void *reverb_thread_func(void*);      // thread main of reverb effect

typedef struct thread_data {
    pthread_t         effect_thread;
    pthread_attr_t    attr;
    pthread_mutex_t   mutex;
    pthread_cond_t    loop_cond;
    audio_io_handle_t io_handle;
    bool              exit;
    int               cmd;
    uint32_t          cmd_code;
    uint32_t          cmd_size;
    void              *cmd_data;
    uint32_t          *reply_size;
    void              *reply_data;
} thread_data_t;

extern thread_data_t *create_effect_thread(thread_func_t);
extern void effect_thread_command(thread_data_t *, int, uint32_t, uint32_t, void *);
extern void destroy_effect_thread(thread_data_t *);

extern const char *effect_str[EFFECT_NUM];

enum {
    EFFECT_LOAD_LIB = 1,
    EFFECT_GET_DESC,
    EFFECT_CREATE,
    EFFECT_CMD,
    EFFECT_PROC,
    EFFECT_RELEASE,
    EFFECT_UNLOAD_LIB,
    EFFECT_EXIT
};

