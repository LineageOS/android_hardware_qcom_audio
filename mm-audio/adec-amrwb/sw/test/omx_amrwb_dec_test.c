
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
#include "QOMX_AudioExtensions.h"
#include "QOMX_AudioIndexExtensions.h"
#include "OMX_Component.h"
#include "pthread.h"
#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/msm_audio.h>
#include<unistd.h>
#include<string.h>
#include <pthread.h>
#ifdef AUDIOV2
#include "control.h"
#endif

#ifdef AUDIOV2
unsigned short session_id;
unsigned short session_id_hpcm;
int device_id;
int control = 0;
const char *device="handset_rx";
int devmgr_fd;
#endif

#include <linux/ioctl.h>

#define SAMPLE_RATE 8000
#define STEREO      2
uint32_t samplerate = 8000;
uint32_t channels = 1;
uint32_t pcmplayback = 0;
uint32_t tunnel      = 0;
uint32_t filewrite   = 0;

QOMX_AUDIO_STREAM_INFO_DATA streaminfoparam;

int sf = 0;
int ch = 0;
int format = 0;

#ifdef _DEBUG

#define DEBUG_PRINT(args...) printf("%s:%d ", __FUNCTION__, __LINE__); \
    printf(args)

#define DEBUG_PRINT_ERROR(args...) printf("%s:%d ", __FUNCTION__, __LINE__); \
    printf(args)

#else

#define DEBUG_PRINT
#define DEBUG_PRINT_ERROR

#endif


#define PCM_PLAYBACK /* To write the pcm decoded data to the msm_pcm device for playback*/

int   m_pcmdrv_fd;

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

/************************************************************************/
/*                GLOBAL DECLARATIONS                     */
/************************************************************************/

pthread_mutex_t lock;
pthread_mutex_t lock1;
pthread_mutexattr_t lock1_attr;
pthread_mutex_t etb_lock1;
pthread_mutex_t etb_lock;
pthread_cond_t etb_cond;

pthread_cond_t cond;
pthread_mutex_t elock;
pthread_cond_t econd;
pthread_cond_t fcond;
FILE * inputBufferFile;
FILE * outputBufferFile;
OMX_PARAM_PORTDEFINITIONTYPE inputportFmt;
OMX_PARAM_PORTDEFINITIONTYPE outputportFmt;

OMX_AUDIO_PARAM_AMRTYPE amrparam;
QOMX_AUDIO_PARAM_AMRWBPLUSTYPE amrwbPlusparam;

OMX_PORT_PARAM_TYPE portParam;
OMX_ERRORTYPE error;
OMX_U8* pBuffer_tmp = NULL;

/* AMRWB specific macros */

//AMR-WB Number of channels
#define AMRWB_CHANNELS 1

//AMR-WB Sampling rate
#define AMRWB_SAMPLE_RATE 16000

//AMR-WB File Header size
#define AMRWB_FILE_HEADER_SIZE 9

/* http://ccrma.stanford.edu/courses/422/projects/WaveFormat/ */

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1

static int bFileclose = 0;

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

static unsigned totaldatalen = 0;

/************************************************************************/
/*                GLOBAL INIT                    */
/************************************************************************/

int input_buf_cnt = 0;
int output_buf_cnt = 0;
int used_ip_buf_cnt = 0;
volatile int event_is_done = 0;
volatile int ebd_event_is_done = 0;
volatile int fbd_event_is_done = 0;
int ebd_cnt;
int bOutputEosReached = 0;
int bInputEosReached = 0;
int bEosOnInputBuf = 0;
int bEosOnOutputBuf = 0;
static int etb_done = 0;
static int etb_event_is_done = 0;

int bFlushing = false;
int bPause    = false;
const char *in_filename;


int timeStampLfile = 0;
int timestampInterval = 100;

//* OMX Spec Version supported by the wrappers. Version = 1.1 */
const OMX_U32 CURRENT_OMX_SPEC_VERSION = 0x00000101;
OMX_COMPONENTTYPE* amrwb_dec_handle = 0;

OMX_BUFFERHEADERTYPE  **pInputBufHdrs = NULL;
OMX_BUFFERHEADERTYPE  **pOutputBufHdrs = NULL;

/************************************************************************/
/*				GLOBAL FUNC DECL                        */
/************************************************************************/
int Init_Decoder(OMX_STRING audio_component);
int Play_Decoder();

OMX_STRING aud_comp;

/**************************************************************************/
/*				STATIC DECLARATIONS                       */
/**************************************************************************/

static int open_audio_file ();
static int Read_Buffer(OMX_BUFFERHEADERTYPE  *pBufHdr );
static OMX_ERRORTYPE Allocate_Buffer ( OMX_COMPONENTTYPE *amrwb_dec_handle,
                                       OMX_BUFFERHEADERTYPE  ***pBufHdrs,
                                       OMX_U32 nPortIndex,
                                       long bufCntMin, long bufSize);


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

static void write_devctlcmd(int fd, const void *buf, int param);

void wait_for_event(void)
{
    pthread_mutex_lock(&lock);
    DEBUG_PRINT("%s: event_is_done=%d", __FUNCTION__, event_is_done);
    while (event_is_done == 0) {
        pthread_cond_wait(&cond, &lock);
    }
    event_is_done = 0;
    pthread_mutex_unlock(&lock);
}

void event_complete(void )
{
    pthread_mutex_lock(&lock);
    if (event_is_done == 0) {
        event_is_done = 1;
        pthread_cond_broadcast(&cond);
    }
    pthread_mutex_unlock(&lock);
}


void etb_wait_for_event(void)
{
    pthread_mutex_lock(&etb_lock);
    DEBUG_PRINT("%s: etb_event_is_done=%d", __FUNCTION__, etb_event_is_done);
    while (etb_event_is_done == 0) {
        pthread_cond_wait(&etb_cond, &etb_lock);
    }
    etb_event_is_done = 0;
    pthread_mutex_unlock(&etb_lock);
}

void etb_event_complete(void )
{
    pthread_mutex_lock(&etb_lock);
    if (etb_event_is_done == 0) {
        etb_event_is_done = 1;
        DEBUG_PRINT("%s: etb_event_is_done=%d", __FUNCTION__, etb_event_is_done);
        pthread_cond_broadcast(&etb_cond);
    }
    pthread_mutex_unlock(&etb_lock);
}


OMX_ERRORTYPE EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                           OMX_IN OMX_PTR pAppData,
                           OMX_IN OMX_EVENTTYPE eEvent,
                           OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2,
                           OMX_IN OMX_PTR pEventData)
{
    DEBUG_PRINT("Function %s \n", __FUNCTION__);
    int bufCnt = 0;
    /* To remove warning for unused variable to keep prototype same */
    (void)hComponent;
    (void)pAppData;
    (void)pEventData;

    switch(eEvent)
    {
    case OMX_EventCmdComplete:
        DEBUG_PRINT("*********************************************\n");
        DEBUG_PRINT("\n OMX_EventCmdComplete \n");
        DEBUG_PRINT("*********************************************\n");
        if(OMX_CommandPortDisable == (OMX_COMMANDTYPE)nData1)
        {
            DEBUG_PRINT("******************************************\n");
            DEBUG_PRINT("Recieved DISABLE Event Command Complete[%lu]\n",nData2);
            DEBUG_PRINT("******************************************\n");
        }
        else if(OMX_CommandPortEnable == (OMX_COMMANDTYPE)nData1)
        {
            DEBUG_PRINT("*********************************************\n");
            DEBUG_PRINT("Recieved ENABLE Event Command Complete[%lu]\n",nData2);
            DEBUG_PRINT("*********************************************\n");
        }
        else if(OMX_CommandFlush== (OMX_COMMANDTYPE)nData1)
        {
            DEBUG_PRINT("*********************************************\n");
            DEBUG_PRINT("Recieved FLUSH Event Command Complete[%lu]\n",nData2);
            DEBUG_PRINT("*********************************************\n");
        }
        event_complete();
        break;
    case OMX_EventError:
        DEBUG_PRINT("*********************************************\n");
        DEBUG_PRINT("\n OMX_EventError \n");
        DEBUG_PRINT("*********************************************\n");
        if(OMX_ErrorInvalidState == (OMX_ERRORTYPE)nData1)
        {
            DEBUG_PRINT("\n OMX_ErrorInvalidState \n");
            for(bufCnt=0; bufCnt < input_buf_cnt; ++bufCnt)
            {
                OMX_FreeBuffer(amrwb_dec_handle, 0, pInputBufHdrs[bufCnt]);
            }
            for(bufCnt=0; bufCnt < output_buf_cnt; ++bufCnt)
            {
                    OMX_FreeBuffer(amrwb_dec_handle, 1, pOutputBufHdrs[bufCnt]);
            }

            DEBUG_PRINT("*********************************************\n");
            DEBUG_PRINT("\n Component Deinitialized \n");
            DEBUG_PRINT("*********************************************\n");
            exit(0);
        }
        break;

    case OMX_EventPortSettingsChanged:
        DEBUG_PRINT("*********************************************\n");
        DEBUG_PRINT("\n OMX_EventPortSettingsChanged \n");
        DEBUG_PRINT("*********************************************\n");
        event_complete();
        break;
    case OMX_EventBufferFlag:
        DEBUG_PRINT("*********************************************\n");
        DEBUG_PRINT("\n OMX_Bufferflag \n");
        DEBUG_PRINT("*********************************************\n");
        bOutputEosReached = true;
        event_complete();
        break;
    default:
        DEBUG_PRINT("\n Unknown Event \n");
        break;
    }
    return OMX_ErrorNone;
}


OMX_ERRORTYPE FillBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                              OMX_IN OMX_PTR pAppData,
                              OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
   unsigned int i=0;
   int bytes_writen = 0;
   static int count = 0;
   static int copy_done = 0;
   static int start_done = 0;
   static int length_filled = 0;
   static int spill_length = 0;
   static int pcm_buf_size = 4800;
   static unsigned int pcm_buf_count = 2;
   struct msm_audio_config drv_pcm_config;

    /* To remove warning for unused variable to keep prototype same */
   (void)pAppData;

   if(count == 0 && pcmplayback)
   {
       DEBUG_PRINT(" open pcm device \n");
       m_pcmdrv_fd = open("/dev/msm_pcm_out", O_RDWR);
       if (m_pcmdrv_fd < 0)
       {
          DEBUG_PRINT("Cannot open audio device\n");
          return -1;
       }
       else
       {
          DEBUG_PRINT("Open pcm device successfull\n");
          DEBUG_PRINT("Configure Driver for PCM playback \n");
          ioctl(m_pcmdrv_fd, AUDIO_GET_CONFIG, &drv_pcm_config);
          DEBUG_PRINT("drv_pcm_config.buffer_count %d \n", drv_pcm_config.buffer_count);
          DEBUG_PRINT("drv_pcm_config.buffer_size %d \n", drv_pcm_config.buffer_size);
          drv_pcm_config.sample_rate = sf;//SAMPLE_RATE; //m_adec_param.nSampleRate;
          drv_pcm_config.channel_count = ch;//channels;  /* 1-> mono 2-> stereo*/
          ioctl(m_pcmdrv_fd, AUDIO_SET_CONFIG, &drv_pcm_config);
          DEBUG_PRINT("Configure Driver for PCM playback \n");
          ioctl(m_pcmdrv_fd, AUDIO_GET_CONFIG, &drv_pcm_config);
          DEBUG_PRINT("drv_pcm_config.buffer_count %d \n", drv_pcm_config.buffer_count);
          DEBUG_PRINT("drv_pcm_config.buffer_size %d \n", drv_pcm_config.buffer_size);
          pcm_buf_size = drv_pcm_config.buffer_size;
          pcm_buf_count = drv_pcm_config.buffer_count;
#ifdef AUDIOV2
          ioctl(m_pcmdrv_fd, AUDIO_GET_SESSION_ID, &session_id_hpcm);
          DEBUG_PRINT("session id 0x%4x \n", session_id_hpcm);
	  if(devmgr_fd >= 0)
	  {
	     write_devctlcmd(devmgr_fd, "-cmd=register_session_rx -sid=",  session_id_hpcm);
	  }
          else
          {
             control = msm_mixer_open("/dev/snd/controlC0", 0);
             if(control < 0)
                printf("ERROR opening the device\n");
             device_id = msm_get_device(device);
             DEBUG_PRINT ("\ndevice_id = %d\n",device_id);
             DEBUG_PRINT("\nsession_id = %d\n",session_id);
             if (msm_en_device(device_id, 1))
             {
                perror("could not enable device\n");
                return -1;
             }
             if (msm_route_stream(1, session_id_hpcm,device_id, 1))
             {
                 DEBUG_PRINT("could not set stream routing\n");
                 return -1;
             }	
          }
#endif
       }
       pBuffer_tmp= (OMX_U8*)malloc(pcm_buf_count*sizeof(OMX_U8)*pcm_buf_size);
       if (pBuffer_tmp == NULL)
       {
         return -1;
       }
       else
       {
         memset(pBuffer_tmp, 0, pcm_buf_count*pcm_buf_size);
       }
   }
   DEBUG_PRINT(" FillBufferDone #%d size %lu\n", count++,pBuffer->nFilledLen);

    if(bEosOnOutputBuf)
        return OMX_ErrorNone;

    if(filewrite == 1)
    {
        bytes_writen =
        fwrite(pBuffer->pBuffer,1,pBuffer->nFilledLen,outputBufferFile);
        DEBUG_PRINT(" FillBufferDone size writen to file  %d\n",bytes_writen);
        totaldatalen += bytes_writen ;
    }

#ifdef PCM_PLAYBACK
    if(pcmplayback && pBuffer->nFilledLen)
    {
        if(start_done == 0)
        {
            if((length_filled+pBuffer->nFilledLen)>=(pcm_buf_count*pcm_buf_size))
            {
                spill_length = (pBuffer->nFilledLen-(pcm_buf_count*pcm_buf_size)+length_filled);
                memcpy (pBuffer_tmp+length_filled, pBuffer->pBuffer, ((pcm_buf_count*pcm_buf_size)-length_filled));
                length_filled = (pcm_buf_count*pcm_buf_size);
                copy_done = 1;
            }
            else
            {
                memcpy (pBuffer_tmp+length_filled, pBuffer->pBuffer, pBuffer->nFilledLen);
               length_filled +=pBuffer->nFilledLen;
            }
            if (copy_done == 1)
            {
                for (i=0; i<pcm_buf_count; i++)
                {
                   if (write(m_pcmdrv_fd, pBuffer_tmp+i*pcm_buf_size, pcm_buf_size ) != pcm_buf_size)
                   {
                      DEBUG_PRINT("FillBufferDone: Write data to PCM failed\n");
                           return -1;
                   }

                }
             DEBUG_PRINT("AUDIO_START called for PCM \n");
             ioctl(m_pcmdrv_fd, AUDIO_START, 0);
             if (spill_length != 0)
             {
                if (write(m_pcmdrv_fd, pBuffer->pBuffer+((pBuffer->nFilledLen)-spill_length), spill_length) != spill_length)
                {
                   DEBUG_PRINT("FillBufferDone: Write data to PCM failed\n");
                   return -1;
                }
             }
             if (pBuffer_tmp)
             {
                 free(pBuffer_tmp);
                 pBuffer_tmp =NULL;
             }
             copy_done = 0;
             start_done = 1;
            }
        }
        else
        {
            if (write(m_pcmdrv_fd, pBuffer->pBuffer, pBuffer->nFilledLen ) !=
                (ssize_t)pBuffer->nFilledLen)
            {
                DEBUG_PRINT("FillBufferDone: Write data to PCM failed\n");
                return OMX_ErrorNone;
            }
        }

        DEBUG_PRINT(" FillBufferDone: writing data to pcm device for play succesfull \n");
    }
#endif   // PCM_PLAYBACK


    if(pBuffer->nFlags != OMX_BUFFERFLAG_EOS)
    {
        DEBUG_PRINT(" FBD calling FTB");
        OMX_FillThisBuffer(hComponent,pBuffer);
    }
    else
    {
        DEBUG_PRINT(" FBD EOS REACHED...........\n");
        bEosOnOutputBuf = true;
        return OMX_ErrorNone;
    }
    return OMX_ErrorNone;
}


OMX_ERRORTYPE EmptyBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                              OMX_IN OMX_PTR pAppData,
                              OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
    int readBytes =0;

    /* To remove warning for unused variable to keep prototype same */
    (void)pAppData;

    DEBUG_PRINT("\nFunction %s cnt[%d]\n", __FUNCTION__, ebd_cnt);
    ebd_cnt++;
    used_ip_buf_cnt--;
    pthread_mutex_lock(&etb_lock1);
    if(!etb_done)
    {
        DEBUG_PRINT("\n*********************************************\n");
        DEBUG_PRINT("Wait till first set of buffers are given to component\n");
        DEBUG_PRINT("\n*********************************************\n");
        etb_done++;
        pthread_mutex_unlock(&etb_lock1);
	DEBUG_PRINT("EBD: Before etb_wait_for_event.....\n");
        etb_wait_for_event();
    }
    else
    {
        pthread_mutex_unlock(&etb_lock1);
    }
    if(bEosOnInputBuf)
    {
        DEBUG_PRINT("\n*********************************************\n");
        DEBUG_PRINT("   EBD::EOS on input port\n ");
        DEBUG_PRINT("*********************************************\n");
        return OMX_ErrorNone;
    }
    else if (true == bFlushing)
    {
        DEBUG_PRINT("omx_amrwb_adec_test: bFlushing is set to TRUE used_ip_buf_cnt=%d\n",used_ip_buf_cnt);
        if (0 == used_ip_buf_cnt)
        {
            bFlushing = false;
        }
        else
        {
            DEBUG_PRINT("omx_amr_adec_test: more buffer to come back\n");
            return OMX_ErrorNone;
        }
    }
    if((readBytes = Read_Buffer(pBuffer)) > 0)
    {
        pBuffer->nFilledLen = readBytes;
        used_ip_buf_cnt++;
        timeStampLfile += timestampInterval;
        pBuffer->nTimeStamp = timeStampLfile;
        OMX_EmptyThisBuffer(hComponent,pBuffer);
    }
    else
    {
        pBuffer->nFlags |= OMX_BUFFERFLAG_EOS;
        used_ip_buf_cnt++;
        bEosOnInputBuf = true;
        pBuffer->nFilledLen = 0;
        timeStampLfile += timestampInterval;
        pBuffer->nTimeStamp = timeStampLfile;
        OMX_EmptyThisBuffer(hComponent,pBuffer);
        DEBUG_PRINT("EBD..Either EOS or Some Error while reading file\n");
    }
    return OMX_ErrorNone;
}

void signal_handler(int sig_id)
{
   if (sig_id == SIGUSR1)
   {
        DEBUG_PRINT("%s Initiate flushing\n", __FUNCTION__);
        bFlushing = true;
        OMX_SendCommand(amrwb_dec_handle, OMX_CommandFlush, OMX_ALL, NULL);
   }
   else if (sig_id == SIGUSR2)
   {
        if (bPause == true)
        {
            DEBUG_PRINT("%s resume playback\n", __FUNCTION__);
            bPause = false;
            OMX_SendCommand(amrwb_dec_handle, OMX_CommandStateSet, OMX_StateExecuting, NULL);
        }
        else
        {
            DEBUG_PRINT("%s pause playback\n", __FUNCTION__);
            bPause = true;
            OMX_SendCommand(amrwb_dec_handle, OMX_CommandStateSet, OMX_StatePause, NULL);
        }
    }
}

int main(int argc, char **argv)
{
    int bufCnt=0;
    OMX_ERRORTYPE result;
    struct sigaction sa;
    struct wav_header hdr;
    int bytes_writen = 0;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &signal_handler;
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);


    pthread_cond_init(&cond, 0);
    pthread_mutex_init(&lock, 0);
    pthread_cond_init(&etb_cond, 0);
    pthread_mutex_init(&etb_lock, 0);
    pthread_mutex_init(&etb_lock1, 0);

    pthread_mutexattr_init(&lock1_attr);
    pthread_mutex_init(&lock1, &lock1_attr);

    if (argc == 6)
    {
        in_filename = argv[1];
        DEBUG_PRINT("argv[1]- file name = %s\n", argv[1]);
        pcmplayback = atoi(argv[2]);
        DEBUG_PRINT("argv[2]- PCM play y/n = %d\n", pcmplayback);
        filewrite = atoi(argv[3]);
        sf = atoi(argv[4]);
        ch = atoi(argv[5]);
    }
    else
    {
        DEBUG_PRINT("\ninvalid format\n");
        DEBUG_PRINT("ex: ./sw-adec-omxamrwb-test AMRINPUTFILE PCMPLAYBACK");
        DEBUG_PRINT("FILEWRITE SAMP-FREQ CHANNELS\n");
	DEBUG_PRINT( "PCMPLAYBACK = 1 (ENABLES PCM PLAYBACK IN NON TUNNEL MODE) \n");
	DEBUG_PRINT( "PCMPLAYBACK = 0 (DISABLES PCM PLAYBACK IN NON TUNNEL MODE) \n");
        DEBUG_PRINT( "FILEWRITE = 1 (ENABLES PCM FILEWRITE IN NON TUNNEL MODE) \n");
        DEBUG_PRINT( "FILEWRITE = 0 (DISABLES PCM FILEWRITE IN NON TUNNEL MODE) \n");
        DEBUG_PRINT( "SAMPLING FREQUENCY:\n");
        DEBUG_PRINT( "CHANNELS = 1 (MONO)\n");
        DEBUG_PRINT( "CHANNELS = 2 (STEREO)\n");
        return 0;
    }

    aud_comp = "OMX.PV.amrdec";

    DEBUG_PRINT(" OMX test app : aud_comp = %s\n",aud_comp);

    if(Init_Decoder(aud_comp)!= 0x00)
    {
        DEBUG_PRINT("Decoder Init failed\n");
        return -1;
    }

    if(Play_Decoder() != 0x00)
    {
        DEBUG_PRINT("Play_Decoder failed\n");
        return -1;
    }

    // Wait till EOS is reached...
    wait_for_event();

    if(bOutputEosReached)
    {
#ifdef PCM_PLAYBACK
        if(1 == pcmplayback)
        {
            sleep(1);
            ioctl(m_pcmdrv_fd, AUDIO_STOP, 0);

#ifdef AUDIOV2
	    if(devmgr_fd >= 0)
	    {
		write_devctlcmd(devmgr_fd, "-cmd=unregister_session_rx -sid=", session_id_hpcm);
	    }
	    else
	     {
		if (msm_route_stream(1, session_id_hpcm, device_id, 0))
		{
			DEBUG_PRINT("\ncould not set stream routing\n");
		}
	    }
#endif
            if(m_pcmdrv_fd >= 0)
            {
                close(m_pcmdrv_fd);
                m_pcmdrv_fd = -1;
                DEBUG_PRINT(" PCM device closed succesfully \n");
            }
            else
            {
                DEBUG_PRINT(" PCM device close failure \n");
            }
        }
#endif // PCM_PLAYBACK

        if(1 == filewrite)
        {
            hdr.riff_id = ID_RIFF;
            hdr.riff_sz = 0;
            hdr.riff_fmt = ID_WAVE;
            hdr.fmt_id = ID_FMT;
            hdr.fmt_sz = 16;
            hdr.audio_format = FORMAT_PCM;
            hdr.num_channels = AMRWB_CHANNELS;
            hdr.sample_rate  = AMRWB_SAMPLE_RATE;
            hdr.byte_rate = hdr.sample_rate * hdr.num_channels * 2;
            hdr.block_align = hdr.num_channels * 2;
            hdr.bits_per_sample = 16;
            hdr.data_id = ID_DATA;
            hdr.data_sz = 0;

            DEBUG_PRINT("output file closed and EOS reached total decoded data length %d\n",totaldatalen);
            hdr.data_sz = totaldatalen;
            hdr.riff_sz = totaldatalen + 8 + 16 + 8;
            fseek(outputBufferFile, 0L , SEEK_SET);
            bytes_writen = fwrite(&hdr,1,sizeof(hdr),outputBufferFile);
            if (bytes_writen <= 0)
            {
                DEBUG_PRINT("Invalid Wav header write failed\n");
            }
            bFileclose = 1;
            fclose(outputBufferFile);
        }

        DEBUG_PRINT("\nMoving the decoder to idle state \n");
        OMX_SendCommand(amrwb_dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);
        wait_for_event();

        DEBUG_PRINT("\nMoving the decoder to loaded state \n");
        OMX_SendCommand(amrwb_dec_handle, OMX_CommandStateSet, OMX_StateLoaded,0);

        DEBUG_PRINT("\nFillBufferDone: Deallocating i/p buffers \n");
        for(bufCnt=0; bufCnt < input_buf_cnt; ++bufCnt)
        {
            OMX_FreeBuffer(amrwb_dec_handle, 0, pInputBufHdrs[bufCnt]);
        }

        DEBUG_PRINT("\nFillBufferDone: Deallocating o/p buffers \n");
        for(bufCnt=0; bufCnt < output_buf_cnt; ++bufCnt) {
                OMX_FreeBuffer(amrwb_dec_handle, 1, pOutputBufHdrs[bufCnt]);
        }

        ebd_cnt=0;
        wait_for_event();
        ebd_cnt=0;

        result = OMX_FreeHandle(amrwb_dec_handle);
        if (result != OMX_ErrorNone)
        {
            DEBUG_PRINT("\nOMX_FreeHandle error. Error code: %d\n", result);
        }
#ifdef AUDIOV2
        if(devmgr_fd >= 0)
        {
           write_devctlcmd(devmgr_fd, "-cmd=unregister_session_rx -sid=", session_id);
           close(devmgr_fd);
        }
        else
        {
           if (msm_route_stream(1,session_id,device_id, 0))
           {
               DEBUG_PRINT("\ncould not set stream routing\n");
               return -1;
           }
           if (msm_en_device(device_id, 0))
           {
               DEBUG_PRINT("\ncould not enable device\n");
               return -1;
           }
           msm_mixer_close();
        }
#endif
        /* Deinit OpenMAX */
        OMX_Deinit();
        fclose(inputBufferFile);
        timeStampLfile = 0;
        amrwb_dec_handle = NULL;
        bInputEosReached = false;
        bOutputEosReached = false;
        bEosOnInputBuf = 0;
        bEosOnOutputBuf = 0;
        pthread_cond_destroy(&cond);
        pthread_cond_destroy(&etb_cond);
        pthread_mutex_destroy(&lock);
        pthread_mutexattr_destroy(&lock1_attr);
        pthread_mutex_destroy(&lock1);
        pthread_mutex_destroy(&etb_lock);
        pthread_mutex_destroy(&etb_lock1);
        etb_done = 0;
        DEBUG_PRINT("*****************************************\n");
        DEBUG_PRINT("******...TEST COMPLETED...***************\n");
        DEBUG_PRINT("*****************************************\n");
    }
    return 0;
}

//int Init_Decoder()
int Init_Decoder(OMX_STRING audio_component)
{
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE omxresult;
    OMX_U32 total = 0;
    OMX_U8** audCompNames;
    typedef OMX_U8* OMX_U8_PTR;
    unsigned int i = 0;
    OMX_STRING role ="audio_decoder.amrwb";

    static OMX_CALLBACKTYPE call_back = {
        &EventHandler,&EmptyBufferDone,&FillBufferDone
    };

    DEBUG_PRINT(" Play_Decoder - pcmplayback = %d\n", pcmplayback);

    /* Init. the OpenMAX Core */
    DEBUG_PRINT("\nInitializing OpenMAX Core....\n");
    omxresult = OMX_Init();

    if(OMX_ErrorNone != omxresult)
    {
        DEBUG_PRINT("\n Failed to Init OpenMAX core");
        return -1;
    }
    else
    {
        DEBUG_PRINT("\nOpenMAX Core Init Done\n");
    }

    /* Query for audio decoders*/
    DEBUG_PRINT("Amrwb_test: Before entering OMX_GetComponentOfRole");
    OMX_GetComponentsOfRole(role, &total, 0);
    DEBUG_PRINT("\nTotal components of role=%s :%lu\n", role, total);

    if(total)
    {
        DEBUG_PRINT("Total number of components = %lu\n", total);
        /* Allocate memory for pointers to component name */
        audCompNames = (OMX_U8**)malloc((sizeof(OMX_U8))*total);

        if(NULL == audCompNames)
        {
            return -1;
        }

        for (i = 0; i < total; ++i)
        {
            audCompNames[i] =
                (OMX_U8*)malloc(sizeof(OMX_U8)*OMX_MAX_STRINGNAME_SIZE);
            if(NULL == audCompNames[i] )
            {
                while (i > 0)
                {
                    free(audCompNames[--i]);
                }
                free(audCompNames);
                return -1;
            }
        }
        DEBUG_PRINT("Before calling OMX_GetComponentsOfRole()\n");
        OMX_GetComponentsOfRole(role, &total, audCompNames);
        DEBUG_PRINT("\nComponents of Role:%s\n", role);
    }
    else
    {
        DEBUG_PRINT("No components found with Role:%s", role);
    }
    omxresult = OMX_GetHandle((OMX_HANDLETYPE*)(&amrwb_dec_handle),
                        (OMX_STRING)audio_component, NULL, &call_back);
    if (FAILED(omxresult))
    {
        DEBUG_PRINT("\nFailed to Load the component:%s\n", audio_component);
        for (i = 0; i < total; ++i)
            free(audCompNames[i]);
	free(audCompNames);
            return -1;
    }
    else
    {
        DEBUG_PRINT("\nComponent is in LOADED state\n");
    }

    /* Get the port information */
    CONFIG_VERSION_SIZE(portParam);
    omxresult = OMX_GetParameter(amrwb_dec_handle, OMX_IndexParamAudioInit,
                                (OMX_PTR)&portParam);

    if(FAILED(omxresult))
    {
        DEBUG_PRINT("\nFailed to get Port Param\n");
        for (i = 0; i < total; ++i)
            free(audCompNames[i]);
	free(audCompNames);
        return -1;
    }
    else
    {
        DEBUG_PRINT("\nportParam.nPorts:%lu\n", portParam.nPorts);
        DEBUG_PRINT("\nportParam.nStartPortNumber:%lu\n",
                                             portParam.nStartPortNumber);
    }
    for (i = 0; i < total; ++i)
        free(audCompNames[i]);
    free(audCompNames);
    return 0;
}

int Play_Decoder()
{
    int i;
    int Size=0;
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE ret;
    OMX_INDEXTYPE index;

    DEBUG_PRINT("sizeof[%d]\n", sizeof(OMX_BUFFERHEADERTYPE));

    /* open the i/p and o/p files based on the video file format passed */
    if(open_audio_file())
    {
        DEBUG_PRINT("\n Returning -1");
        return -1;
    }
    /* Query the decoder input min buf requirements */
    CONFIG_VERSION_SIZE(inputportFmt);

    /* Port for which the Client needs to obtain info */
    inputportFmt.nPortIndex = portParam.nStartPortNumber;

    OMX_GetParameter(amrwb_dec_handle,OMX_IndexParamPortDefinition,&inputportFmt);
    DEBUG_PRINT ("\nDec: Input Buffer Count %lu\n", inputportFmt.nBufferCountMin);
    DEBUG_PRINT ("\nDec: Input Buffer Size %lu\n", inputportFmt.nBufferSize);

    if(OMX_DirInput != inputportFmt.eDir)
    {
        DEBUG_PRINT ("\nDec: Expect Input Port\n");
        return -1;
    }
// Modified to Set the Actual Buffer Count for input port
    inputportFmt.nBufferCountActual = inputportFmt.nBufferCountMin + 3;
    OMX_SetParameter(amrwb_dec_handle,OMX_IndexParamPortDefinition,&inputportFmt);

    /* Query the decoder outport's min buf requirements */
    CONFIG_VERSION_SIZE(outputportFmt);
    /* Port for which the Client needs to obtain info */
    outputportFmt.nPortIndex = portParam.nStartPortNumber + 1;

    OMX_GetParameter(amrwb_dec_handle,OMX_IndexParamPortDefinition,&outputportFmt);
    DEBUG_PRINT ("\nDec: Output Buffer Count %lu\n", outputportFmt.nBufferCountMin);
    DEBUG_PRINT ("\nDec: Output Buffer Size %lu\n", outputportFmt.nBufferSize);

    if(OMX_DirOutput != outputportFmt.eDir)
    {
        DEBUG_PRINT ("\nDec: Expect Output Port\n");
        return -1;
    }
        // Modified to Set the Actual Buffer Count for output port
    outputportFmt.nBufferCountActual = outputportFmt.nBufferCountMin + 1;
    OMX_SetParameter(amrwb_dec_handle,OMX_IndexParamPortDefinition,&outputportFmt);

    CONFIG_VERSION_SIZE(amrparam);
    OMX_GetExtensionIndex(amrwb_dec_handle,"OMX.Qualcomm.index.audio.sessionId",&index);
    OMX_GetParameter(amrwb_dec_handle,index,&streaminfoparam);
#ifdef AUDIOV2
    session_id = streaminfoparam.sessionId;
    devmgr_fd = open("/data/omx_devmgr", O_WRONLY);
    if(devmgr_fd >= 0)
    {
        control = 0;
        write_devctlcmd(devmgr_fd, "-cmd=register_session_rx -sid=", session_id);
    }
    else
    {
	/*
		control = msm_mixer_open("/dev/snd/controlC0", 0);
		if(control < 0)
			printf("ERROR opening the device\n");
		device_id = msm_get_device(device);
		DEBUG_PRINT ("\ndevice_id = %d\n",device_id);
		DEBUG_PRINT("\nsession_id = %d\n",session_id);
		if (msm_en_device(device_id, 1))
		{
			perror("could not enable device\n");
			return -1;
		}

		if (msm_route_stream(1,session_id,device_id, 1))
		{
			perror("could not set stream routing\n");
			return -1;
		}
	*/
    }
#endif

     OMX_GetParameter(amrwb_dec_handle,OMX_IndexParamAudioAmr,&amrparam);
     amrparam.nPortIndex   =  0;
     amrparam.nChannels    =  ch;
     amrparam.eAMRBandMode = OMX_AUDIO_AMRBandModeWB0; //default
     amrparam.eAMRFrameFormat = OMX_AUDIO_AMRFrameFormatFSF;
     OMX_SetParameter(amrwb_dec_handle,OMX_IndexParamAudioAmr,&amrparam);

    DEBUG_PRINT ("\nOMX_SendCommand Decoder -> IDLE\n");
    OMX_SendCommand(amrwb_dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);

    input_buf_cnt = inputportFmt.nBufferCountMin + 3;
    DEBUG_PRINT("Transition to Idle State succesful...\n");
    /* Allocate buffer on decoder's i/p port */
    error = Allocate_Buffer(amrwb_dec_handle, &pInputBufHdrs, inputportFmt.nPortIndex,
                            input_buf_cnt, inputportFmt.nBufferSize);
    if (error != OMX_ErrorNone)
    {
        DEBUG_PRINT ("\nOMX_AllocateBuffer Input buffer error\n");
        return -1;
    }
    else
    {
        DEBUG_PRINT ("\nOMX_AllocateBuffer Input buffer success\n");
    }

    
    output_buf_cnt = outputportFmt.nBufferCountMin  + 1;

    /* Allocate buffer on decoder's O/Pp port */
    error = Allocate_Buffer(amrwb_dec_handle, &pOutputBufHdrs, outputportFmt.nPortIndex,
                                output_buf_cnt, outputportFmt.nBufferSize);
    if (error != OMX_ErrorNone)
    {
        DEBUG_PRINT ("\nOMX_AllocateBuffer Output buffer error\n");
        return -1;
    }
    else
    {
            DEBUG_PRINT ("\nOMX_AllocateBuffer Output buffer success\n");
    }

    wait_for_event();

    DEBUG_PRINT ("\nOMX_SendCommand Decoder -> Executing\n");
    OMX_SendCommand(amrwb_dec_handle, OMX_CommandStateSet, OMX_StateExecuting,0);
    wait_for_event();

    
    DEBUG_PRINT(" Start sending OMX_FILLthisbuffer\n");
    for(i=0; i < output_buf_cnt; i++)
    {
       DEBUG_PRINT ("\nOMX_FillThisBuffer on output buf no.%d\n",i);
       pOutputBufHdrs[i]->nOutputPortIndex = 1;
       pOutputBufHdrs[i]->nFlags &= ~OMX_BUFFERFLAG_EOS;
       ret = OMX_FillThisBuffer(amrwb_dec_handle, pOutputBufHdrs[i]);
       if (OMX_ErrorNone != ret)
       {
           DEBUG_PRINT("OMX_FillThisBuffer failed with result %d\n", ret);
       }
       else
       {
          DEBUG_PRINT("OMX_FillThisBuffer success!\n");
       }
    }

    DEBUG_PRINT(" Start sending OMX_emptythisbuffer\n");
    for (i = 0;i < input_buf_cnt;i++)
    {

		DEBUG_PRINT ("\nOMX_EmptyThisBuffer on Input buf no.%d\n",i);
        pInputBufHdrs[i]->nInputPortIndex = 0;
        Size = Read_Buffer(pInputBufHdrs[i]);
        if(Size <=0 ){
          DEBUG_PRINT("NO DATA READ\n");
          bInputEosReached = true;
          pInputBufHdrs[i]->nFlags= OMX_BUFFERFLAG_EOS;
        }
        pInputBufHdrs[i]->nFilledLen = Size;
        pInputBufHdrs[i]->nInputPortIndex = 0;
        used_ip_buf_cnt++;
        timeStampLfile += timestampInterval;
        pInputBufHdrs[i]->nTimeStamp = timeStampLfile;
        ret = OMX_EmptyThisBuffer(amrwb_dec_handle, pInputBufHdrs[i]);
        if (OMX_ErrorNone != ret) {
            DEBUG_PRINT("OMX_EmptyThisBuffer failed with result %d\n", ret);
        }
        else {
            DEBUG_PRINT("OMX_EmptyThisBuffer success!\n");
        }
        if(Size <=0 ){
            break;//eos reached
        }
    }
    pthread_mutex_lock(&etb_lock1);
    if(etb_done)
    {
        DEBUG_PRINT("\n****************************\n");
        DEBUG_PRINT("Component is waiting for EBD to be releases, BC signal\n");
        DEBUG_PRINT("\n****************************\n");
        etb_event_complete();
    }
    else
    {
        DEBUG_PRINT("\n****************************\n");
        DEBUG_PRINT("EBD not yet happened ...\n");
        DEBUG_PRINT("\n****************************\n");
        etb_done++;
    }
    pthread_mutex_unlock(&etb_lock1);

    return 0;
}

static OMX_ERRORTYPE Allocate_Buffer ( OMX_COMPONENTTYPE *avc_dec_handle,
                                       OMX_BUFFERHEADERTYPE  ***pBufHdrs,
                                       OMX_U32 nPortIndex,
                                       long bufCntMin, long bufSize)
{
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE error=OMX_ErrorNone;
    long bufCnt=0;

    /* To remove warning for unused variable to keep prototype same */
    (void)avc_dec_handle;

    *pBufHdrs= (OMX_BUFFERHEADERTYPE **)
                   malloc(sizeof(OMX_BUFFERHEADERTYPE*)*bufCntMin);

    for(bufCnt=0; bufCnt < bufCntMin; ++bufCnt)
    {
        DEBUG_PRINT("\n OMX_AllocateBuffer No %ld \n", bufCnt);
        error = OMX_AllocateBuffer(amrwb_dec_handle, &((*pBufHdrs)[bufCnt]),
                                   nPortIndex, NULL, bufSize);
    }

    return error;
}

static int Read_Buffer (OMX_BUFFERHEADERTYPE  *pBufHdr )
{

    int bytes_read=0;
    static int totalbytes_read =0;

    DEBUG_PRINT ("\nInside Read_Buffer nAllocLen:%lu\n", pBufHdr->nAllocLen);

    pBufHdr->nFilledLen = 0;
    pBufHdr->nFlags |= OMX_BUFFERFLAG_EOS;
    bytes_read = fread(pBufHdr->pBuffer,
                            1, pBufHdr->nAllocLen, inputBufferFile);
    pBufHdr->nFilledLen = bytes_read;
    totalbytes_read += bytes_read;

    DEBUG_PRINT ("\bytes_read = %d\n",bytes_read);
    DEBUG_PRINT ("\totalbytes_read = %d\n",totalbytes_read);
    if( bytes_read <= 0)
    {
        pBufHdr->nFlags |= OMX_BUFFERFLAG_EOS;
        DEBUG_PRINT ("\nBytes read zero\n");
    }
    else
    {
        pBufHdr->nFlags &= ~OMX_BUFFERFLAG_EOS;
        DEBUG_PRINT ("\nBytes read is Non zero\n");
    }
    return bytes_read;
}

static int open_audio_file ()
{
    int error_code = 0;
    const char *outfilename = "Audio_amrwb.wav";
    struct wav_header hdr;
    int header_len = 0;

    memset(&hdr,0,sizeof(hdr));

    hdr.riff_id = ID_RIFF;
    hdr.riff_sz = 0;
    hdr.riff_fmt = ID_WAVE;
    hdr.fmt_id = ID_FMT;
    hdr.fmt_sz = 16;
    hdr.audio_format = FORMAT_PCM;
    hdr.num_channels = AMRWB_CHANNELS;
    hdr.sample_rate  = AMRWB_SAMPLE_RATE;
    hdr.byte_rate = hdr.sample_rate * hdr.num_channels * 2;
    hdr.block_align = hdr.num_channels * 2;
    hdr.bits_per_sample = 16;
    hdr.data_id = ID_DATA;
    hdr.data_sz = 0;

    DEBUG_PRINT("Inside %s filename=%s\n", __FUNCTION__, in_filename);
    inputBufferFile = fopen (in_filename, "rb");
    if (inputBufferFile == NULL) {
        DEBUG_PRINT("\ni/p file %s could NOT be opened\n",
                                         in_filename);
        error_code = -1;
    }

    if(filewrite == 1)
    {
        DEBUG_PRINT("output file is opened\n");
        outputBufferFile = fopen(outfilename,"wb");
        if (outputBufferFile == NULL)
        {
            DEBUG_PRINT("\no/p file %s could NOT be opened\n",
                                             outfilename);
            error_code = -1;
            return error_code;
        }

        header_len = fwrite(&hdr,1,sizeof(hdr),outputBufferFile);

        if (header_len <= 0)
        {
            DEBUG_PRINT("Invalid Wav header \n");
        }
        DEBUG_PRINT(" Length og wav header is %d \n",header_len );
    }
    return error_code;
}

static void write_devctlcmd(int fd, const void *buf, int param){
	int nbytes, nbytesWritten;
	char cmdstr[128];
	snprintf(cmdstr, 128, "%s%d\n", (char *)buf, param);
	nbytes = strlen(cmdstr);
	nbytesWritten = write(fd, cmdstr, nbytes);

	if(nbytes != nbytesWritten)
		printf("Failed to write string \"%s\" to omx_devmgr\n", cmdstr);
}

