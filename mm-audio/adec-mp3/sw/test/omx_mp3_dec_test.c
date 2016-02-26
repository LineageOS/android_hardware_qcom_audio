
/*--------------------------------------------------------------------------
Copyright (c) 2010, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of The Linux Foundation nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

/*
    An Open max test application ....
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/ioctl.h>
#include "OMX_Core.h"
#include "OMX_Component.h"
#include "QOMX_AudioExtensions.h"
#include "QOMX_AudioIndexExtensions.h"
#ifdef AUDIOV2
#include "control.h"
#endif
#include "pthread.h"
#include <signal.h>
#include <stdint.h>
#include<string.h>
#include <pthread.h>
#include <linux/ioctl.h>
#include <linux/msm_audio.h>
#include <errno.h>

#define USE_BUFFER_CASE 1
#define HOST_PCM_DEVICE 0
#define PCM_DEC_DEVICE 1

OMX_U32 mp3_frequency_index[3][4] = {
   {11025,0,22050,44100},
   {12000,0,24000,48000},
   {8000,0,16000,32000}
};

int is_multi_inst = 0;

#define DEBUG_PRINT       printf
#define DEBUG_PRINT_ERROR printf
#define PCM_PLAYBACK /* To write the pcm decoded data to the msm_pcm device for playback*/

#ifdef PCM_PLAYBACK

struct mp3_header
{
    OMX_U8 sync;
    OMX_U8 version;
    uint8_t Layer;
    OMX_U8 protection;
    OMX_U32  bitrate;
    OMX_U32 sampling_rate;
    OMX_U8 padding;
    OMX_U8 private_bit;
    OMX_U8 channel_mode;
};


#define DEFAULT_SAMPLING_RATE  44100
#define DEFAULT_CHANNEL_MODE   2

#endif  // PCM_PLAYBACK


/************************************************************************/
/*                #DEFINES                            */
/************************************************************************/
#define false 0
#define true 1

#define CONFIG_VERSION_SIZE(param) \
    param.nVersion.nVersion = CURRENT_OMX_SPEC_VERSION;\
    param.nSize = sizeof(param);

#define FAILED(result) (result != OMX_ErrorNone)

#define SUCCEEDED(result) (result == OMX_ErrorNone)

OMX_ERRORTYPE  parse_mp3_frameheader(OMX_BUFFERHEADERTYPE* buffer,
                                     struct mp3_header *header);

unsigned int extract_id3_header_size(OMX_U8* buffer);

/* http://ccrma.stanford.edu/courses/422/projects/WaveFormat/ */

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1

struct wav_header {
  uint32_t riff_id;
  uint32_t riff_sz;
  uint32_t riff_fmt;
  uint32_t fmt_id;
  uint32_t fmt_sz;
  uint16_t audio_format;
  uint16_t num_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;       /* sample_rate * num_channels * bps / 8 */
  uint16_t block_align;     /* num_channels * bps / 8 */
  uint16_t bits_per_sample;
  uint32_t data_id;
  uint32_t data_sz;
};

typedef struct hpcm
{
    int          pipe_in;
    int          pipe_out;
}hpcm;

typedef enum msg
{
    CTRL = 0,
    DATA = 1
}MSG;

typedef struct hpcm_info
{
    MSG                  msg_type;
    int                  fd;
    OMX_COMPONENTTYPE    *hComponent;
    OMX_BUFFERHEADERTYPE *bufHdr;
}hpcm_info;

struct adec_appdata
{
   uint32_t pcmplayback;
   uint32_t tunnel;
   uint32_t filewrite;
   uint32_t flushinprogress;
   uint32_t buffer_option;
   uint32_t pcm_device_type;
   pthread_mutex_t lock;
   pthread_cond_t cond;
   pthread_mutex_t elock;
   pthread_cond_t econd;
   pthread_cond_t fcond;
   FILE * inputBufferFile;
   FILE * outputBufferFile;
   OMX_PARAM_PORTDEFINITIONTYPE inputportFmt;
   OMX_PARAM_PORTDEFINITIONTYPE outputportFmt;
   OMX_AUDIO_PARAM_MP3TYPE mp3param;
   QOMX_AUDIO_STREAM_INFO_DATA streaminfoparam;
   OMX_PORT_PARAM_TYPE portParam;
   OMX_ERRORTYPE error;
   int input_buf_cnt;
   int output_buf_cnt;
   int used_ip_buf_cnt;
   int event_is_done;
   int ebd_event_is_done;
   int fbd_event_is_done;
   int ebd_cnt;
   int bOutputEosReached ;
   int bInputEosReached ;
   int bFlushing;
   int bPause;
   #ifdef AUDIOV2
   unsigned short session_id;
   unsigned short session_id_hpcm;
   int device_id ;
   int control ;
   char device[44];
   int devmgr_fd;
   #endif
   const char *in_filename;
   unsigned totaldatalen;
   OMX_STRING aud_comp;
   OMX_COMPONENTTYPE* mp3_dec_handle;
   OMX_BUFFERHEADERTYPE  **pInputBufHdrs ;
   OMX_BUFFERHEADERTYPE  **pOutputBufHdrs;
   int m_pcmdrv_fd;
   int num_pcm_buffers;
   pthread_mutex_t pcm_buf_lock;
   pthread_t m_pcmdrv_evt_thread_id;
   const char *out_filename;
   int bReconfigureOutputPort;
   int bEosOnInputBuf;
   int bEosOnOutputBuf;
   int bParseHeader;
   struct mp3_header mp3Header;
   OMX_U8* pBuffer_tmp;
   int count;
   int copy_done;
   int start_done;
   unsigned int length_filled;
   int spill_length;
   unsigned int pcm_buf_size;
   unsigned int pcm_buf_count;
   int first_buffer;
   hpcm mp3_hpcm;
};

struct adec_appdata adec_mp3_inst1;

//* OMX Spec Version supported by the wrappers. Version = 1.1 */
const OMX_U32 CURRENT_OMX_SPEC_VERSION = 0x00000101;


/************************************************************************/
/*                GLOBAL FUNC DECL                        */
/************************************************************************/
int Init_Decoder(struct adec_appdata* adec_appdata);
int Play_Decoder(struct adec_appdata* adec_appdata);
void process_portreconfig(struct adec_appdata* adec_appdata);
/**************************************************************************/
/*                STATIC DECLARATIONS                       */
/**************************************************************************/

static int open_audio_file (struct adec_appdata* adec_appdata);
static int Read_Buffer(OMX_BUFFERHEADERTYPE  *pBufHdr,FILE * inputBufferFile);
static OMX_ERRORTYPE Use_Buffer ( struct adec_appdata* adec_appdata,
                                  OMX_U32 nPortIndex );

static OMX_ERRORTYPE Free_Buffer ( struct adec_appdata* adec_appdata,
                                   OMX_U32 nPortIndex,
                                   OMX_BUFFERHEADERTYPE *bufHdr
                                  );

static OMX_ERRORTYPE Allocate_Buffer ( struct adec_appdata* adec_appdata,
                                       OMX_U32 nPortIndex );

static OMX_ERRORTYPE EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                                  OMX_IN OMX_PTR pAppData,
                                  OMX_IN OMX_EVENTTYPE eEvent,
                                  OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2,
                                  OMX_IN OMX_PTR pEventData);

static OMX_ERRORTYPE EmptyBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                                     OMX_IN OMX_PTR pAppData,
                                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);

static OMX_ERRORTYPE FillBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                                     OMX_IN OMX_PTR pAppData,
                                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);
void write_devctlcmd(int fd, const void *buf, int param);

void adec_appdata_init(struct adec_appdata* adec_appdata)
{
    adec_appdata->totaldatalen = 0;
    adec_appdata->input_buf_cnt = 0;
    adec_appdata->output_buf_cnt = 0;
    adec_appdata->used_ip_buf_cnt = 0;
    adec_appdata->event_is_done = 0;
    adec_appdata->ebd_event_is_done = 0;
    adec_appdata->fbd_event_is_done = 0;
    adec_appdata->ebd_cnt = 0;
    adec_appdata->bOutputEosReached = 0;
    adec_appdata->bInputEosReached = 0;
    adec_appdata->bFlushing = false;
    adec_appdata->bPause= false;
    adec_appdata->pcmplayback = 0;
    adec_appdata->tunnel      = 0;
    adec_appdata->filewrite   = 0;
    adec_appdata->flushinprogress = 0;
    adec_appdata->buffer_option = 0;
    adec_appdata->pcm_device_type = HOST_PCM_DEVICE;
    adec_appdata->bReconfigureOutputPort = 0;
    adec_appdata->bEosOnInputBuf = 0;
    adec_appdata->bEosOnOutputBuf = 0;
    adec_appdata->bParseHeader = 0;
    adec_appdata->mp3_dec_handle = NULL;
    adec_appdata->pInputBufHdrs = NULL;
    adec_appdata->pOutputBufHdrs = NULL;
    adec_appdata->pBuffer_tmp = NULL;
    adec_appdata->count = 0;
    adec_appdata->copy_done = 0;
    adec_appdata->start_done = 0;
    adec_appdata->length_filled = 0;
    adec_appdata->spill_length = 0;
    adec_appdata->pcm_buf_size = 4800;
    adec_appdata->pcm_buf_count = 2;
    adec_appdata->first_buffer = 1;
    adec_appdata->mp3Header.sync = 0;
    adec_appdata->mp3Header.version = 0;
    adec_appdata->mp3Header.Layer = 0;
    adec_appdata->mp3Header.protection = 0;
    adec_appdata->mp3Header.bitrate = 0;
    adec_appdata->mp3Header.sampling_rate = 0;
    adec_appdata->mp3Header.padding = 0;
    adec_appdata->mp3Header.private_bit = 0;
    adec_appdata->mp3Header.channel_mode = 0;
    adec_appdata->m_pcmdrv_fd = -1;
    adec_appdata->num_pcm_buffers = 0;
    adec_appdata->inputBufferFile = NULL;
    adec_appdata->outputBufferFile = NULL;
    adec_appdata->error = 0;
}

void wait_for_event(struct adec_appdata * adec_appdata)
{
   pthread_mutex_lock(&adec_appdata->lock);
   DEBUG_PRINT("%s: event_is_done=%d", __FUNCTION__, adec_appdata->event_is_done);
   while (adec_appdata->event_is_done == 0) {
      pthread_cond_wait(&adec_appdata->cond, &adec_appdata->lock);
   }
   adec_appdata->event_is_done = 0;
   pthread_mutex_unlock(&adec_appdata->lock);
}

void event_complete(struct adec_appdata * adec_appdata)
{
   pthread_mutex_lock(&adec_appdata->lock);
   if (adec_appdata->event_is_done == 0) {
      adec_appdata->event_is_done = 1;
      pthread_cond_broadcast(&adec_appdata->cond);
   }
   pthread_mutex_unlock(&adec_appdata->lock);
}

void *process_hpcm_drv_events( void* data)
{
    struct adec_appdata *adec_data = (struct adec_appdata *)data;
    hpcm_info ftb;

    int n=0;
    hpcm_info p;
    DEBUG_PRINT("%s adec_data=%p pipe_in=%d pipe_out=%d\n",__FUNCTION__,
                                                      adec_data,
                                                      adec_data->mp3_hpcm.pipe_in,
                                                      adec_data->mp3_hpcm.pipe_out);
    while(1)
    {
        DEBUG_PRINT("\n Waiting for next FBD from OMX.....\n");
        n = read(adec_data->mp3_hpcm.pipe_in,&p,sizeof(struct hpcm_info));
        if(n <= 0){
            DEBUG_PRINT("*********************\n");
            DEBUG_PRINT("KILLING HPCM THREAD...\n");
            DEBUG_PRINT("***********************\n");
            return (void*) -1;
        }
        if(p.msg_type == CTRL)
        {
            event_complete(adec_data);
            DEBUG_PRINT("DATA EMPTY\n");
        }
        else
        {
            DEBUG_PRINT("***********************\n");
          DEBUG_PRINT("\n%s-->pipe_in=%d pipe_out=%d n=%d\n",__FUNCTION__,
                                               adec_data->mp3_hpcm.pipe_in,
                                               adec_data->mp3_hpcm.pipe_out,n);
            DEBUG_PRINT("***********************\n");
        ftb.hComponent = p.hComponent;
        ftb.bufHdr = p.bufHdr;

        if ( write(adec_data->m_pcmdrv_fd, ftb.bufHdr->pBuffer, ftb.bufHdr->nFilledLen ) !=
                 (ssize_t)(ftb.bufHdr->nFilledLen) )
        {
            DEBUG_PRINT_ERROR("%s: Write data to PCM failed\n",__FUNCTION__);
        }
        DEBUG_PRINT("drvfd=%d bufHdr[%p] buffer[%p] len[%lu] hComponent[%p] bOutputEos=%d\n",
                                          adec_data->m_pcmdrv_fd,
                                          ftb.bufHdr,ftb.bufHdr->pBuffer,
                                          ftb.bufHdr->nFilledLen,
                                          ftb.hComponent,adec_data->bOutputEosReached);
        if(!(adec_data->bOutputEosReached))
            OMX_FillThisBuffer(ftb.hComponent,ftb.bufHdr);
        }
    }
    return 0;
}

/* Thread for handling the events from PCM_DEC driver */
void* process_pcm_drv_events( void* data)
{
    struct adec_appdata *adec_data = (struct adec_appdata *)data;
    OMX_BUFFERHEADERTYPE *bufHdr = NULL;
    struct msm_audio_event tcxo_event;
    int rc = 0, buf_count = 0;

    if(data == NULL)
    {
        DEBUG_PRINT("\n PPDE: data is NULL\n");
        return (void*)(-1);
    }

    while(1)
    {
        DEBUG_PRINT("\nPPDE:Calling ioctl AUDIO_GET_EVENT ...\n");
        rc = ioctl(adec_data->m_pcmdrv_fd, AUDIO_GET_EVENT, &tcxo_event);
        if((rc == -1) && (errno == ENODEV ))
        {
            DEBUG_PRINT("\nPPDE:Exiting with rc %d and error %d", rc, errno);
            return (void*)(-1);
        }
        DEBUG_PRINT("\nPPDE:Event Type[%d]", tcxo_event.event_type);

        switch(tcxo_event.event_type)
        {
            case AUDIO_EVENT_WRITE_DONE:
            {
                bufHdr = (OMX_BUFFERHEADERTYPE*)tcxo_event.event_payload.
                    aio_buf.private_data;

                if(bufHdr)
                {
                    buf_count++;
                    DEBUG_PRINT("\nPPDE:PCMDEC-ASYNC_WRITE DONE for bufHdr[%p], \
                        buf_count = %d\n", bufHdr, buf_count);

                    pthread_mutex_lock(&adec_data->pcm_buf_lock);
                    adec_data->num_pcm_buffers--;
                    pthread_mutex_unlock(&adec_data->pcm_buf_lock);

                    if(adec_data->bOutputEosReached == true)
                    {
                        if(adec_data->num_pcm_buffers == 0)
                        {
                            DEBUG_PRINT("\nPPDE: Output EOS reached in PCMDEC\n");
                            DEBUG_PRINT("\nPPDE::: OUTPUT EOS REACHED....\n");
                            event_complete(adec_data);
                            return 0;
                        }
                        else
                        {
                            DEBUG_PRINT("\nWaiting for PCM to play remaining \
                                %d buffers ...\n", adec_data->num_pcm_buffers);
                        }
                    }
                    else
                    {
                        DEBUG_PRINT("\nPPDE calling FTB");
                        OMX_FillThisBuffer(adec_data->mp3_dec_handle, bufHdr);
                    }
                }
                else
                {
                    DEBUG_PRINT("\nPPDE: Invalid bufHdr[%p] in WRITE_DONE\n",
                        bufHdr);
                }
            }
            break;

            default:
                DEBUG_PRINT("PPDE: Received Invalid Event");
            break;
        }
    }
    return 0;
}

OMX_ERRORTYPE EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                           OMX_IN OMX_PTR pAppData,
                           OMX_IN OMX_EVENTTYPE eEvent,
                           OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2,
                           OMX_IN OMX_PTR pEventData)
{
   //DEBUG_PRINT("Function %s \n command %d  Event complete %d", __FUNCTION__,(OMX_COMMANDTYPE)nData1,nData2);
   int bufCnt=0;
   struct adec_appdata* adec_appdata;
    /* To remove warning for unused variable to keep prototype same */
   (void)hComponent;
   (void)pEventData;

   if(NULL != pAppData)
       adec_appdata = (struct adec_appdata*) pAppData;
   else
       return OMX_ErrorBadParameter;
   switch(eEvent) {
      case OMX_EventCmdComplete:
         DEBUG_PRINT("*********************************************\n");
         DEBUG_PRINT("\n OMX_EventCmdComplete \n");
         DEBUG_PRINT("*********************************************\n");
         if(OMX_CommandPortDisable == (OMX_COMMANDTYPE)nData1) {
            DEBUG_PRINT("******************************************\n");
            DEBUG_PRINT("Recieved DISABLE Event Command Complete[%d]\n",(signed)nData2);
            DEBUG_PRINT("******************************************\n");
         }
         else if(OMX_CommandPortEnable == (OMX_COMMANDTYPE)nData1) {
            DEBUG_PRINT("*********************************************\n");
            DEBUG_PRINT("Recieved ENABLE Event Command Complete[%d]\n",(signed)nData2);
            DEBUG_PRINT("*********************************************\n");
         }
         else if(OMX_CommandFlush== (OMX_COMMANDTYPE)nData1)
         {
             DEBUG_PRINT("*********************************************\n");
             DEBUG_PRINT("Recieved FLUSH Event Command Complete[%d]\n",(signed)nData2);
             DEBUG_PRINT("*********************************************\n");
         }
         event_complete(adec_appdata);
      break;

      case OMX_EventError:
         DEBUG_PRINT("*********************************************\n");
         DEBUG_PRINT("\n OMX_EventError \n");
         DEBUG_PRINT("*********************************************\n");
         if(OMX_ErrorInvalidState == (OMX_ERRORTYPE)nData1)
             {
                DEBUG_PRINT("\n OMX_ErrorInvalidState \n");
                for(bufCnt=0; bufCnt < adec_appdata->input_buf_cnt; ++bufCnt)
                {
                   OMX_FreeBuffer(adec_appdata->mp3_dec_handle, 0, adec_appdata->pInputBufHdrs[bufCnt]);
                }
                if(adec_appdata->tunnel == 0)
                {
                    for(bufCnt=0; bufCnt < adec_appdata->output_buf_cnt; ++bufCnt)
                    {
                      OMX_FreeBuffer(adec_appdata->mp3_dec_handle, 1, adec_appdata->pOutputBufHdrs[bufCnt]);
                    }
                }

                DEBUG_PRINT("*********************************************\n");
                DEBUG_PRINT("\n Component Deinitialized \n");
                DEBUG_PRINT("*********************************************\n");
                exit(0);
             }
             else if(OMX_ErrorComponentSuspended == (OMX_ERRORTYPE)nData1)
             {
                DEBUG_PRINT("*********************************************\n");
                DEBUG_PRINT("\n Component Received Suspend Event \n");
                DEBUG_PRINT("*********************************************\n");
             }
      break;

       case OMX_EventPortSettingsChanged:
          if(adec_appdata->tunnel == 0)
          {
              adec_appdata->bReconfigureOutputPort = 1;
              DEBUG_PRINT("*********************************************\n");
              DEBUG_PRINT("\n OMX_EventPortSettingsChanged \n");
              DEBUG_PRINT("*********************************************\n");
              event_complete(adec_appdata);
          }
      break;

      case OMX_EventBufferFlag:
         DEBUG_PRINT("\n *********************************************\n");
         DEBUG_PRINT("\n OMX_EventBufferFlag \n");
         DEBUG_PRINT("\n *********************************************\n");
         adec_appdata->bOutputEosReached = true;
         if((!adec_appdata->pcmplayback && adec_appdata->filewrite)
                || (adec_appdata->pcmplayback &&
                 (adec_appdata->pcm_device_type == HOST_PCM_DEVICE ||
                 (adec_appdata->pcm_device_type == PCM_DEC_DEVICE
                 && !adec_appdata->num_pcm_buffers))))
         {
                 event_complete(adec_appdata);
         }
      break;
      case OMX_EventComponentResumed:
         DEBUG_PRINT("*********************************************\n");
         DEBUG_PRINT("\n Component Received Resume Event \n");
         DEBUG_PRINT("*********************************************\n");
         break;
      default:
         DEBUG_PRINT("\n Unknown Event \n");
      break;
   }
   return OMX_ErrorNone;
}


OMX_ERRORTYPE FillBufferDone(OMX_IN OMX_HANDLETYPE          hComponent,
                             OMX_IN OMX_PTR                 pAppData,
                             OMX_IN OMX_BUFFERHEADERTYPE*   pBuffer)
{
    struct msm_audio_aio_buf audio_aio_buf;
    unsigned int i=0;
    int bytes_writen = 0;
    struct msm_audio_config drv_pcm_config;
    struct adec_appdata* adec_appdata;

    if(NULL != pAppData)
       adec_appdata = (struct adec_appdata*) pAppData;
    else
       return OMX_ErrorBadParameter;

    if (adec_appdata->flushinprogress == 1 )
    {
        DEBUG_PRINT(" FillBufferDone: flush is in progress so hold the buffers\n");
        return OMX_ErrorNone;
    }
    if ( (adec_appdata->count == 0) &&
        (adec_appdata->pcm_device_type == HOST_PCM_DEVICE) &&
        (adec_appdata->pcmplayback))
    {
        DEBUG_PRINT(" open pcm device \n");
        adec_appdata->m_pcmdrv_fd = open("/dev/msm_pcm_out", O_RDWR);
        if ( adec_appdata->m_pcmdrv_fd < 0 )
        {
            DEBUG_PRINT("Cannot open audio device\n");
            return -1;
        }
        else
        {
            DEBUG_PRINT("Open pcm device successfull\n");
            DEBUG_PRINT("Configure Driver for PCM playback \n");
            ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_GET_CONFIG, &drv_pcm_config);
            DEBUG_PRINT("drv_pcm_config.buffer_count %d \n", drv_pcm_config.buffer_count);
            DEBUG_PRINT("drv_pcm_config.buffer_size %d \n",  drv_pcm_config.buffer_size);
            drv_pcm_config.sample_rate   = adec_appdata->mp3Header.sampling_rate;
            drv_pcm_config.channel_count = adec_appdata->mp3Header.channel_mode;
            ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_SET_CONFIG, &drv_pcm_config);
            DEBUG_PRINT("Configure Driver for PCM playback \n");
            ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_GET_CONFIG, &drv_pcm_config);
            DEBUG_PRINT("drv_pcm_config.buffer_count %d \n", drv_pcm_config.buffer_count);
            DEBUG_PRINT("drv_pcm_config.buffer_size %d \n",  drv_pcm_config.buffer_size);
            adec_appdata->pcm_buf_size = drv_pcm_config.buffer_size;
            adec_appdata->pcm_buf_count = drv_pcm_config.buffer_count;
#ifdef AUDIOV2
            ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_GET_SESSION_ID, &adec_appdata->session_id_hpcm);
            DEBUG_PRINT("session id 0x%4x \n", adec_appdata->session_id_hpcm);
            if(adec_appdata->devmgr_fd >= 0)
            {
               write_devctlcmd(adec_appdata->devmgr_fd, "-cmd=register_session_rx -sid=", adec_appdata->session_id_hpcm);
            }
            else
            {
               adec_appdata->control = msm_mixer_open("/dev/snd/controlC0", 0);
               if (adec_appdata->control < 0)
                  printf("ERROR opening the device\n");
               adec_appdata->device_id = msm_get_device(adec_appdata->device);
               DEBUG_PRINT ("\ndevice_id = %d\n", adec_appdata->device_id);
               DEBUG_PRINT("\nsessionid = %d\n", adec_appdata->session_id);
               if (msm_en_device(adec_appdata->device_id, 1))
               {
                  perror("could not enable device\n");
                  return -1;
               }
               if (msm_route_stream(1, adec_appdata->session_id_hpcm, adec_appdata->device_id, 1))
               {
                  DEBUG_PRINT("could not set stream routing\n");
                  return -1;
               }
            }
#endif
        }
        adec_appdata->pBuffer_tmp= (OMX_U8*)malloc(adec_appdata->pcm_buf_count*sizeof(OMX_U8)*adec_appdata->pcm_buf_size);
        if ( adec_appdata->pBuffer_tmp == NULL )
        {
            return -1;
        }
        else
        {
            memset(adec_appdata->pBuffer_tmp, 0, adec_appdata->pcm_buf_count*adec_appdata->pcm_buf_size);
        }
    }
    DEBUG_PRINT(" FillBufferDone #%d size %u\n", adec_appdata->count++,(unsigned)(pBuffer->nFilledLen));

    if ( adec_appdata->bEosOnOutputBuf )
    {
        return OMX_ErrorNone;
    }

    if ( (adec_appdata->tunnel == 0) && (adec_appdata->filewrite == 1) )
    {
        bytes_writen =
        fwrite(pBuffer->pBuffer,1,pBuffer->nFilledLen,adec_appdata->outputBufferFile);
        DEBUG_PRINT(" FillBufferDone size writen to file  %d\n",bytes_writen);
        adec_appdata->totaldatalen += bytes_writen ;
    }

#ifdef PCM_PLAYBACK
    if ( adec_appdata->pcmplayback && pBuffer->nFilledLen )
    {
      if(adec_appdata->pcm_device_type == HOST_PCM_DEVICE)
      {
        if ( adec_appdata->start_done == 0 )
        {
            if ( (adec_appdata->length_filled+ pBuffer->nFilledLen)>=(adec_appdata->pcm_buf_count*adec_appdata->pcm_buf_size) )
            {
                adec_appdata->spill_length = (pBuffer->nFilledLen-(adec_appdata->pcm_buf_count*adec_appdata->pcm_buf_size)+adec_appdata->length_filled);
                memcpy (adec_appdata->pBuffer_tmp+adec_appdata->length_filled, pBuffer->pBuffer,
                        ((adec_appdata->pcm_buf_count*adec_appdata->pcm_buf_size)-adec_appdata->length_filled));
                adec_appdata->length_filled = (adec_appdata->pcm_buf_count*adec_appdata->pcm_buf_size);
                adec_appdata->copy_done = 1;
            }
            else
            {
                memcpy (adec_appdata->pBuffer_tmp+adec_appdata->length_filled, pBuffer->pBuffer, pBuffer->nFilledLen);
                adec_appdata->length_filled +=pBuffer->nFilledLen;
            }
            if (adec_appdata->copy_done == 1 )
            {
                for ( i=0; i<adec_appdata->pcm_buf_count; i++ )
                {
                    if ( write(adec_appdata->m_pcmdrv_fd,adec_appdata->pBuffer_tmp+i*adec_appdata->pcm_buf_size, adec_appdata->pcm_buf_size )
                         != (ssize_t)(adec_appdata->pcm_buf_size) )
                    {
                        DEBUG_PRINT("FillBufferDone: Write data to PCM failed\n");
                        return -1;
                    }

                }
                DEBUG_PRINT("AUDIO_START called for PCM \n");
                ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_START, 0);
                if (adec_appdata->spill_length != 0 )
                {
                    if ( write(adec_appdata->m_pcmdrv_fd, pBuffer->pBuffer+((pBuffer->nFilledLen)-adec_appdata->spill_length),adec_appdata->spill_length)
                         != adec_appdata->spill_length )
                    {
                        DEBUG_PRINT("FillBufferDone: Write data to PCM failed\n");
                        return -1;
                    }
                }




                adec_appdata->copy_done = 0;
                adec_appdata->start_done = 1;
            }
            if((adec_appdata->pcmplayback && (adec_appdata->pcm_device_type == HOST_PCM_DEVICE)) ||
                (!adec_appdata->pcmplayback && adec_appdata->filewrite))
            {
                DEBUG_PRINT(" FBD calling FTB");
                OMX_FillThisBuffer(hComponent,pBuffer);
            }
        }
        else
        {
            unsigned int len=0;
            hpcm_info ftb;
            ftb.msg_type = DATA;
            ftb.hComponent = hComponent;
            ftb.bufHdr = pBuffer;
            len= write(adec_appdata->mp3_hpcm.pipe_out,&ftb,sizeof(hpcm_info));
            DEBUG_PRINT(" FillBufferDone: writing data to hpcm thread len=%d\n",len);

        }

      }

        /* Write o/p data in Async manner to PCM Dec Driver */
        if(adec_appdata->pcm_device_type == PCM_DEC_DEVICE)
        {
            if(adec_appdata->count == 1)
            {
               DEBUG_PRINT("FillBufferDone: PCM AUDIO_START\n");
               ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_START, 0);
            }

            audio_aio_buf.buf_len = pBuffer->nAllocLen;
            audio_aio_buf.data_len = pBuffer->nFilledLen;
            audio_aio_buf.buf_addr = pBuffer->pBuffer;
            audio_aio_buf.private_data = pBuffer;

            DEBUG_PRINT("FBD:Calling PCMDEC ASYNC_WRITE for bufhdr[%p]\n", pBuffer);

            if(0 > ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_ASYNC_WRITE, &audio_aio_buf))
            {
                DEBUG_PRINT("\nERROR in PCMDEC ASYNC WRITE call\n");
                return OMX_ErrorHardware;
            }

            pthread_mutex_lock(&adec_appdata->pcm_buf_lock);
            adec_appdata->num_pcm_buffers++;
            DEBUG_PRINT("FBD: Bufcnt with PCMDEC  = %d\n", adec_appdata->num_pcm_buffers);
            pthread_mutex_unlock(&adec_appdata->pcm_buf_lock);

            DEBUG_PRINT("FBD: PCMDEC ASYNC_WRITE call is succesfull\n");
        }
    }
    else if(adec_appdata->pcmplayback && !pBuffer->nFilledLen)
    {
        DEBUG_PRINT(" FBD calling FTB...special case");
        OMX_FillThisBuffer(hComponent,pBuffer);

    }
    else if(!(adec_appdata->pcmplayback) && (adec_appdata->filewrite))
    {
        DEBUG_PRINT(" FBD calling FTB");
        OMX_FillThisBuffer(hComponent,pBuffer);

    }
#endif   // PCM_PLAYBACK

    if(pBuffer->nFlags & OMX_BUFFERFLAG_EOS)
    {
        DEBUG_PRINT("FBD EOS REACHED...........\n");
        adec_appdata->bEosOnOutputBuf = true;
    }

    return OMX_ErrorNone;
}


OMX_ERRORTYPE EmptyBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                              OMX_IN OMX_PTR pAppData,
                              OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
   int readBytes =0;
   struct adec_appdata* adec_appdata;

   if(NULL != pAppData)
       adec_appdata = (struct adec_appdata*) pAppData;
   else
       return OMX_ErrorBadParameter;
   DEBUG_PRINT("\nFunction %s cnt[%d]\n", __FUNCTION__,adec_appdata->ebd_cnt);
   adec_appdata->ebd_cnt++;
   adec_appdata->used_ip_buf_cnt--;
   if(adec_appdata->bEosOnInputBuf) {
      DEBUG_PRINT("\n*********************************************\n");
      DEBUG_PRINT("   EBD::EOS on input port\n ");
      DEBUG_PRINT("   TBD:::De Init the open max here....!!!\n");
      DEBUG_PRINT("*********************************************\n");

     return OMX_ErrorNone;
   }
   else if (adec_appdata->bFlushing == true) {
      if (adec_appdata->used_ip_buf_cnt == 0) {
         fseek(adec_appdata->inputBufferFile, 0, 0);
         adec_appdata->bFlushing = false;
      }
      else {
         DEBUG_PRINT("omx_mp3_adec_test: more buffer to come back\n");
         return OMX_ErrorNone;
      }
   }
   if((readBytes = Read_Buffer(pBuffer,adec_appdata->inputBufferFile)) > 0) {
      pBuffer->nFilledLen = readBytes;
      adec_appdata->used_ip_buf_cnt++;
      OMX_EmptyThisBuffer(hComponent,pBuffer);
   }
   else {
        DEBUG_PRINT("\n readBytes = %d\n", readBytes);
        pBuffer->nFlags |= OMX_BUFFERFLAG_EOS;
        adec_appdata->bEosOnInputBuf = true;
        adec_appdata->used_ip_buf_cnt++;
        pBuffer->nFilledLen = 0;
        OMX_EmptyThisBuffer(hComponent,pBuffer);
        DEBUG_PRINT("EBD..Either EOS or Some Error while reading file\n");
   }
   return OMX_ErrorNone;
}


void signal_handler(int sig_id)
{
   /* Flush */

   if (sig_id == SIGUSR1) {
      DEBUG_PRINT("SIGUSR1 Invoked\n");
      if(!is_multi_inst)
      {
          DEBUG_PRINT("%s Initiate flushing\n", __FUNCTION__);
          adec_mp3_inst1.bFlushing = true;
          OMX_SendCommand(adec_mp3_inst1.mp3_dec_handle, OMX_CommandFlush, OMX_ALL, NULL);
      }
   }
   else if (sig_id == SIGUSR2) {
      DEBUG_PRINT("SIGUSR2 Invoked\n");
      if(!is_multi_inst)
      {
         if (adec_mp3_inst1.bPause == true) {
             DEBUG_PRINT("%s resume playback\n", __FUNCTION__);
             adec_mp3_inst1.bPause = false;
             OMX_SendCommand(adec_mp3_inst1.mp3_dec_handle, OMX_CommandStateSet, OMX_StateExecuting, NULL);
         }
         else {
             DEBUG_PRINT("%s pause playback\n", __FUNCTION__);
             adec_mp3_inst1.bPause = true;
             OMX_SendCommand(adec_mp3_inst1.mp3_dec_handle, OMX_CommandStateSet, OMX_StatePause, NULL);
         }
      }
   }
}


void* thread_function(void* data)
{
   struct adec_appdata *adec_mp3_inst = (struct adec_appdata *)data;
   struct wav_header hdr;
   int bufCnt=0;
   OMX_ERRORTYPE result;
   int bytes_writen = 0;

#ifdef AUDIOV2
strlcpy(adec_mp3_inst1.device,"speaker_stereo_rx",
			sizeof(adec_mp3_inst1.device));
adec_mp3_inst1.control=0;
#endif

   if(Init_Decoder(adec_mp3_inst)!= 0x00) {
      DEBUG_PRINT("Decoder Init failed\n");
      return (void*)(-1);
   }

   if(Play_Decoder(adec_mp3_inst) != 0x00) {
      DEBUG_PRINT("Play_Decoder failed\n");
      return (void*) (-1);
   }

   // Wait till EOS is reached...
   if(adec_mp3_inst->bReconfigureOutputPort)
   {
      wait_for_event(adec_mp3_inst);
   }

   DEBUG_PRINT(" bOutputEosReached = %d bInputEosReached = %d \n",
       adec_mp3_inst->bOutputEosReached, adec_mp3_inst->bInputEosReached);
   if(adec_mp3_inst->bOutputEosReached) {

      #ifdef PCM_PLAYBACK
      if(adec_mp3_inst->pcmplayback == 1) {
         sleep(1);
         if(adec_mp3_inst->pcm_device_type == PCM_DEC_DEVICE)
         {
             fsync(adec_mp3_inst->m_pcmdrv_fd);
         }


         if(adec_mp3_inst->pcm_device_type == PCM_DEC_DEVICE)
         {
             DEBUG_PRINT("\n Calling ABORT_GET_EVENT for PCMDEC driver\n");
             if(0 > ioctl(adec_mp3_inst->m_pcmdrv_fd, AUDIO_ABORT_GET_EVENT, NULL))
             {
                 DEBUG_PRINT("\n Error in ioctl AUDIO_ABORT_GET_EVENT\n");
             }

             DEBUG_PRINT("\n Waiting for PCMDrv Event Thread complete\n");
             pthread_join(adec_mp3_inst->m_pcmdrv_evt_thread_id, NULL);
         }
         else
         {
             DEBUG_PRINT("*******************************\n");
             DEBUG_PRINT("\n HPCMDrv Event Thread complete %d %d\n",
                                adec_mp3_inst->mp3_hpcm.pipe_in,
                                adec_mp3_inst->mp3_hpcm.pipe_out);
             close(adec_mp3_inst->mp3_hpcm.pipe_in);
             close(adec_mp3_inst->mp3_hpcm.pipe_out);
             adec_mp3_inst->mp3_hpcm.pipe_in=-1;
             adec_mp3_inst->mp3_hpcm.pipe_out=-1;
             pthread_join(adec_mp3_inst->m_pcmdrv_evt_thread_id, NULL);
             DEBUG_PRINT("*******************************\n");
         }
         ioctl(adec_mp3_inst->m_pcmdrv_fd, AUDIO_STOP, 0);
#ifdef AUDIOV2
        if(adec_mp3_inst->devmgr_fd >= 0)
        {
            write_devctlcmd(adec_mp3_inst->devmgr_fd, "-cmd=unregister_session_rx -sid=", adec_mp3_inst->session_id_hpcm);
        }
        else
        {
	     if (msm_route_stream(1, adec_mp3_inst->session_id_hpcm, adec_mp3_inst->device_id, 0))
             {
                DEBUG_PRINT("\ncould not set stream routing\n");
             }
        }
#endif
         if(adec_mp3_inst->m_pcmdrv_fd >= 0) {
            close(adec_mp3_inst->m_pcmdrv_fd);
            adec_mp3_inst->m_pcmdrv_fd = -1;
            DEBUG_PRINT(" PCM device closed succesfully \n");
         }
         else {
            DEBUG_PRINT(" PCM device close failure \n");
         }
      }
      #endif // PCM_PLAYBACK

      if((adec_mp3_inst->tunnel == 0) && (adec_mp3_inst->filewrite == 1)) {
         hdr.riff_id = ID_RIFF;
         hdr.riff_sz = 0;
         hdr.riff_fmt = ID_WAVE;
         hdr.fmt_id = ID_FMT;
         hdr.fmt_sz = 16;
         hdr.audio_format = FORMAT_PCM;
         hdr.num_channels = adec_mp3_inst->mp3Header.channel_mode;
         hdr.sample_rate = adec_mp3_inst->mp3Header.sampling_rate;
         hdr.byte_rate = hdr.sample_rate * hdr.num_channels * 2;
         hdr.block_align = hdr.num_channels * 2;
         hdr.bits_per_sample = 16;
         hdr.data_id = ID_DATA;
         hdr.data_sz = 0;

         DEBUG_PRINT("output file closed and EOS reached total decoded data length %d\n",adec_mp3_inst->totaldatalen);
         hdr.data_sz = adec_mp3_inst->totaldatalen;
         hdr.riff_sz = adec_mp3_inst->totaldatalen + 8 + 16 + 8;
         fseek(adec_mp3_inst->outputBufferFile, 0L , SEEK_SET);
         bytes_writen = fwrite(&hdr,1,sizeof(hdr),adec_mp3_inst->outputBufferFile);
         if (bytes_writen <= 0) {
            DEBUG_PRINT("Invalid Wav header write failed\n");
         }
         fclose(adec_mp3_inst->outputBufferFile);
      }
      /************************************************************************************/

      DEBUG_PRINT("\nMoving the decoder to idle state \n");
      OMX_SendCommand(adec_mp3_inst->mp3_dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);
      wait_for_event(adec_mp3_inst);

      DEBUG_PRINT("\nMoving the decoder to loaded state \n");
      OMX_SendCommand(adec_mp3_inst->mp3_dec_handle, OMX_CommandStateSet, OMX_StateLoaded,0);

      DEBUG_PRINT("\nFillBufferDone: Deallocating i/p buffers \n");
      for(bufCnt=0; bufCnt < adec_mp3_inst->input_buf_cnt; ++bufCnt) {
         Free_Buffer(adec_mp3_inst, 0, adec_mp3_inst->pInputBufHdrs[bufCnt]);
      }

      free(adec_mp3_inst->pInputBufHdrs);
      adec_mp3_inst->pInputBufHdrs = NULL;

      if(adec_mp3_inst->tunnel == 0) {
         DEBUG_PRINT("\nFillBufferDone: Deallocating o/p buffers \n");
         for(bufCnt=0; bufCnt < adec_mp3_inst->output_buf_cnt; ++bufCnt) {
            Free_Buffer(adec_mp3_inst, 1, adec_mp3_inst->pOutputBufHdrs[bufCnt]);
         }
         free(adec_mp3_inst->pOutputBufHdrs);
         adec_mp3_inst->pOutputBufHdrs = NULL;
      }

      DEBUG_PRINT("*******************************************\n");
      wait_for_event(adec_mp3_inst);
      adec_mp3_inst->ebd_cnt=0;
      adec_mp3_inst->bOutputEosReached = false;
      adec_mp3_inst->bInputEosReached = false;
      adec_mp3_inst->bEosOnInputBuf = 0;
      adec_mp3_inst->bEosOnOutputBuf = 0;
      adec_mp3_inst->bReconfigureOutputPort = 0;
      if (adec_mp3_inst->pBuffer_tmp )
      {
          free(adec_mp3_inst->pBuffer_tmp);
          adec_mp3_inst->pBuffer_tmp =NULL;
      }
      result = OMX_FreeHandle(adec_mp3_inst->mp3_dec_handle);
      if (result != OMX_ErrorNone) {
         DEBUG_PRINT_ERROR("\nOMX_FreeHandle error. Error code: %d\n", result);
      }
      else DEBUG_PRINT("OMX_FreeHandle success...\n");

      adec_mp3_inst->mp3_dec_handle = NULL;
#ifdef AUDIOV2
      if(adec_mp3_inst->devmgr_fd >= 0)
      {
         write_devctlcmd(adec_mp3_inst->devmgr_fd, "-cmd=unregister_session_rx -sid=", adec_mp3_inst->session_id);
         close(adec_mp3_inst->devmgr_fd);
      }
      else
      {
         if (msm_route_stream(1, adec_mp3_inst->session_id, adec_mp3_inst->device_id, 0))
         {
            DEBUG_PRINT("\ncould not set stream routing\n");
            return (void *)-1;
         }
         if (msm_en_device(adec_mp3_inst->device_id, 0))
         {
            DEBUG_PRINT("\ncould not enable device\n");
            return (void *)-1;
         }
         msm_mixer_close();
       }
#endif
      pthread_cond_destroy(&adec_mp3_inst->cond);
      pthread_mutex_destroy(&adec_mp3_inst->lock);

      if(adec_mp3_inst->pcmplayback &&
          adec_mp3_inst->pcm_device_type == PCM_DEC_DEVICE)
      {
          pthread_mutex_destroy(&adec_mp3_inst->pcm_buf_lock);
      }
   }
   return 0;
}

int main(int argc, char **argv)
{
    struct sigaction sa;
    pthread_t thread1_id;
    int thread1_ret=0;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &signal_handler;
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    if(argc == 7)
    {
            adec_appdata_init(&adec_mp3_inst1);
            pthread_cond_init(&adec_mp3_inst1.cond, 0);
            pthread_mutex_init(&adec_mp3_inst1.lock, 0);
            adec_mp3_inst1.in_filename = argv[1];
            adec_mp3_inst1.bParseHeader = atoi(argv[2]);
            adec_mp3_inst1.pcmplayback = atoi(argv[3]);
            adec_mp3_inst1.filewrite = atoi(argv[4]);
            adec_mp3_inst1.out_filename = argv[5];
            adec_mp3_inst1.buffer_option = atoi(argv[6]);

            //adec_mp3_inst1.out_filename = (char*)malloc(sizeof("audio.wav"));
            //strncpy(adec_mp3_inst1.out_filename,"audio.wav",strlen("audio.wav"));
            if(adec_mp3_inst1.tunnel == 0)
               adec_mp3_inst1.aud_comp = "OMX.PV.mp3dec";

            if(adec_mp3_inst1.pcmplayback &&
                adec_mp3_inst1.pcm_device_type == PCM_DEC_DEVICE)
            {
               pthread_mutex_init(&adec_mp3_inst1.pcm_buf_lock, 0);
            }
            DEBUG_PRINT(" OMX test app : aud_comp instance = %s\n",adec_mp3_inst1.aud_comp);

    }
    else
    {
            DEBUG_PRINT( "invalid format: \n");
            DEBUG_PRINT( "ex: ./sw-adec-omxmp3-test MP3INPUTFILE ParseHeader PCMPLAYBACK \n");
            DEBUG_PRINT( "FILEWRITE OUTFILENAME BUFFEROPTION PCMDEVICETYPE\n");
            DEBUG_PRINT( "ParseHeader= 1 (Parses MP3 Header) \n");
            DEBUG_PRINT( "ParseHeader= 0 (Uses Default Sampling rate and channel) \n");
            DEBUG_PRINT( "PCMPLAYBACK = 1 (ENABLES PCM PLAYBACK IN NON TUNNEL MODE) \n");
            DEBUG_PRINT( "PCMPLAYBACK = 0 (DISABLES PCM PLAYBACK IN NON TUNNEL MODE) \n");
            DEBUG_PRINT( "FILEWRITE = 1 (ENABLES PCM FILEWRITE IN NON TUNNEL MODE) \n");
            DEBUG_PRINT( "FILEWRITE = 0 (DISABLES PCM FILEWRITE IN NON TUNNEL MODE) \n");
            DEBUG_PRINT( "BUFFER OPTION = 0 (AllocateBuffer case)\n");
            DEBUG_PRINT( "BUFFER OPTION = 1 (UseBuffer case)\n");
            return 0;
    }

   if(adec_mp3_inst1.tunnel == 0)
     adec_mp3_inst1.aud_comp = "OMX.PV.mp3dec";

   DEBUG_PRINT(" OMX test app : aud_comp instance 1= %s\n",adec_mp3_inst1.aud_comp);
   pthread_create (&thread1_id, NULL, &thread_function, &adec_mp3_inst1);
   pthread_join(thread1_id,(void**) &thread1_ret);
   if(0 == thread1_ret)
     DEBUG_PRINT(" Thread 1 ended successfully\n");
   
    /* Deinit OpenMAX */
    OMX_Deinit();

    DEBUG_PRINT("*****************************************\n");
    DEBUG_PRINT("******...TEST COMPLETED...***************\n");
    DEBUG_PRINT("*****************************************\n");
    return 0;
}


int Init_Decoder(struct adec_appdata* adec_appdata)
{
   DEBUG_PRINT("Inside %s \n", __FUNCTION__);
   OMX_ERRORTYPE omxresult;
   OMX_U32 total = 0;
   typedef OMX_U8* OMX_U8_PTR;
   char *role ="audio_decoder.mp3";

   static OMX_CALLBACKTYPE call_back = {
      &EventHandler,&EmptyBufferDone,&FillBufferDone
   };

   /* Init. the OpenMAX Core */
   DEBUG_PRINT("\nInitializing OpenMAX Core....\n");
   omxresult = OMX_Init();

   if(OMX_ErrorNone != omxresult) {
      DEBUG_PRINT("\n Failed to Init OpenMAX core");
      return -1;
   }
   else {
      DEBUG_PRINT("\nOpenMAX Core Init Done\n");
   }

   /* Query for audio decoders*/
   DEBUG_PRINT("Mp3_test: Before entering OMX_GetComponentOfRole");
   OMX_GetComponentsOfRole(role, &total, 0);
   DEBUG_PRINT("\nTotal components of role = %s :%u \n", role ,(unsigned)total);

   DEBUG_PRINT("\nComponent before GEThandle %s \n", adec_appdata->aud_comp);

   omxresult = OMX_GetHandle((OMX_HANDLETYPE*)(&adec_appdata->mp3_dec_handle),
                        (OMX_STRING)adec_appdata->aud_comp, adec_appdata, &call_back);

   if (FAILED(omxresult)) {
      DEBUG_PRINT("\nFailed to Load the  component:%s\n", adec_appdata->aud_comp);
      return -1;
   }
   else {
      DEBUG_PRINT("\nComponent %s is in LOADED state with handle: %p\n", adec_appdata->aud_comp,adec_appdata->mp3_dec_handle);
   }

   /* Get the port information */
   CONFIG_VERSION_SIZE(adec_appdata->portParam);
   omxresult = OMX_GetParameter(adec_appdata->mp3_dec_handle, OMX_IndexParamAudioInit,
                                (OMX_PTR)&(adec_appdata->portParam));

   if(FAILED(omxresult)) {
      DEBUG_PRINT("\nFailed to get Port Param\n");
      return -1;
   }
   else {
      DEBUG_PRINT("\nportParam.nPorts:%u\n", (unsigned)(adec_appdata->portParam.nPorts));
      DEBUG_PRINT("\nportParam.nStartPortNumber:%u\n",
                                          (unsigned)(adec_appdata->portParam.nStartPortNumber));
   }
   return 0;
}

int Play_Decoder(struct adec_appdata* adec_appdata)
{
   int i;
   int Size=0;
   DEBUG_PRINT("Inside %s \n", __FUNCTION__);
   OMX_ERRORTYPE ret;
   OMX_INDEXTYPE index;
   #ifdef PCM_PLAYBACK
   struct msm_audio_config drv_pcm_config;
   #endif  // PCM_PLAYBACK

   DEBUG_PRINT("sizeof[%d]\n", sizeof(OMX_BUFFERHEADERTYPE));

   /* open the i/p and o/p files based on the video file format passed */
   if(open_audio_file(adec_appdata)) {
      DEBUG_PRINT("\n Returning -1");
      return -1;
   }
   /* Query the decoder input min buf requirements */
   CONFIG_VERSION_SIZE(adec_appdata->inputportFmt);

   /* Port for which the Client needs to obtain info */
   adec_appdata->inputportFmt.nPortIndex = adec_appdata->portParam.nStartPortNumber;

   OMX_GetParameter(adec_appdata->mp3_dec_handle,OMX_IndexParamPortDefinition,&adec_appdata->inputportFmt);
   DEBUG_PRINT ("\nDec: Input Buffer Count %u\n",(unsigned) (adec_appdata->inputportFmt.nBufferCountMin));
   DEBUG_PRINT ("\nDec: Input Buffer Size %u\n", (unsigned) (adec_appdata->inputportFmt.nBufferSize));

   if(OMX_DirInput != adec_appdata->inputportFmt.eDir) {
      DEBUG_PRINT ("\nDec: Expect Input Port\n");
      return -1;
   }

   adec_appdata->inputportFmt.nBufferCountActual = adec_appdata->inputportFmt.nBufferCountMin +  5;
   OMX_SetParameter(adec_appdata->mp3_dec_handle,OMX_IndexParamPortDefinition,&adec_appdata->inputportFmt);
   OMX_GetExtensionIndex(adec_appdata->mp3_dec_handle,"OMX.Qualcomm.index.audio.sessionid",&index);
   OMX_GetParameter(adec_appdata->mp3_dec_handle,index,&adec_appdata->streaminfoparam);
#ifdef AUDIOV2
	adec_appdata->session_id = adec_appdata->streaminfoparam.sessionId;
	adec_appdata->devmgr_fd = open("/data/omx_devmgr", O_WRONLY);
	if(adec_appdata->devmgr_fd >= 0)
	{
           adec_appdata->control = 0;
	   write_devctlcmd(adec_appdata->devmgr_fd, "-cmd=register_session_rx -sid=", adec_appdata->session_id);
        }
#endif
   if(adec_appdata->tunnel == 0) {
      /* Query the decoder outport's min buf requirements */
      CONFIG_VERSION_SIZE(adec_appdata->outputportFmt);
      /* Port for which the Client needs to obtain info */
      adec_appdata->outputportFmt.nPortIndex = adec_appdata->portParam.nStartPortNumber + 1;

      OMX_GetParameter(adec_appdata->mp3_dec_handle,OMX_IndexParamPortDefinition,&adec_appdata->outputportFmt);
      DEBUG_PRINT ("\nDec: Output Buffer Count %u\n",(unsigned)( adec_appdata->outputportFmt.nBufferCountMin));
      DEBUG_PRINT ("\nDec: Output Buffer Size %u\n",(unsigned)( adec_appdata->outputportFmt.nBufferSize));

      if(OMX_DirOutput != adec_appdata->outputportFmt.eDir) {
         DEBUG_PRINT ("\nDec: Expect Output Port\n");
         return -1;
      }
    adec_appdata->outputportFmt.nBufferCountActual = adec_appdata->outputportFmt.nBufferCountMin + 3;
    OMX_SetParameter(adec_appdata->mp3_dec_handle,OMX_IndexParamPortDefinition,&adec_appdata->outputportFmt);
   }

   CONFIG_VERSION_SIZE(adec_appdata->mp3param);

   DEBUG_PRINT(" adec_appdata->pcm_device_type = %d\n", adec_appdata->pcm_device_type);
   #ifdef PCM_PLAYBACK
   if(adec_appdata->pcmplayback && adec_appdata->pcm_device_type == PCM_DEC_DEVICE)
   {
      DEBUG_PRINT(" open pcm dec device \n");
      adec_appdata->m_pcmdrv_fd = open("/dev/msm_pcm_dec", O_WRONLY | O_NONBLOCK);
      if (adec_appdata->m_pcmdrv_fd < 0)
      {
          DEBUG_PRINT("Play_Decoder: cannot open pcm_dec device");
          return -1;
      }
      DEBUG_PRINT(" Play_Decoder: open pcm device successfull\n");

      /* Create the Event Thread for PCM Dec driver */
      if (pthread_create(&adec_appdata->m_pcmdrv_evt_thread_id, 0, process_pcm_drv_events,
          adec_appdata) < 0)
      {
          DEBUG_PRINT("\n Event Thread creation for PCM Dec driver FAILED\n");
          return -1;
      }
   }
   else
   {
        int fds[2];
        if (pipe(fds)) {
            DEBUG_PRINT("\n%s: pipe creation failed\n", __FUNCTION__);
        }
        adec_appdata->mp3_hpcm.pipe_in=fds[0];
        adec_appdata->mp3_hpcm.pipe_out=fds[1];
        DEBUG_PRINT("********************************\n");
        DEBUG_PRINT("HPCM PIPES %d %d\n",fds[0],fds[1]);
        DEBUG_PRINT("********************************\n");

      if (pthread_create(&adec_appdata->m_pcmdrv_evt_thread_id, 0, process_hpcm_drv_events,
          adec_appdata) < 0)
      {
          DEBUG_PRINT_ERROR("\n Event Thread creation for PCM Dec driver FAILED\n");
          return -1;
      }
   }
   #endif  // PCM_PLAYBACK

   DEBUG_PRINT ("\nOMX_SendCommand Decoder -> IDLE\n");
   OMX_SendCommand(adec_appdata->mp3_dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);
   /* wait_for_event(); should not wait here event complete status will
      not come until enough buffer are allocated */

   adec_appdata->input_buf_cnt = adec_appdata->inputportFmt.nBufferCountActual; //inputportFmt.nBufferCountMin + 5;
   DEBUG_PRINT("Transition to Idle State succesful...\n");

   if(adec_appdata->buffer_option == USE_BUFFER_CASE)
   {
        /* Use buffer on decoder's I/P port */
        adec_appdata->error = Use_Buffer(adec_appdata,
           adec_appdata->inputportFmt.nPortIndex);
        if (adec_appdata->error != OMX_ErrorNone)
        {
           DEBUG_PRINT ("\nOMX_UseBuffer Input buffer error\n");
           return -1;
        }
        else
        {
            DEBUG_PRINT ("\nOMX_UseBuffer Input buffer success\n");
        }
   }
   else
   {
       /* Allocate buffer on decoder's i/p port */
       adec_appdata->error = Allocate_Buffer(adec_appdata,
           adec_appdata->inputportFmt.nPortIndex);
       if (adec_appdata->error != OMX_ErrorNone) {
           DEBUG_PRINT ("\nOMX_AllocateBuffer Input buffer error\n");
           return -1;
       }
       else {
           DEBUG_PRINT ("\nOMX_AllocateBuffer Input buffer success\n");
       }
   }

   if(adec_appdata->tunnel == 0) {
      adec_appdata->output_buf_cnt = adec_appdata->outputportFmt.nBufferCountActual ;

      if(adec_appdata->buffer_option == USE_BUFFER_CASE)
      {
          /* Use buffer on decoder's O/P port */
          adec_appdata->error = Use_Buffer(adec_appdata,
              adec_appdata->outputportFmt.nPortIndex);
          if (adec_appdata->error != OMX_ErrorNone)
          {
             DEBUG_PRINT ("\nOMX_UseBuffer Output buffer error\n");
             return -1;
          }
          else
          {
             DEBUG_PRINT ("\nOMX_UseBuffer Output buffer success\n");
          }
      }
      else
      {
          /* Allocate buffer on decoder's O/Pp port */
          adec_appdata->error = Allocate_Buffer(adec_appdata,
              adec_appdata->outputportFmt.nPortIndex);
          if (adec_appdata->error != OMX_ErrorNone) {
             DEBUG_PRINT ("\nOMX_AllocateBuffer Output buffer error\n");
             return -1;
          }
          else {
             DEBUG_PRINT ("\nOMX_AllocateBuffer Output buffer success\n");
          }
      }
   }

   wait_for_event(adec_appdata);

   DEBUG_PRINT ("\nOMX_SendCommand Decoder -> Executing\n");
   OMX_SendCommand(adec_appdata->mp3_dec_handle, OMX_CommandStateSet, OMX_StateExecuting,0);
   wait_for_event(adec_appdata);

   if((adec_appdata->tunnel == 0))
   {
      DEBUG_PRINT(" Start sending OMX_FILLthisbuffer\n");

      for(i=0; i < adec_appdata->output_buf_cnt; i++) {
         DEBUG_PRINT ("\nOMX_FillThisBuffer on output buf no.%d\n",i);
         adec_appdata->pOutputBufHdrs[i]->nOutputPortIndex = 1;
         adec_appdata->pOutputBufHdrs[i]->nFlags &= ~OMX_BUFFERFLAG_EOS;
         ret = OMX_FillThisBuffer(adec_appdata->mp3_dec_handle, adec_appdata->pOutputBufHdrs[i]);
         if (OMX_ErrorNone != ret) {
            DEBUG_PRINT("OMX_FillThisBuffer failed with result %d\n", ret);
         }
         else {
            DEBUG_PRINT("OMX_FillThisBuffer success!\n");
         }
      }
   }


   DEBUG_PRINT(" Start sending OMX_emptythisbuffer\n");
   for (i = 0;i < adec_appdata->input_buf_cnt;i++) {
      DEBUG_PRINT ("\nOMX_EmptyThisBuffer on Input buf no.%d\n",i);
      adec_appdata->pInputBufHdrs[i]->nInputPortIndex = 0;
      Size = Read_Buffer(adec_appdata->pInputBufHdrs[i],adec_appdata->inputBufferFile);
      if(Size <=0 ) {
         DEBUG_PRINT("\n readBytes = %d\n", Size);
         DEBUG_PRINT("NO DATA READ\n");
         adec_appdata->bEosOnInputBuf = true;
         Size = 0;
         adec_appdata->pInputBufHdrs[i]->nFlags |= OMX_BUFFERFLAG_EOS;
         DEBUG_PRINT("Play_decoder::EOS or Error while reading file\n");
      }
      adec_appdata->pInputBufHdrs[i]->nFilledLen = Size;
      adec_appdata->pInputBufHdrs[i]->nInputPortIndex = 0;
      adec_appdata->used_ip_buf_cnt++;

      if(adec_appdata->first_buffer)
      {
          adec_appdata->first_buffer = 0;
          ret = parse_mp3_frameheader(adec_appdata->pInputBufHdrs[i],&adec_appdata->mp3Header);
          if(ret != OMX_ErrorNone)
          {
              DEBUG_PRINT("parse_mp3_frameheader return failure\n");
              adec_appdata->mp3Header.sampling_rate = DEFAULT_SAMPLING_RATE;
              adec_appdata->mp3Header.channel_mode  = DEFAULT_CHANNEL_MODE;
          }

          /* Get the Output port PCM configuration details */
          adec_appdata->mp3param.nPortIndex   = 0;
          adec_appdata->mp3param.nSampleRate  = adec_appdata->mp3Header.sampling_rate;
          adec_appdata->mp3param.nChannels    = adec_appdata->mp3Header.channel_mode;
          adec_appdata->mp3param.nBitRate     = 0;
          adec_appdata->mp3param.eChannelMode = OMX_AUDIO_ChannelModeStereo;
          adec_appdata->mp3param.eFormat      = OMX_AUDIO_MP3StreamFormatMP1Layer3;

          if(!adec_appdata->bParseHeader)
          {
              adec_appdata->mp3param.nSampleRate = DEFAULT_SAMPLING_RATE;
              adec_appdata->mp3param.nChannels   = DEFAULT_CHANNEL_MODE;
          }

          OMX_SetParameter(adec_appdata->mp3_dec_handle, OMX_IndexParamAudioMp3,
                          (OMX_PTR)&adec_appdata->mp3param);

          if(adec_appdata->pcmplayback &&
              adec_appdata->pcm_device_type == PCM_DEC_DEVICE)
          {
              DEBUG_PRINT("configure Driver for PCM playback \n");
              ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_GET_CONFIG, &drv_pcm_config);
              drv_pcm_config.sample_rate   = adec_appdata->mp3Header.sampling_rate;
              drv_pcm_config.channel_count = adec_appdata->mp3Header.channel_mode;
              ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_SET_CONFIG, &drv_pcm_config);
          }
      }

      ret = OMX_EmptyThisBuffer(adec_appdata->mp3_dec_handle, adec_appdata->pInputBufHdrs[i]);
      if (OMX_ErrorNone != ret) {
         DEBUG_PRINT("OMX_EmptyThisBuffer failed with result %d\n", ret);
      }
      else {
         DEBUG_PRINT("OMX_EmptyThisBuffer success!\n");
      }
   }

    /* Waiting for EOS or PortSettingsChange*/
    while(1)
    {
        wait_for_event(adec_appdata);
        if(adec_appdata->bOutputEosReached)
        {
           adec_appdata->bReconfigureOutputPort = 0;
           printf("bOutputEosReached breaking\n");
           break;
        }
        else
        {
            if(adec_appdata->tunnel == 0 && adec_appdata->bReconfigureOutputPort)
                process_portreconfig(adec_appdata);
        }
    }

   return 0;
}

unsigned int extract_id3_header_size(OMX_U8* buffer)
{
    unsigned int size = 0;
    OMX_U8* pTemp = NULL;

    if(!buffer)
    {
        return 0;
    }

    pTemp = buffer+6;
    size = ((pTemp[0]&0x7F) << 21);
    size |= ((pTemp[1]&0x7F) << 14);
    size |= ((pTemp[2]&0x7F) << 7);
    size |= ((pTemp[3]&0x7F));

    return (size+10);
}

OMX_ERRORTYPE  parse_mp3_frameheader(OMX_BUFFERHEADERTYPE* buffer,
                                     struct mp3_header *header)
{
    OMX_U8* temp_pBuf1 = NULL;
    unsigned int i = 0;
    unsigned int id3_size = 0;
    OMX_U8 temp;


    for(i=0;i<10;i++)
     DEBUG_PRINT ("\n buffer[%d] = 0x%x",i,buffer->pBuffer[i]);
    if ( buffer->nFilledLen == 0 )
    {
        DEBUG_PRINT ("\n Length is zero hence no point in processing \n");
        return OMX_ErrorNone;
    }

    temp_pBuf1 = buffer->pBuffer;

    i = 0;
    while (i<buffer->nFilledLen)
    {
        if((i < buffer->nFilledLen-2) && (temp_pBuf1[0] == 0x49) &&
            (temp_pBuf1[1] == 0x44) && (temp_pBuf1[2] == 0x33))
        {
            if(i < buffer->nFilledLen-10)
            {
                id3_size = extract_id3_header_size(temp_pBuf1);
                DEBUG_PRINT("\n ID3 tag size = %u\n", id3_size);
            }
            else
            {
                DEBUG_PRINT("\nFull ID3 tag header not available\n");
                return OMX_ErrorMax;
            }

            if(id3_size && i < buffer->nFilledLen-id3_size)
            {
                i += id3_size;
                temp_pBuf1 += id3_size;

                DEBUG_PRINT("\n Skipping valid ID3 tag\n");
                break;
            }
            else
            {
                DEBUG_PRINT("\n ID3 Tag size 0 or exceeds 1st buffer\n");
                return OMX_ErrorMax;
            }
        }
        else if(*temp_pBuf1 == 0xFF )
        {
            break;
        }

        i++;
        temp_pBuf1++;
    }

    if ( i==buffer->nFilledLen )
       return OMX_ErrorMax;

    temp = temp_pBuf1[0];
    header->sync = temp & 0xFF;
    if ( header->sync == 0xFF )
    {
        temp = temp_pBuf1[1];
        header->sync = temp & 0xC0;
        if ( header->sync != 0xC0 )
        {
            DEBUG_PRINT("parse_mp3_frameheader failure");
            return OMX_ErrorMax;
        }
    }
    else
    {
        DEBUG_PRINT("parse_mp3_frameheader failure");
        return OMX_ErrorMax;
    }
    temp = temp_pBuf1[1];
    header->version = (temp & 0x18)>>3;
    header->Layer = (temp & 0x06)>>1;
    temp = temp_pBuf1[2];
    header->sampling_rate = (temp & 0x0C)>>2;
    temp = temp_pBuf1[3];
    header->channel_mode = (temp & 0xC0)>>6;

    DEBUG_PRINT("Channel Mode: %u, Sampling rate: %u and header version: %u from the header\n",
                (unsigned)(header->channel_mode),(unsigned) (header->sampling_rate),(unsigned)( header->version));
    // Stereo, Joint Stereo,Dual Mono)
    if ( (header->channel_mode == 0)||(header->channel_mode == 1)||(header->channel_mode == 2) )
    {
        header->channel_mode = 2;  // stereo
    }
    else if ( header->channel_mode == 3 )
    {
        header->channel_mode = 1; // for all other cases configuring as mono TBD
    }
    else
    {
        header->channel_mode = 2; // if the channel is not recog. making the channel by default to Stereo.
        DEBUG_PRINT("Defauting the channel mode to Stereo");
    }
    header->sampling_rate = mp3_frequency_index[header->sampling_rate][header->version];
    DEBUG_PRINT(" frequency = %u, channels = %u\n",(unsigned)(header->sampling_rate),(unsigned)(header->channel_mode));
    return OMX_ErrorNone;
}


static OMX_ERRORTYPE Allocate_Buffer ( struct adec_appdata* adec_appdata,
                                       OMX_U32 nPortIndex )
{
   OMX_BUFFERHEADERTYPE  ***pBufHdrs = NULL;
   OMX_BUFFERHEADERTYPE *bufHdr = NULL;
   struct msm_audio_pmem_info pmem_info;
   DEBUG_PRINT("Inside %s \n", __FUNCTION__);
   OMX_ERRORTYPE error=OMX_ErrorNone;
   long bufCnt=0;
   long bufCntMin = 0;
   long bufSize = 0;

   if(!adec_appdata || !adec_appdata->mp3_dec_handle)
   {
       DEBUG_PRINT("\nAllocate_Buffer:Invalid i/p parameter\n");
       return OMX_ErrorBadParameter;
   }

   if(nPortIndex == 0)
   {
       pBufHdrs = &adec_appdata->pInputBufHdrs;
       bufCntMin = adec_appdata->input_buf_cnt;
       bufSize = adec_appdata->inputportFmt.nBufferSize;
   }
   else if(nPortIndex == 1)
   {
       pBufHdrs = &adec_appdata->pOutputBufHdrs;
       bufCntMin = adec_appdata->output_buf_cnt;
       bufSize = adec_appdata->outputportFmt.nBufferSize;
   }
   else
   {
       DEBUG_PRINT("\nAllocate_Buffer:Invalid PortIndex\n");
       return OMX_ErrorBadPortIndex;
   }

   *pBufHdrs= (OMX_BUFFERHEADERTYPE **)
                   malloc(sizeof(OMX_BUFFERHEADERTYPE*)*bufCntMin);

   if(*pBufHdrs == NULL)
   {
       DEBUG_PRINT ("\nAllocate_Buffer: *pBufHdrs allocation failed!\n");
       return OMX_ErrorInsufficientResources;
   }

   for(bufCnt=0; bufCnt < bufCntMin; ++bufCnt) {
      DEBUG_PRINT("\n OMX_AllocateBuffer No %ld \n", bufCnt);
      error = OMX_AllocateBuffer(adec_appdata->mp3_dec_handle, &((*pBufHdrs)[bufCnt]),
                                   nPortIndex, NULL, bufSize);

      if(error != OMX_ErrorNone)
      {
          DEBUG_PRINT("\nOMX_AllocateBuffer ERROR\n");
          break;
      }

#ifdef PCM_PLAYBACK
      if(adec_appdata->pcmplayback == 1 && adec_appdata->pcm_device_type == PCM_DEC_DEVICE)
      {
         if(nPortIndex == 1)
         {
             bufHdr = (*pBufHdrs)[bufCnt];

             if(bufHdr)
             {
                  pmem_info.fd = (int)bufHdr->pOutputPortPrivate;
                  pmem_info.vaddr = bufHdr->pBuffer;

                  DEBUG_PRINT ("\n PCMDEC REGISTER_PMEM fd = %d, vaddr = %x",
                      pmem_info.fd,(unsigned) (pmem_info.vaddr));
                  if(0 > ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_REGISTER_PMEM, &pmem_info))
                  {
                      DEBUG_PRINT("\n Error in ioctl AUDIO_REGISTER_PMEM\n");
                      error = OMX_ErrorHardware;
                      break;
                  }
              }
              else
              {
                  DEBUG_PRINT("\nbufHdr is NULL, couldnt REGISTER PMEM\n");
                  error = OMX_ErrorUndefined;
                  break;
              }
          }
      }
#endif

   }

   if(error != OMX_ErrorNone && bufCnt < bufCntMin)
   {
       while(bufCnt)
       {
            bufCnt--;
            bufHdr = (*pBufHdrs)[bufCnt];
            Free_Buffer(adec_appdata, nPortIndex, bufHdr);
       }
       free(*pBufHdrs);
       *pBufHdrs = NULL;
   }

   return error;
}

static OMX_ERRORTYPE Use_Buffer ( struct adec_appdata* adec_appdata,
                                  OMX_U32 nPortIndex )
{
    OMX_BUFFERHEADERTYPE  ***pBufHdrs = NULL;
    OMX_BUFFERHEADERTYPE *bufHdr = NULL;
    OMX_U8 *buffer = NULL;
    struct msm_audio_pmem_info pmem_info;
    OMX_ERRORTYPE error = OMX_ErrorNone;
    long bufCnt = 0;
    long bufCntMin = 0;
    long bufSize = 0;
    int pmem_fd = -1;

    if(!adec_appdata || !adec_appdata->mp3_dec_handle)
    {
       DEBUG_PRINT("\nUse_Buffer:Invalid i/p parameter\n");
       return OMX_ErrorBadParameter;
    }

    if(nPortIndex == 0)
    {
       pBufHdrs = &adec_appdata->pInputBufHdrs;
       bufCntMin = adec_appdata->input_buf_cnt;
       bufSize = adec_appdata->inputportFmt.nBufferSize;
    }
    else if(nPortIndex == 1)
    {
       pBufHdrs = &adec_appdata->pOutputBufHdrs;
       bufCntMin = adec_appdata->output_buf_cnt;
       bufSize = adec_appdata->outputportFmt.nBufferSize;
    }
    else
    {
        DEBUG_PRINT("\nUse_Buffer:Invalid PortIndex\n");
        return OMX_ErrorBadPortIndex;
    }

    DEBUG_PRINT("Inside %s \n", __FUNCTION__);
    *pBufHdrs = (OMX_BUFFERHEADERTYPE **)calloc
        (sizeof(OMX_BUFFERHEADERTYPE*)*bufCntMin, 1);

    if(*pBufHdrs == NULL)
    {
        DEBUG_PRINT ("\nUse_Buffer: *pBufHdrs allocation failed!\n");
        return OMX_ErrorInsufficientResources;
    }

    DEBUG_PRINT("\nUse_Buffer::*pBufHdrs = %p", *pBufHdrs);
    for(bufCnt = 0; bufCnt < bufCntMin; ++bufCnt)
    {
        pmem_fd = open("/dev/pmem_adsp", O_RDWR);

        if (pmem_fd < 0)
        {
            DEBUG_PRINT ("\n pmem_adsp open failed");
            error = OMX_ErrorInsufficientResources;
            break;
        }

        DEBUG_PRINT("\nUse_Buffer:: For Buffer %ld, pmem_fd = %d \n", bufCnt,
           pmem_fd);

        /* Map the PMEM file descriptor into current process address space */
        buffer = (OMX_U8*) mmap( NULL,
                                 bufSize,
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED,
                                 pmem_fd,
                                 0
                                );

        if(MAP_FAILED == buffer)
        {
            DEBUG_PRINT ("\n mmap() failed");
            buffer = NULL;
            close(pmem_fd);
            error = OMX_ErrorInsufficientResources;
            break;
        }

        DEBUG_PRINT("\n Use_Buffer::Client Buf = %p", buffer);
        DEBUG_PRINT("\n OMX_UseBuffer No %ld \n", bufCnt);
        error = OMX_UseBuffer( adec_appdata->mp3_dec_handle, &((*pBufHdrs)[bufCnt]),
                               nPortIndex, (void*)pmem_fd, bufSize, buffer
                              );
        DEBUG_PRINT("\nUse_Buffer::Buf ret = %p", (*pBufHdrs)[bufCnt]);

        if(error != OMX_ErrorNone)
        {
            DEBUG_PRINT("\nOMX_AllocateBuffer ERROR\n");
            munmap(buffer, bufSize);
            buffer = NULL;
            close(pmem_fd);
            break;
        }

#ifdef PCM_PLAYBACK
      if(adec_appdata->pcmplayback == 1 && adec_appdata->pcm_device_type == PCM_DEC_DEVICE)
      {
          if(nPortIndex == 1)
          {
              bufHdr = (*pBufHdrs)[bufCnt];
              if(bufHdr)
              {
                  pmem_info.fd = pmem_fd;
                  pmem_info.vaddr = buffer;
                  DEBUG_PRINT ("\n PCMDEC REGISTER_PMEM fd = %d, vaddr = %x",
                      pmem_info.fd, (unsigned)(pmem_info.vaddr));
                  if(0 > ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_REGISTER_PMEM, &pmem_info))
                  {
                      DEBUG_PRINT("\n Error in ioctl AUDIO_REGISTER_PMEM\n");
                      error = OMX_ErrorHardware;
                      break;
                  }
              }
              else
              {
                  DEBUG_PRINT("\nbufHdr is NULL, couldnt REGISTER PMEM\n");
                  error = OMX_ErrorUndefined;
                  break;
              }
          }
      }
#endif

    }

    if(error != OMX_ErrorNone && bufCnt != bufCntMin)
    {
        while(bufCnt)
        {
            bufCnt--;
            bufHdr = (*pBufHdrs)[bufCnt];
            Free_Buffer(adec_appdata, nPortIndex, bufHdr);
        }
        free(*pBufHdrs);
        *pBufHdrs = NULL;
    }

    return error;
}

static OMX_ERRORTYPE Free_Buffer ( struct adec_appdata* adec_appdata,
                                   OMX_U32 nPortIndex,
                                   OMX_BUFFERHEADERTYPE *bufHdr
                                  )
{
    struct msm_audio_pmem_info audio_pmem_buf;
    OMX_ERRORTYPE error = OMX_ErrorNone;
    int pmem_fd = -1;

    if(!adec_appdata || !adec_appdata->mp3_dec_handle || !bufHdr
        || (nPortIndex > 1))
    {
       DEBUG_PRINT("\nFree_Buffer:Invalid i/p parameters\n");
       return OMX_ErrorBadParameter;
    }

    DEBUG_PRINT("\nFree_Buffer::bufHdr = %p", bufHdr);
    if(adec_appdata->buffer_option == USE_BUFFER_CASE)
    {
        pmem_fd = (int)bufHdr->pAppPrivate;
    }
    else
    {
        if(nPortIndex == 1)
        {
            pmem_fd = (int)bufHdr->pOutputPortPrivate;
        }
    }

#ifdef PCM_PLAYBACK
    if(adec_appdata->pcmplayback && adec_appdata->pcm_device_type == PCM_DEC_DEVICE)
    {
        if(nPortIndex == 1 && pmem_fd > 0)
        {
            audio_pmem_buf.fd = pmem_fd;
            audio_pmem_buf.vaddr = bufHdr->pBuffer;
            DEBUG_PRINT ("\n PCMDEC DEREGISTER_PMEM fd = %d, vaddr = %x",
                audio_pmem_buf.fd,(unsigned)( audio_pmem_buf.vaddr));
            if(0 > ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_DEREGISTER_PMEM, &audio_pmem_buf))
            {
                DEBUG_PRINT("\n Error in ioctl AUDIO_DEREGISTER_PMEM\n");
                error = OMX_ErrorHardware;
            }
        }
    }
#endif

    if(adec_appdata->buffer_option == USE_BUFFER_CASE)
    {
        if (bufHdr->pBuffer &&
            (EINVAL == munmap (bufHdr->pBuffer, bufHdr->nAllocLen)))
        {
            DEBUG_PRINT ("\n Error in Unmapping the buffer %p",
              bufHdr);
        }
        bufHdr->pBuffer = NULL;
        close(pmem_fd);
        DEBUG_PRINT("FREED CLIENT BUFHDR[%p], pmem_fd[%d]", bufHdr, pmem_fd);
    }
    return(OMX_FreeBuffer(adec_appdata->mp3_dec_handle, nPortIndex, bufHdr));
}

static int Read_Buffer (OMX_BUFFERHEADERTYPE  *pBufHdr,FILE* inputBufferFile)
{
   int bytes_read=0;

   pBufHdr->nFilledLen = 0;
   pBufHdr->nFlags |= OMX_BUFFERFLAG_EOS;

   bytes_read = fread(pBufHdr->pBuffer, 1, pBufHdr->nAllocLen , inputBufferFile);
   DEBUG_PRINT ("\nBytes read :%d\n",bytes_read);
   pBufHdr->nFilledLen = bytes_read;
   if(bytes_read == 0) {
      pBufHdr->nFlags |= OMX_BUFFERFLAG_EOS;
      DEBUG_PRINT ("\nBytes read zero\n");
   }
   else {
      pBufHdr->nFlags &= ~OMX_BUFFERFLAG_EOS;
      DEBUG_PRINT ("\nBytes read is Non zero\n");
   }

   return bytes_read;;
}

static int open_audio_file (struct adec_appdata* adec_appdata)
{
   int error_code = 0;
   struct wav_header hdr;
   int header_len = 0;
   memset(&hdr,0,sizeof(hdr));

   hdr.riff_id = ID_RIFF;
   hdr.riff_sz = 0;
   hdr.riff_fmt = ID_WAVE;
   hdr.fmt_id = ID_FMT;
   hdr.fmt_sz = 16;
   hdr.audio_format = FORMAT_PCM;
   hdr.num_channels = 2; // Will be updated in the end
   hdr.sample_rate = 44100; // Will be updated in the end
   hdr.byte_rate = hdr.sample_rate * hdr.num_channels * 2;
   hdr.block_align = hdr.num_channels * 2;
   hdr.bits_per_sample = 16;
   hdr.data_id = ID_DATA;
   hdr.data_sz = 0;

   DEBUG_PRINT("Inside %s filename=%s\n", __FUNCTION__, adec_appdata->in_filename);
   adec_appdata->inputBufferFile = fopen (adec_appdata->in_filename, "rb");
   if (adec_appdata->inputBufferFile == NULL) {
      DEBUG_PRINT("\ni/p file %s could NOT be opened\n",
                     adec_appdata->in_filename);
      error_code = -1;
   }

   if((adec_appdata->tunnel == 0) && (adec_appdata->filewrite == 1)) {
      DEBUG_PRINT("output file is opened\n");
      adec_appdata->outputBufferFile = fopen(adec_appdata->out_filename,"wb");
      if (adec_appdata->outputBufferFile == NULL) {
         DEBUG_PRINT("\no/p file %s could NOT be opened\n",
                       adec_appdata->out_filename);
         error_code = -1;
         return error_code;
      }

      header_len = fwrite(&hdr,1,sizeof(hdr),adec_appdata->outputBufferFile);

      if (header_len <= 0) {
         DEBUG_PRINT("Invalid Wav header \n");
      }
      DEBUG_PRINT(" Length og wav header is %d \n",header_len );
   }
   return error_code;
}

void process_portreconfig(struct adec_appdata* adec_appdata)
{
         int bufCnt,i=0;
         OMX_ERRORTYPE ret;
         struct msm_audio_config drv_pcm_config;

         unsigned int len=0;
         hpcm_info ftb;
         ftb.msg_type = CTRL;
         ftb.hComponent = NULL;
         ftb.bufHdr = NULL;

         DEBUG_PRINT("************************************");
         DEBUG_PRINT("RECIEVED EVENT PORT SETTINGS CHANGED EVENT\n");
         DEBUG_PRINT("******************************************\n");
         if(adec_appdata->start_done)
         sleep(1);
         len= write(adec_appdata->mp3_hpcm.pipe_out,&ftb,sizeof(hpcm_info));
         wait_for_event(adec_appdata);
         DEBUG_PRINT("*PORT SETTINGS CHANGED: FLUSHCOMMAND TO COMPONENT*******\n");
         adec_appdata->flushinprogress = 1;
         OMX_SendCommand(adec_appdata->mp3_dec_handle, OMX_CommandFlush, 1, NULL);
         wait_for_event(adec_appdata);  // output port

         // Send DISABLE command
        OMX_SendCommand(adec_appdata->mp3_dec_handle, OMX_CommandPortDisable, 1, 0);
        DEBUG_PRINT("******************************************\n");
        DEBUG_PRINT("FREEING BUFFERS output_buf_cnt=%d\n",adec_appdata->output_buf_cnt);
        DEBUG_PRINT("******************************************\n");

         // Free output Buffer
         for(bufCnt=0; bufCnt < adec_appdata->output_buf_cnt; ++bufCnt) {
             Free_Buffer(adec_appdata, 1, adec_appdata->pOutputBufHdrs[bufCnt]);
         }

         free(adec_appdata->pOutputBufHdrs);
         adec_appdata->pOutputBufHdrs = NULL;
         // wait for Disable event to come back
         wait_for_event(adec_appdata);
         DEBUG_PRINT("******************************************\n");
         DEBUG_PRINT("DISABLE EVENT RECD\n");
         DEBUG_PRINT("******************************************\n");

         // Send Enable command
         OMX_SendCommand(adec_appdata->mp3_dec_handle, OMX_CommandPortEnable, 1, 0);

         adec_appdata->flushinprogress = 0;

         // AllocateBuffers
         DEBUG_PRINT("******************************************\n");
         DEBUG_PRINT("ALLOC BUFFER AFTER PORT REENABLE");
         DEBUG_PRINT("******************************************\n");

         if(adec_appdata->buffer_option == USE_BUFFER_CASE)
         {

             /* Use buffer on decoder's o/p port */
             adec_appdata->error = Use_Buffer(adec_appdata,
                 adec_appdata->outputportFmt.nPortIndex);
             if (adec_appdata->error != OMX_ErrorNone)
             {
                 DEBUG_PRINT ("\nOMX_UseBuffer Output buffer error\n");
                 return;
             }
             else
             {
                 DEBUG_PRINT ("\nOMX_UseBuffer Output buffer success\n");
             }
         }
         else
         {
             /* Allocate buffer on decoder's o/p port */
             adec_appdata->error = Allocate_Buffer(adec_appdata,
                 adec_appdata->outputportFmt.nPortIndex);
             if (adec_appdata->error != OMX_ErrorNone) {
               DEBUG_PRINT ("\nOMX_AllocateBuffer Output buffer error output_buf_cnt=%d\n",adec_appdata->output_buf_cnt);
               return;
             }
             else {
               DEBUG_PRINT ("\nOMX_AllocateBuffer Output buffer success output_buf_cnt=%d\n",adec_appdata->output_buf_cnt);
             }
         }

         DEBUG_PRINT("******************************************\n");
         DEBUG_PRINT("ENABLE EVENTiHANDLER RECD\n");
         DEBUG_PRINT("******************************************\n");
         // wait for enable event to come back
         wait_for_event(adec_appdata);
         if(adec_appdata->pcmplayback && adec_appdata->pcm_device_type == HOST_PCM_DEVICE
                && adec_appdata->start_done)

         {
            DEBUG_PRINT(" Calling fsync on pcm driver...\n");
            while (fsync(adec_appdata->m_pcmdrv_fd) < 0) {
            printf(" fsync failed\n");
            sleep(1);
         }
         DEBUG_PRINT(" Calling stop on pcm driver...\n");
         ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_STOP, 0);
          DEBUG_PRINT(" Calling flush on pcm driver...\n");
         ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_FLUSH, 0);
         sleep(3);
         OMX_GetParameter(adec_appdata->mp3_dec_handle,OMX_IndexParamAudioMp3,&adec_appdata->mp3param);
         drv_pcm_config.sample_rate = adec_appdata->mp3param.nSampleRate;
         drv_pcm_config.channel_count =adec_appdata-> mp3param.nChannels;
         printf("sample =%lu channel = %lu\n",adec_appdata->mp3param.nSampleRate,adec_appdata->mp3param.nChannels);
         ioctl(adec_appdata->m_pcmdrv_fd, AUDIO_SET_CONFIG, &drv_pcm_config);


         DEBUG_PRINT("Configure Driver for PCM playback \n");
         adec_appdata->start_done = 0;
         adec_appdata->bReconfigureOutputPort = 0;
         }
         DEBUG_PRINT("******************************************\n");
         DEBUG_PRINT("FTB after PORT RENABLE\n");
         DEBUG_PRINT("******************************************\n");
         for(i=0; i < adec_appdata->output_buf_cnt; i++) {
           DEBUG_PRINT ("\nOMX_FillThisBuffer on output buf no.%d\n",i);
           adec_appdata->pOutputBufHdrs[i]->nOutputPortIndex = 1;
           adec_appdata->pOutputBufHdrs[i]->nFlags &= ~OMX_BUFFERFLAG_EOS;
           ret = OMX_FillThisBuffer(adec_appdata->mp3_dec_handle, adec_appdata->pOutputBufHdrs[i]);
           if (OMX_ErrorNone != ret) {
             DEBUG_PRINT("OMX_FillThisBuffer failed with result %d\n", ret);
           }
           else {
             DEBUG_PRINT("OMX_FillThisBuffer success!\n");
           }
        }
}

void write_devctlcmd(int fd, const void *buf, int param){
	int nbytes, nbytesWritten;
	char cmdstr[128];
	snprintf(cmdstr, 128, "%s%d\n", (char *)buf, param);
	nbytes = strlen(cmdstr);
	nbytesWritten = write(fd, cmdstr, nbytes);

	if(nbytes != nbytesWritten)
		printf("Failed to write string \"%s\" to omx_devmgr\n", cmdstr);
}


