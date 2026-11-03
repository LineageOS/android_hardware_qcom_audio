/*
 * Copyright (C) 2026 The LineageOS Project
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

/*
 * Open source implementation of the Xiaomi "MIUS" ultrasound proximity
 * extension found in the stock sm6150 audio HAL (audio.primary.sm6150.so).
 *
 * Signal flow on stock:
 *   sensors.mius.proximity.so -> libultrasound.so::ultrasound_enable()
 *     -> SEQPACKET /dev/socket/audio_hw_socket (cmd 0x11, {enable, mode})
 *     -> audio HAL set_parameters("ultrasound-proximity=1|0")
 *     -> hostless RX (earpiece) + TX (mic) PCMs at 96 kHz, mixer paths
 *        "ultrasound-*", "Mi_Ultrasound *" kcontrols in the ADSP driver.
 *
 * Everything below mirrors the behaviour of the stock blob; the DSP does the
 * actual ultrasound generation/processing.
 */

#define LOG_TAG "audio_hw_ultrasound"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cutils/properties.h>
#include <cutils/sockets.h>
#include <cutils/str_parms.h>
#include <log/log.h>
#include <tinyalsa/asoundlib.h>
#include <audio_route/audio_route.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include "ultrasound.h"

/* PCM front ends used by the stock HAL for every ultrasound usecase. */
#define US_DEFAULT_RX_PCM_DEVICE 37
#define US_DEFAULT_TX_PCM_DEVICE 42
#define US_PROP_RX_PCM "ro.vendor.audio.ultrasound.rx_pcm"
#define US_PROP_TX_PCM "ro.vendor.audio.ultrasound.tx_pcm"

#define US_PROP_TYPE "ro.vendor.audio.us.type"
#define US_PROP_MODE "vendor.audio.ultrasound.mode"

/* Parameters */
#define US_PARAM_PROXIMITY "ultrasound-proximity"
#define US_PARAM_PROXIMITY_ALT "ultrasound-sensor"
#define US_PARAM_PRE_PROXIMITY "pre_ultrasound-proximity"
#define US_PARAM_SCREEN_ON "ultrasound-screen-on"

/* Mixer paths (names taken from stock mixer_paths / HAL strings) */
#define US_PATH_USECASE "ultrasound-proximity"
#define US_PATH_USECASE_OUT "ultrasound-proximity-output"
#define US_PATH_USECASE_IN "ultrasound-proximity-input"
#define US_PATH_OUT_DEVICE "ultrasound-output"
#define US_PATH_IN_DEVICE "ultrasound-input"
#define US_PATH_PSEUDO_PORT "ultrasound-on"
#define US_PATH_RAMPDOWN "ultrasound-rampdown"
#define US_PATH_SUSPEND "ultrasound-suspend"
#define US_PATH_SCREEN_ON "ultrasound-screen-on"

/* Mixer controls */
#define US_CTL_RX_DEVICE "Mi_Ultrasound Rx Device"
#define US_CTL_CALIBRATION "Mi_Ultrasound Calibration Data"
#define US_CTL_MODE "Ultrasound Mode"
#define US_CTL_MODE_MI "Mi_Ultrasound Mode"

#define US_CAL_FILE "/mnt/vendor/persist/audio/mi_us_cal.txt"
#define US_CAL_MAX_VALUES 14
#define US_CAL_FILE_MAX 0x8c

/* Socket protocol used by libultrasound.so */
#define US_SOCKET_NAME "audio_hw_socket"
#define US_MSG_SIZE 0x108
#define US_CMD_ULTRASOUND 0x11
#define US_CMD_SET_PARAMETERS 0x12
#define US_CMD_OVERLAY_DYNAMIC 0x13
#define US_MAX_CLIENTS 3

/* Values for "Mi_Ultrasound Rx Device" as derived by the stock HAL */
enum {
    US_RX_DEVICE_HANDSET = 0,
    US_RX_DEVICE_SPEAKER_CALL = 1,
    US_RX_DEVICE_SPEAKER = 2,
};

enum us_out_class {
    US_OUT_NONE = 0,
    US_OUT_HANDSET,
    US_OUT_SPEAKER,
    US_OUT_EXTERNAL,
};

struct us_msg {
    uint32_t cmd;
    uint32_t len;
    uint8_t data[US_MSG_SIZE - 8];
} __attribute__((packed));

static struct pcm_config us_pcm_config = {
    .channels = 1,
    .rate = 96000,
    .period_size = 1024,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
};

static struct {
    struct audio_device *adev;
    bool running;
    bool suspended;
    bool out_device_applied;
    int rx_device;
    struct pcm *rx_pcm;
    struct pcm *tx_pcm;
    char out_usecase_path[64];

    /* socket server */
    bool thread_started;
    pthread_t thread;
    int server_fd;
    int wake_pipe[2];
} us = {
    .rx_device = -1,
    .server_fd = -1,
    .wake_pipe = { -1, -1 },
};

/* ---------------------------------------------------------------------- */
/* helpers                                                                */
/* ---------------------------------------------------------------------- */

static int us_apply(struct audio_device *adev, const char *path)
{
    int ret = audio_route_apply_and_update_path(adev->audio_route, path);
    if (ret)
        ALOGW("%s: could not apply path (%s): %d", __func__, path, ret);
    else
        ALOGV("%s: applied %s", __func__, path);
    return ret;
}

static int us_reset(struct audio_device *adev, const char *path)
{
    int ret = audio_route_reset_and_update_path(adev->audio_route, path);
    if (ret)
        ALOGW("%s: could not reset path (%s): %d", __func__, path, ret);
    else
        ALOGV("%s: reset %s", __func__, path);
    return ret;
}

static int us_set_ctl(struct audio_device *adev, const char *name, int value)
{
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, name);
    if (!ctl) {
        ALOGV("%s: no mixer ctl %s", __func__, name);
        return -ENODEV;
    }
    if (mixer_ctl_set_value(ctl, 0, value) < 0) {
        ALOGE("%s: failed to set %s to %d", __func__, name, value);
        return -EINVAL;
    }
    ALOGD("%s: %s = %d", __func__, name, value);
    return 0;
}

static enum us_out_class us_classify(snd_device_t snd_device)
{
    const char *name;

    if (snd_device < SND_DEVICE_OUT_BEGIN || snd_device >= SND_DEVICE_OUT_END)
        return US_OUT_NONE;

    name = platform_get_snd_device_name(snd_device);
    if (!name || !*name)
        return US_OUT_NONE;

    /* "speaker-and-headphones" etc. still drive the loudspeaker */
    if (strstr(name, "handset"))
        return US_OUT_HANDSET;
    if (strstr(name, "speaker"))
        return US_OUT_SPEAKER;
    return US_OUT_EXTERNAL;
}

static bool us_in_call(struct audio_device *adev)
{
    return adev->mode == AUDIO_MODE_IN_CALL ||
           adev->mode == AUDIO_MODE_IN_COMMUNICATION;
}

static enum us_out_class us_active_output_class(struct audio_device *adev)
{
    bool handset = false, speaker = false, external = false;
    int i;

    for (i = SND_DEVICE_OUT_BEGIN; i < SND_DEVICE_OUT_END; i++) {
        if (adev->snd_dev_ref_cnt[i] <= 0)
            continue;
        switch (us_classify(i)) {
            case US_OUT_HANDSET: handset = true; break;
            case US_OUT_SPEAKER: speaker = true; break;
            case US_OUT_EXTERNAL: external = true; break;
            default: break;
        }
    }

    if (handset)
        return US_OUT_HANDSET;
    if (speaker)
        return US_OUT_SPEAKER;
    if (external)
        return US_OUT_EXTERNAL;
    return US_OUT_NONE;
}

/* ---------------------------------------------------------------------- */
/* engine                                                                 */
/* ---------------------------------------------------------------------- */

static void us_suspend(struct audio_device *adev)
{
    if (us.suspended)
        return;
    ALOGD("%s", __func__);
    us_apply(adev, US_PATH_SUSPEND);
    usleep(50000);
    us.suspended = true;
}

static void us_resume(struct audio_device *adev)
{
    if (!us.suspended)
        return;
    ALOGD("%s", __func__);
    us_reset(adev, US_PATH_SUSPEND);
    us.suspended = false;
}

static void us_set_rx_device(struct audio_device *adev, int rx_device)
{
    /* stock HAL suspends the engine around a device *change* */
    bool pulse;

    if (rx_device < 0 || rx_device == us.rx_device)
        return;

    pulse = us.running && !us.suspended && us.rx_device >= 0;
    if (pulse)
        us_suspend(adev);
    if (us_set_ctl(adev, US_CTL_RX_DEVICE, rx_device) == 0)
        us.rx_device = rx_device;
    if (pulse)
        us_resume(adev);
}

static void us_cal_load(struct audio_device *adev)
{
    char buf[US_CAL_FILE_MAX + 1];
    float values[US_CAL_MAX_VALUES];
    struct mixer_ctl *ctl;
    char *tok, *save = NULL;
    size_t len;
    int count = 0, ret;
    unsigned int num;
    FILE *f;

    memset(values, 0, sizeof(values));

    f = fopen(US_CAL_FILE, "r");
    if (!f) {
        ALOGW("%s: cannot open calibration file %s: %s", __func__,
              US_CAL_FILE, strerror(errno));
        return;
    }
    len = fread(buf, 1, US_CAL_FILE_MAX, f);
    fclose(f);
    if (len == 0) {
        ALOGW("%s: cannot read calibration file %s", __func__, US_CAL_FILE);
        return;
    }
    buf[len] = '\0';

    for (tok = strtok_r(buf, " ", &save); tok && count < US_CAL_MAX_VALUES;
         tok = strtok_r(NULL, " ", &save))
        values[count++] = (float)atof(tok);

    ctl = mixer_get_ctl_by_name(adev->mixer, US_CTL_CALIBRATION);
    if (!ctl) {
        ALOGW("%s: no %s control", __func__, US_CTL_CALIBRATION);
        return;
    }

    num = mixer_ctl_get_num_values(ctl);
    if (mixer_ctl_get_type(ctl) == MIXER_CTL_TYPE_BYTE) {
        /* stock passes the raw 14 floats (56 bytes) */
        if (num > sizeof(values))
            num = sizeof(values);
    } else {
        if (num > US_CAL_MAX_VALUES)
            num = US_CAL_MAX_VALUES;
    }
    ret = mixer_ctl_set_array(ctl, values, num);
    if (ret < 0)
        ALOGE("%s: could not set %s: %d", __func__, US_CTL_CALIBRATION, ret);
    else
        ALOGD("%s: loaded %d calibration values", __func__, count);
}

static int us_pcm_id(const char *prop, int def)
{
    return property_get_int32(prop, def);
}

static void us_apply_out_device(struct audio_device *adev, bool apply)
{
    if (apply == us.out_device_applied)
        return;
    if (apply)
        us_apply(adev, US_PATH_OUT_DEVICE);
    else
        us_reset(adev, US_PATH_OUT_DEVICE);
    us.out_device_applied = apply;
}

/*
 * Mirror of ultrasound_select_devices / ultrasound_extn_get_mode_for_usecase:
 *  - if the earpiece or the speaker is already in use by another usecase,
 *    share it and only tell the DSP which device the tone goes to;
 *  - if only an external device (headset, BT, USB...) is active, suspend;
 *  - otherwise drive the earpiece amp ourselves via "ultrasound-output".
 */
static void us_update_routing_l(struct audio_device *adev)
{
    enum us_out_class cls;

    if (!us.running)
        return;

    cls = us_active_output_class(adev);
    ALOGV("%s: active output class %d", __func__, cls);

    switch (cls) {
        case US_OUT_HANDSET:
            us_apply_out_device(adev, false);
            us_resume(adev);
            us_set_rx_device(adev, US_RX_DEVICE_HANDSET);
            break;
        case US_OUT_SPEAKER:
            us_apply_out_device(adev, false);
            us_resume(adev);
            us_set_rx_device(adev, us_in_call(adev) ? US_RX_DEVICE_SPEAKER_CALL
                                                    : US_RX_DEVICE_SPEAKER);
            break;
        case US_OUT_EXTERNAL:
            ALOGD("%s: external output active, suspending ultrasound", __func__);
            us_suspend(adev);
            us_apply_out_device(adev, false);
            break;
        case US_OUT_NONE:
        default:
            us_resume(adev);
            us_apply_out_device(adev, true);
            us_set_rx_device(adev, US_RX_DEVICE_HANDSET);
            break;
    }
}

static void us_stop_l(struct audio_device *adev)
{
    ALOGD("%s: enter", __func__);

    if (!us.suspended) {
        us_apply(adev, US_PATH_SUSPEND);
        usleep(50000);
        us.suspended = true;
    }

    /* TX */
    if (us.tx_pcm) {
        pcm_close(us.tx_pcm);
        us.tx_pcm = NULL;
    }
    us_reset(adev, US_PATH_USECASE_IN);
    us_reset(adev, US_PATH_IN_DEVICE);

    /* pseudo port off with ramp down */
    us_apply(adev, US_PATH_RAMPDOWN);
    us_reset(adev, US_PATH_RAMPDOWN);
    us_reset(adev, US_PATH_PSEUDO_PORT);

    /* usecase */
    us_reset(adev, US_PATH_USECASE);

    /* RX */
    if (us.rx_pcm) {
        pcm_close(us.rx_pcm);
        us.rx_pcm = NULL;
    }
    if (us.out_usecase_path[0]) {
        us_reset(adev, us.out_usecase_path);
        us.out_usecase_path[0] = '\0';
    }
    us_apply_out_device(adev, false);

    us_reset(adev, US_PATH_SUSPEND);
    us.suspended = false;
    us.running = false;
    us.rx_device = -1;

    ALOGD("%s: exit", __func__);
}

static int us_start_l(struct audio_device *adev)
{
    int rx_id, tx_id, mode, ret = 0;

    if (us.running)
        return 0;

    ALOGD("%s: enter", __func__);

    rx_id = us_pcm_id(US_PROP_RX_PCM, US_DEFAULT_RX_PCM_DEVICE);
    tx_id = us_pcm_id(US_PROP_TX_PCM, US_DEFAULT_TX_PCM_DEVICE);

    us_cal_load(adev);

    us_apply(adev, US_PATH_USECASE);

    mode = property_get_int32(US_PROP_MODE, 0);
    if (mode >= 1 && us_set_ctl(adev, US_CTL_MODE, mode) != 0)
        us_set_ctl(adev, US_CTL_MODE_MI, mode);

    us_apply(adev, US_PATH_PSEUDO_PORT);

    /* mark running so routing helpers act; roll back on failure */
    us.running = true;
    us.suspended = false;

    /* TX: microphone */
    us_apply(adev, US_PATH_IN_DEVICE);
    us_apply(adev, US_PATH_USECASE_IN);
    us.tx_pcm = pcm_open(adev->snd_card, tx_id, PCM_IN, &us_pcm_config);
    if (!us.tx_pcm || !pcm_is_ready(us.tx_pcm)) {
        ALOGE("%s: TX pcm_open(%d,%d) failed: %s", __func__, adev->snd_card,
              tx_id, us.tx_pcm ? pcm_get_error(us.tx_pcm) : "null");
        ret = -EIO;
        goto fail;
    }
    if (pcm_start(us.tx_pcm) < 0) {
        ALOGE("%s: pcm start failed for ultrasound microphone: %s", __func__,
              pcm_get_error(us.tx_pcm));
        ret = -EIO;
        goto fail;
    }

    /* RX: earpiece (or shared output device) */
    us_update_routing_l(adev);
    snprintf(us.out_usecase_path, sizeof(us.out_usecase_path), "%s %s",
             US_PATH_USECASE_OUT,
             us_active_output_class(adev) == US_OUT_SPEAKER ? "speaker" : "handset");
    if (us_apply(adev, us.out_usecase_path) != 0) {
        strlcpy(us.out_usecase_path, US_PATH_USECASE_OUT,
                sizeof(us.out_usecase_path));
        us_apply(adev, us.out_usecase_path);
    }
    us.rx_pcm = pcm_open(adev->snd_card, rx_id, PCM_OUT, &us_pcm_config);
    if (!us.rx_pcm || !pcm_is_ready(us.rx_pcm)) {
        ALOGE("%s: RX pcm_open(%d,%d) failed: %s", __func__, adev->snd_card,
              rx_id, us.rx_pcm ? pcm_get_error(us.rx_pcm) : "null");
        ret = -EIO;
        goto fail;
    }
    if (pcm_start(us.rx_pcm) < 0) {
        ALOGE("%s: pcm start failed for ultrasound receiver: %s", __func__,
              pcm_get_error(us.rx_pcm));
        ret = -EIO;
        goto fail;
    }

    ALOGD("%s: exit - success (rx %d, tx %d)", __func__, rx_id, tx_id);
    return 0;

fail:
    ALOGE("%s: failed to start ultrasound, err %d", __func__, ret);
    us_stop_l(adev);
    return ret;
}

/* ---------------------------------------------------------------------- */
/* public API (called with adev->lock held)                               */
/* ---------------------------------------------------------------------- */

void audio_extn_ultrasound_set_parameters(struct audio_device *adev,
                                          struct str_parms *parms)
{
    char value[32];
    int ret;

    ret = str_parms_get_str(parms, US_PARAM_PROXIMITY, value, sizeof(value));
    if (ret < 0)
        ret = str_parms_get_str(parms, US_PARAM_PROXIMITY_ALT, value,
                                sizeof(value));
    if (ret >= 0) {
        bool enable = atoi(value) != 0 || !strcmp(value, "on") ||
                      !strcmp(value, "true");
        ALOGD("%s: proximity %s (running %d)", __func__, value, us.running);
        if (enable)
            us_start_l(adev);
        else if (us.running)
            us_stop_l(adev);
    }

    ret = str_parms_get_str(parms, US_PARAM_PRE_PROXIMITY, value, sizeof(value));
    if (ret >= 0)
        ALOGD("%s: pre enable %s (ignored)", __func__, value);

    ret = str_parms_get_str(parms, US_PARAM_SCREEN_ON, value, sizeof(value));
    if (ret >= 0 && atoi(value) == 1) {
        ALOGD("%s: power button pressed", __func__);
        us_apply(adev, US_PATH_SCREEN_ON);
        us_reset(adev, US_PATH_SCREEN_ON);
    }
}

void audio_extn_ultrasound_pre_enable_snd_device(struct audio_device *adev,
                                                 snd_device_t snd_device)
{
    enum us_out_class cls;

    if (!us.running || !us.out_device_applied)
        return;
    if (snd_device < SND_DEVICE_OUT_BEGIN || snd_device >= SND_DEVICE_OUT_END)
        return;
    if (adev->snd_dev_ref_cnt[snd_device] > 0)
        return;

    cls = us_classify(snd_device);
    /*
     * Release the earpiece amp before the new device path is applied so that
     * resetting "ultrasound-output" doesn't undo the new device's controls.
     */
    if (cls == US_OUT_HANDSET || cls == US_OUT_SPEAKER || cls == US_OUT_EXTERNAL)
        us_apply_out_device(adev, false);
}

void audio_extn_ultrasound_update_routing(struct audio_device *adev)
{
    us_update_routing_l(adev);
}

/* ---------------------------------------------------------------------- */
/* audio_hw_socket server (libultrasound.so client)                       */
/* ---------------------------------------------------------------------- */

static bool us_type_is_mius(void)
{
    char type[PROPERTY_VALUE_MAX];
    property_get(US_PROP_TYPE, type, "default");
    return !strcmp(type, "mius");
}

static void us_dispatch(int fd, struct us_msg *msg)
{
    struct audio_hw_device *dev = &us.adev->device;
    uint8_t reply[2] = { 1, 0 };

    switch (msg->cmd) {
        case US_CMD_ULTRASOUND: {
            uint8_t enable = msg->data[0];
            uint8_t mode = msg->data[1];
            ALOGD("%s: ultrasound enable %d mode %d", __func__, enable, mode);
            if (!us_type_is_mius() && mode == 7) {
                ALOGW("%s: cover mode not supported", __func__);
                break;
            }
            if (enable) {
                dev->set_parameters(dev, US_PARAM_PROXIMITY "=1");
            } else {
                /* stock waits 100ms before turning the engine off */
                usleep(100000);
                dev->set_parameters(dev, US_PARAM_PROXIMITY "=0");
            }
            break;
        }
        case US_CMD_SET_PARAMETERS: {
            uint32_t len = msg->len;
            if (len >= sizeof(msg->data))
                len = sizeof(msg->data) - 1;
            msg->data[len] = '\0';
            ALOGD("%s: set_parameters(%s)", __func__, (char *)msg->data);
            dev->set_parameters(dev, (char *)msg->data);
            break;
        }
        case US_CMD_OVERLAY_DYNAMIC:
            ALOGD("%s: overlay update not supported, ignoring", __func__);
            break;
        default:
            ALOGE("%s: unknown command 0x%x", __func__, msg->cmd);
            break;
    }

    if (write(fd, reply, sizeof(reply)) < 0)
        ALOGE("%s: write failed: %s", __func__, strerror(errno));
}

static void *us_socket_thread(void *arg __unused)
{
    struct pollfd fds[2 + US_MAX_CLIENTS];
    int clients[US_MAX_CLIENTS];
    int i, n;

    for (i = 0; i < US_MAX_CLIENTS; i++)
        clients[i] = -1;

    for (;;) {
        int nfds = 0;

        fds[nfds].fd = us.wake_pipe[0];
        fds[nfds++].events = POLLIN;
        fds[nfds].fd = us.server_fd;
        fds[nfds++].events = POLLIN;
        for (i = 0; i < US_MAX_CLIENTS; i++) {
            fds[nfds].fd = clients[i];
            fds[nfds++].events = POLLIN;
        }

        n = poll(fds, nfds, -1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            ALOGE("%s: poll failed: %s", __func__, strerror(errno));
            break;
        }

        if (fds[0].revents)
            break;

        if (fds[1].revents & POLLIN) {
            int c = accept(us.server_fd, NULL, NULL);
            if (c >= 0) {
                for (i = 0; i < US_MAX_CLIENTS && clients[i] >= 0; i++);
                if (i == US_MAX_CLIENTS) {
                    ALOGE("%s: too many clients", __func__);
                    close(c);
                } else {
                    clients[i] = c;
                    ALOGD("%s: client %d connected", __func__, c);
                }
            }
        }

        for (i = 0; i < US_MAX_CLIENTS; i++) {
            short ev = fds[2 + i].revents;
            struct us_msg msg;
            ssize_t r;

            if (clients[i] < 0 || !ev)
                continue;

            if (ev & POLLIN) {
                memset(&msg, 0, sizeof(msg));
                r = read(clients[i], &msg, sizeof(msg));
                if (r > 0) {
                    if (r < 8) {
                        ALOGE("%s: short message (%d)", __func__, (int)r);
                        continue;
                    }
                    us_dispatch(clients[i], &msg);
                    continue;
                }
                if (r < 0 && errno == EINTR)
                    continue;
            }
            /* hangup, error or EOF */
            ALOGD("%s: client %d disconnected", __func__, clients[i]);
            close(clients[i]);
            clients[i] = -1;
        }
    }

    for (i = 0; i < US_MAX_CLIENTS; i++)
        if (clients[i] >= 0)
            close(clients[i]);
    return NULL;
}

static void us_socket_start(void)
{
    us.server_fd = android_get_control_socket(US_SOCKET_NAME);
    if (us.server_fd < 0) {
        ALOGW("%s: no %s control socket, libultrasound clients unsupported "
              "(add 'socket audio_hw_socket seqpacket 0666 system system' "
              "to the audio HAL service)", __func__, US_SOCKET_NAME);
        return;
    }
    if (listen(us.server_fd, US_MAX_CLIENTS) < 0) {
        ALOGE("%s: listen failed: %s", __func__, strerror(errno));
        goto err;
    }
    if (pipe(us.wake_pipe) < 0) {
        ALOGE("%s: pipe failed: %s", __func__, strerror(errno));
        goto err;
    }
    if (pthread_create(&us.thread, NULL, us_socket_thread, NULL)) {
        ALOGE("%s: pthread_create failed", __func__);
        close(us.wake_pipe[0]);
        close(us.wake_pipe[1]);
        us.wake_pipe[0] = us.wake_pipe[1] = -1;
        goto err;
    }
    us.thread_started = true;
    ALOGD("%s: listening on %s", __func__, US_SOCKET_NAME);
    return;

err:
    /* the control socket fd is owned by init; leave it open */
    us.server_fd = -1;
}

static void us_socket_stop(void)
{
    if (!us.thread_started)
        return;
    if (write(us.wake_pipe[1], "x", 1) < 0)
        ALOGE("%s: write failed: %s", __func__, strerror(errno));
    pthread_join(us.thread, NULL);
    close(us.wake_pipe[0]);
    close(us.wake_pipe[1]);
    us.wake_pipe[0] = us.wake_pipe[1] = -1;
    us.thread_started = false;
}

/* ---------------------------------------------------------------------- */

int audio_extn_ultrasound_init(struct audio_device *adev)
{
    us.adev = adev;
    us.running = false;
    us.suspended = false;
    us.out_device_applied = false;
    us.rx_device = -1;

    /* Make sure a stale suspend from a previous HAL instance is cleared. */
    us_reset(adev, US_PATH_SUSPEND);

    us_socket_start();
    return 0;
}

void audio_extn_ultrasound_deinit(void)
{
    us_socket_stop();
    if (us.adev && us.running) {
        pthread_mutex_lock(&us.adev->lock);
        us_stop_l(us.adev);
        pthread_mutex_unlock(&us.adev->lock);
    }
    us.adev = NULL;
}
