/*
* Copyright (c) 2019, 2021 The Linux Foundation. All rights reserved.
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
*
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/
#define LOG_TAG "audio_hw::BatteryListener"
#include <log/log.h>
#ifdef QTI_HEALTH
#include <android/binder_manager.h>
#include <aidl/android/hardware/health/IHealth.h>
#include <aidl/android/hardware/health/IHealthInfoCallback.h>
#include <aidl/android/hardware/health/BnHealthInfoCallback.h>
#else

#include <android/hidl/manager/1.0/IServiceManager.h>
#include <android/hardware/health/2.1/IHealth.h>
#include <android/hardware/health/2.1/IHealthInfoCallback.h>
#include <healthhalutils/HealthHalUtils.h>
#include <hidl/HidlTransportSupport.h>
#endif
#include <thread>
#include "battery_listener.h"

#ifdef QTI_HEALTH
using aidl::android::hardware::health::BatteryStatus;
using aidl::android::hardware::health::HealthInfo;
using aidl::android::hardware::health::IHealthInfoCallback;
using aidl::android::hardware::health::BnHealthInfoCallback;
using aidl::android::hardware::health::IHealth;
#else
using android::hardware::interfacesEqual;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::health::V1_0::BatteryStatus;
using android::hardware::health::V1_0::toString;
using android::hardware::health::V2_1::HealthInfo;
using android::hardware::health::V2_1::IHealthInfoCallback;
using android::hardware::health::V2_1::IHealth;
using android::hardware::health::V2_0::Result;
using android::hidl::manager::V1_0::IServiceManager;
#endif

using namespace std::literals::chrono_literals;

namespace android {

#define GET_HEALTH_SVC_RETRY_CNT 5
#define GET_HEALTH_SVC_WAIT_TIME_MS 500

#ifdef QTI_HEALTH
struct BatteryListenerImpl : public BnHealthInfoCallback {
    typedef std::function<void(bool)> cb_fn_t;
    BatteryListenerImpl(cb_fn_t cb);
    ~BatteryListenerImpl ();
    ndk::ScopedAStatus healthInfoChanged(const HealthInfo& info) override;
    static void serviceDied(void *cookie);
    bool isCharging() {
        std::lock_guard<std::mutex> _l(mLock);
        return statusToBool(mStatus);
    }
    void reset();
    status_t init();
  private:
    std::shared_ptr<IHealth> mHealth;
    BatteryStatus mStatus;
    cb_fn_t mCb;
    std::mutex mLock;
    std::condition_variable mCond;
    std::unique_ptr<std::thread> mThread;
    ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;
    bool mDone;
    bool statusToBool(const BatteryStatus &s) const {
        return (s == BatteryStatus::CHARGING) ||
               (s ==  BatteryStatus::FULL);
    }
};
static std::shared_ptr<BatteryListenerImpl> batteryListener;
#else
struct BatteryListenerImpl : public hardware::health::V2_1::IHealthInfoCallback,
                             public hardware::hidl_death_recipient {
    typedef std::function<void(bool)> cb_fn_t;
    BatteryListenerImpl(cb_fn_t cb);
    virtual ~BatteryListenerImpl ();
    virtual hardware::Return<void> healthInfoChanged(
            const hardware::health::V2_0::HealthInfo& info);
    virtual hardware::Return<void> healthInfoChanged_2_1(
            const hardware::health::V2_1::HealthInfo& info);
    virtual void serviceDied(uint64_t cookie,
                             const wp<hidl::base::V1_0::IBase>& who);
    bool isCharging() {
        std::lock_guard<std::mutex> _l(mLock);
        return statusToBool(mStatus);
    }
    void reset();
  private:
    sp<hardware::health::V2_1::IHealth> mHealth;
    status_t init();
    BatteryStatus mStatus;
    cb_fn_t mCb;
    std::mutex mLock;
    std::condition_variable mCond;
    std::unique_ptr<std::thread> mThread;
    bool mDone;
    bool statusToBool(const BatteryStatus &s) const {
        return (s == BatteryStatus::CHARGING) ||
               (s ==  BatteryStatus::FULL);
    }
};
#endif

status_t BatteryListenerImpl::init()
{
    int tries = 0;
#ifdef QTI_HEALTH
    auto service_name = std::string() + IHealth::descriptor + "/default";
#endif

    if (mHealth != NULL)
        return INVALID_OPERATION;

    do {
#ifdef QTI_HEALTH
        mHealth = IHealth::fromBinder(ndk::SpAIBinder(
            AServiceManager_getService(service_name.c_str())));
#else
        mHealth = IHealth::getService();
#endif
        if (mHealth != NULL)
            break;
        usleep(GET_HEALTH_SVC_WAIT_TIME_MS * 1000);
        tries++;
    } while(tries < GET_HEALTH_SVC_RETRY_CNT);

    if (mHealth == NULL) {
        ALOGE("no health service found, retries %d", tries);
        return NO_INIT;
    } else {
        ALOGI("Get health service in %d tries", tries);
    }
    mStatus = BatteryStatus::UNKNOWN;
#ifdef QTI_HEALTH
    auto ret = mHealth->getChargeStatus(&mStatus);
#else
    auto ret = mHealth->getChargeStatus([&](Result r, BatteryStatus status) {
        if (r != Result::SUCCESS) {
            ALOGE("batterylistener: cannot get battery status");
            return;
        }
        mStatus = status;
    });
#endif
    if (!ret.isOk())
        ALOGE("batterylistener: get charge status transaction error");

    if (mStatus == BatteryStatus::UNKNOWN)
        ALOGW("batterylistener: init: invalid battery status");
    mDone = false;
    mThread = std::make_unique<std::thread>([this]() {
            std::unique_lock<std::mutex> l(mLock);
            BatteryStatus local_status = mStatus;
            while (!mDone) {
                if (local_status == mStatus) {
                    mCond.wait(l);
                    continue;
                }
                local_status = mStatus;
                switch (local_status) {
                    // NOT_CHARGING is a special event that indicates, a battery is connected,
                    // but not charging. This is seen for approx a second
                    // after charger is plugged in. A charging event is eventually received.
                    // We must try to avoid an unnecessary cb to HAL
                    // only to call it again shortly.
                    // An option to deal with this transient event would be to ignore this.
                    // Or process this event with a slight delay (i.e cancel this event
                    // if a different event comes in within a timeout
                    case BatteryStatus::NOT_CHARGING : {
                        auto mStatusnot_ncharging =
                                [this, local_status]() { return mStatus != local_status; };
                        mCond.wait_for(l, 3s, mStatusnot_ncharging);
                        if (mStatusnot_ncharging()) // i.e event changed
                            break;
                    }
                    [[fallthrough]];
                    default:
                        bool c = statusToBool(local_status);
                        ALOGI("healthInfo cb thread: cb %s", c ? "CHARGING" : "NOT CHARGING");
                        l.unlock();
                        mCb(c);
                        l.lock();
                        break;
                }
            }
        });
#ifdef QTI_HEALTH
    mHealth->registerCallback(batteryListener);
    binder_status_t binder_status = AIBinder_linkToDeath(
        mHealth->asBinder().get(), mDeathRecipient.get(), this);
    if (binder_status != STATUS_OK) {
        ALOGE("Failed to link to death, status %d",
            static_cast<int>(binder_status));
        return NO_INIT;
    }
#else
    mHealth->registerCallback(this);
    mHealth->linkToDeath(this, 0 /* cookie */);
#endif
    return NO_ERROR;
}
#ifdef QTI_HEALTH
BatteryListenerImpl::BatteryListenerImpl(cb_fn_t cb) :
        mCb(cb),
        mDeathRecipient(AIBinder_DeathRecipient_new(BatteryListenerImpl::serviceDied))
{

}
#else
BatteryListenerImpl::BatteryListenerImpl(cb_fn_t cb) :
        mCb(cb)
{
    init();
}
#endif
BatteryListenerImpl::~BatteryListenerImpl()
{
#ifdef QTI_HEALTH
    {
        std::lock_guard<std::mutex> _l(mLock);
        mDone = true;
        mCond.notify_one();
    }
#endif
    mThread->join();
}

void BatteryListenerImpl::reset() {
    std::lock_guard<std::mutex> _l(mLock);
    if (mHealth != nullptr) {
#ifdef QTI_HEALTH
        mHealth->unregisterCallback(batteryListener);
        binder_status_t status = AIBinder_unlinkToDeath(
            mHealth->asBinder().get(), mDeathRecipient.get(), this);
        if (status != STATUS_OK && status != STATUS_DEAD_OBJECT)
            ALOGE("Cannot unlink to death");
#else
        mHealth->unregisterCallback(this);
        mHealth->unlinkToDeath(this);
#endif
    }
    mStatus = BatteryStatus::UNKNOWN;
    mDone = true;
    mCond.notify_one();
}
#ifdef QTI_HEALTH
void BatteryListenerImpl::serviceDied(void *cookie)
{
    BatteryListenerImpl *listener = reinterpret_cast<BatteryListenerImpl *>(cookie);
    {
        std::lock_guard<std::mutex> _l(listener->mLock);
        if (listener->mHealth == NULL) {
            ALOGE("health not initialized");
            return;
        }
        ALOGI("health service died, reinit");
        listener->mDone = true;
        listener->mCond.notify_one();
    }
    listener->mThread->join();
    std::lock_guard<std::mutex> _l(listener->mLock);
    listener->mHealth = NULL;
    listener->init();
}
#else
void BatteryListenerImpl::serviceDied(uint64_t cookie __unused,
                                     const wp<hidl::base::V1_0::IBase>& who)
{
    {
        std::lock_guard<std::mutex> _l(mLock);
        if (mHealth == NULL || !interfacesEqual(mHealth, who.promote())) {
            ALOGE("health not initialized or unknown interface died");
            return;
        }
        ALOGI("health service died, reinit");
        mDone = true;
    }
    mThread->join();
    std::lock_guard<std::mutex> _l(mLock);
    init();
}
#endif

// this callback seems to be a SYNC callback and so
// waits for return before next event is issued.
// therefore we need not have a queue to process
// NOT_CHARGING and CHARGING concurrencies.
// Replace single var by a list if this assumption is broken
#ifdef QTI_HEALTH
ndk::ScopedAStatus BatteryListenerImpl::healthInfoChanged(const HealthInfo& info)
{
    ALOGV("healthInfoChanged: %d", info.batteryStatus);
    std::unique_lock<std::mutex> l(mLock);
    if (info.batteryStatus != mStatus) {
        mStatus = info.batteryStatus;
        mCond.notify_one();
    }
    return ndk::ScopedAStatus::ok();
}
#else
Return<void> BatteryListenerImpl::healthInfoChanged(
        const hardware::health::V2_0::HealthInfo& info)
{
    ALOGV("healthInfoChanged: %d", info.legacy.batteryStatus);
    std::unique_lock<std::mutex> l(mLock);
    if (info.legacy.batteryStatus != mStatus) {
        mStatus = info.legacy.batteryStatus;
        mCond.notify_one();
    }
    return Void();
}

Return<void> BatteryListenerImpl::healthInfoChanged_2_1(
        const hardware::health::V2_1::HealthInfo& info) {
    ALOGV("healthInfoChanged_2_1: %d", info.legacy.legacy.batteryStatus);
    healthInfoChanged(info.legacy);
    return Void();
}

static sp<BatteryListenerImpl> batteryListener;
#endif
status_t batteryPropertiesListenerInit(BatteryListenerImpl::cb_fn_t cb)
{
#ifdef QTI_HEALTH
    batteryListener = ndk::SharedRefBase::make<BatteryListenerImpl>(cb);
    return batteryListener->init();
#else
    batteryListener = new BatteryListenerImpl(cb);
    return NO_ERROR;
#endif
}

status_t batteryPropertiesListenerDeinit()
{
    batteryListener->reset();
#ifndef QTI_HEALTH
    batteryListener.clear();
#endif
    return OK;
}

bool batteryPropertiesListenerIsCharging()
{
    return batteryListener->isCharging();
}

} // namespace android

extern "C" {
void battery_properties_listener_init(battery_status_change_fn_t fn)
{
    android::batteryPropertiesListenerInit([=](bool charging) {
                                               fn(charging);
                                          });
}

void battery_properties_listener_deinit()
{
    android::batteryPropertiesListenerDeinit();
}

bool battery_properties_is_charging()
{
    return android::batteryPropertiesListenerIsCharging();
}

} // extern C
