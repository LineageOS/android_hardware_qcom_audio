/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Not a contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef VOICE_EXTN_H
#define VOICE_EXTN_H

int voice_extn_update_calls(struct audio_device *adev);
int voice_extn_get_session_from_use_case(struct audio_device *adev,
                                         const audio_usecase_t usecase_id,
                                         struct voice_session **session);
int voice_extn_init(struct audio_device *adev);
int voice_extn_set_parameters(struct audio_device *adev,
                              struct str_parms *parms);
int voice_extn_is_in_call(struct audio_device *adev, bool *in_call);

#ifndef MULTI_VOICE_SESSION_ENABLED
int voice_extn_update_calls(struct audio_device *adev)
{
    return -ENOSYS;
}

int voice_extn_get_session_from_use_case(struct audio_device *adev,
                                       const audio_usecase_t usecase_id,
                                       struct voice_session **session)
{
    return -ENOSYS;
}

int voice_extn_init(struct audio_device *adev)
{
    return -ENOSYS;
}

int  voice_extn_set_parameters(struct audio_device *adev,
                               struct str_parms *parms)
{
    return -ENOSYS;
}

int voice_extn_is_in_call(struct audio_device *adev, bool *in_call)
{
    return -ENOSYS;
}
#endif

#endif //VOICE_EXTN_H
