/*
 * Copyright (C) 2017 The Android Open Source Project
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

#define LOG_TAG "BroadcastRadioDefault.tuner"
#define LOG_NDEBUG 0

#include "BroadcastRadio.h"
#include "Tuner.h"

#include <broadcastradio-utils/Utils.h>
#include <log/log.h>

namespace android {
namespace hardware {
namespace broadcastradio {
namespace V1_1 {
namespace implementation {

using namespace std::chrono_literals;

using V1_0::Band;
using V1_0::BandConfig;
using V1_0::Class;
using V1_0::Direction;
using utils::HalRevision;

using std::chrono::milliseconds;
using std::lock_guard;
using std::move;
using std::mutex;
using std::sort;
using std::vector;

const struct {
    milliseconds config = 50ms;
    milliseconds scan = 200ms;
    milliseconds step = 100ms;
    milliseconds tune = 150ms;
} gDefaultDelay;

Tuner::Tuner(V1_0::Class classId, const sp<V1_0::ITunerCallback>& callback)
    : mClassId(classId),
      mCallback(callback),
      mCallback1_1(ITunerCallback::castFrom(callback).withDefault(nullptr)),
      mIsAnalogForced(false) {
        ALOGD("Tuner::Tuner");
	dmhd1000.activate();
	sleep(1);
	dmhd1000.setDTR(true);
	sleep(1);
	dmhd1000.hd_setvolume(50);
	sleep(1);
	dmhd1000.passCB(mCurrentProgram, mCurrentProgramInfo, callback);// mCallback1_1);
	mIsClosed = false;
}

void Tuner::forceClose() {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    dmhd1000.close();
    mIsClosed = true;
    mThread.cancelAll();
}

Return<Result> Tuner::setConfiguration(const BandConfig& config) {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::NOT_INITIALIZED;
    if (mClassId != Class::AM_FM) {
        ALOGE("Can't set AM/FM configuration on SAT/DT radio tuner");
        return Result::INVALID_STATE;
    }

    if (config.lowerLimit >= config.upperLimit) return Result::INVALID_ARGUMENTS;

    auto task = [this, config]() {
        ALOGI("Setting AM/FM config");
        lock_guard<mutex> lk(mMut);

        mAmfmConfig = move(config);
        mAmfmConfig.antennaConnected = true;
        mCurrentProgram = utils::make_selector(mAmfmConfig.type, mAmfmConfig.lowerLimit);

        if (utils::isFm(mAmfmConfig.type)) {
            dmhd1000.tune(931, 0, dmhd1000.BAND_FM);
        } else {
            dmhd1000.tune(1010, 0, dmhd1000.BAND_AM);
        }
	dmhd1000.request_hdsignalstrenth();

        mIsAmfmConfigSet = true;
        mCallback->configChange(Result::OK, mAmfmConfig);
    };
    mThread.schedule(task, gDefaultDelay.config);

    return Result::OK;
}

Return<void> Tuner::getConfiguration(getConfiguration_cb _hidl_cb) {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);

    if (!mIsClosed && mIsAmfmConfigSet) {
        _hidl_cb(Result::OK, mAmfmConfig);
    } else {
        _hidl_cb(Result::NOT_INITIALIZED, {});
    }
    return {};
}

// makes ProgramInfo that points to no program
static ProgramInfo makeDummyProgramInfo(const ProgramSelector& selector) {
    ProgramInfo info11 = {};
    auto& info10 = info11.base;

    ALOGD("%s", __func__);

    utils::getLegacyChannel(selector, &info10.channel, &info10.subChannel);
    info10.tuned = 1;
    info10.stereo = 1;
    info10.digital = 0;
    info10.signalStrength = 50;
    info11.selector = selector;
    info11.flags |= ProgramInfoFlags::LIVE;

    return info11;
}

HalRevision Tuner::getHalRev() const {
    ALOGD("%s", __func__);
    if (mCallback1_1 != nullptr) {
        return HalRevision::V1_1;
    } else {
        return HalRevision::V1_0;
    }
}

void Tuner::tuneInternalLocked(const ProgramSelector& sel) {

    ALOGD("Tuning type: %d, value: %lu", sel.programType, sel.primaryId.value);

    if (sel.programType == static_cast<uint32_t>(ProgramType::AM)){
	ALOGD("Tuning AM, sending command to dmhd...");
        dmhd1000.tune(sel.primaryId.value, 0, dmhd1000.BAND_AM);
    } else if (sel.programType == static_cast<uint32_t>(ProgramType::FM)){
	ALOGD("Tuning FM, sending command to dmhd...");
        dmhd1000.tune(sel.primaryId.value/100, 0, dmhd1000.BAND_FM);
    }
    dmhd1000.request_hdsignalstrenth();
    mIsTuneCompleted = true;

    mCurrentProgramInfo = makeDummyProgramInfo(sel);

    if (mCallback1_1 == nullptr) {
        mCallback->tuneComplete(Result::OK, mCurrentProgramInfo.base);
    } else {
        mCallback1_1->tuneComplete_1_1(Result::OK, mCurrentProgramInfo.selector);
        mCallback1_1->currentProgramInfoChanged(mCurrentProgramInfo); // TODO: this is the one where the channel data comes from.
    }
}

// This is not actually scan. This is SEEK to next channel in whatever direction
Return<Result> Tuner::scan(Direction direction, bool skipSubChannel __unused) {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::NOT_INITIALIZED;

    mIsTuneCompleted = false;
    auto task = [this, direction]() {
        ALOGI("Performing seek %s", toString(direction).c_str());

        lock_guard<mutex> lk(mMut);
        if (direction == Direction::UP){
            dmhd1000.hd_seekup();
        } else {
            dmhd1000.hd_seekdown();
        }
    };
    mThread.schedule(task, gDefaultDelay.scan);

    return Result::OK;
}

Return<Result> Tuner::step(Direction direction, bool skipSubChannel) {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::NOT_INITIALIZED;

    ALOGW_IF(!skipSubChannel, "can't step to next frequency without ignoring subChannel");

    if (!utils::isAmFm(utils::getType(mCurrentProgram))) {
        ALOGE("Can't step in anything else than AM/FM");
        return Result::NOT_INITIALIZED;
    }

    if (!mIsAmfmConfigSet) {
        ALOGW("AM/FM config not set");
        return Result::INVALID_STATE;
    }
    mIsTuneCompleted = false;

    auto task = [this, direction]() {
        ALOGI("Performing step %s", toString(direction).c_str());

        lock_guard<mutex> lk(mMut);

        auto current = utils::getId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY, 0);

        if (direction == Direction::UP) {
            current += mAmfmConfig.spacings[0];
        } else {
            current -= mAmfmConfig.spacings[0];
        }

        if (current > mAmfmConfig.upperLimit) current = mAmfmConfig.lowerLimit;
        if (current < mAmfmConfig.lowerLimit) current = mAmfmConfig.upperLimit;

        tuneInternalLocked(utils::make_selector(mAmfmConfig.type, current));
    };
    mThread.schedule(task, gDefaultDelay.step);

    return Result::OK;
}

Return<Result> Tuner::tune(uint32_t channel, uint32_t subChannel) {
    ALOGD("%s(%d, %d)", __func__, channel, subChannel);
    Band band;
    {
        lock_guard<mutex> lk(mMut);
        band = mAmfmConfig.type;
    }
    return tuneByProgramSelector(utils::make_selector(band, channel, subChannel));
}

Return<Result> Tuner::tuneByProgramSelector(const ProgramSelector& sel) {
    ALOGD("%s(%s)", __func__, toString(sel).c_str());
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::NOT_INITIALIZED;

    // checking if ProgramSelector is valid
    auto programType = utils::getType(sel);
    if (utils::isAmFm(programType)) {
        if (!mIsAmfmConfigSet) {
            ALOGW("AM/FM config not set");
            return Result::INVALID_STATE;
        }

        auto freq = utils::getId(sel, IdentifierType::AMFM_FREQUENCY);
        if (freq < mAmfmConfig.lowerLimit){// || freq > mAmfmConfig.upperLimit) {
	    ALOGD("freq: %lu, lowerLimit: %d, upperLimit: %d", freq, mAmfmConfig.lowerLimit, mAmfmConfig.upperLimit);
            return Result::INVALID_ARGUMENTS;
        }
    } else if (programType == ProgramType::DAB) {
	    ALOGD("Attempting to tune DAB, invalid");
        if (!utils::hasId(sel, IdentifierType::DAB_SIDECC)) return Result::INVALID_ARGUMENTS;
    } else if (programType == ProgramType::DRMO) {
	    ALOGD("Attempting to tune DRMO, invalid");
        if (!utils::hasId(sel, IdentifierType::DRMO_SERVICE_ID)) return Result::INVALID_ARGUMENTS;
    } else if (programType == ProgramType::SXM) {
	    ALOGD("Attempting to tune SXM, invalid");
        if (!utils::hasId(sel, IdentifierType::SXM_SERVICE_ID)) return Result::INVALID_ARGUMENTS;
    } else {
	    ALOGD("Attempting to tune <<unknown>>, invalid");
        return Result::INVALID_ARGUMENTS;
    }

    mIsTuneCompleted = false;
    auto task = [this, sel]() {
        lock_guard<mutex> lk(mMut);
        tuneInternalLocked(sel);
    };
    mThread.schedule(task, gDefaultDelay.tune);

    return Result::OK;
}

Return<Result> Tuner::cancel() {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::NOT_INITIALIZED;

    mThread.cancelAll();
    return Result::OK;
}

Return<Result> Tuner::cancelAnnouncement() {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::NOT_INITIALIZED;

    return Result::OK;
}

// TODO: callback from dmhd1000 to update info from RDS
Return<void> Tuner::getProgramInformation(getProgramInformation_cb _hidl_cb) {
    ALOGD("%s", __func__);
    return getProgramInformation_1_1([&](Result result, const ProgramInfo& info) {
        _hidl_cb(result, info.base);
    });
}

// TODO: callback from dmhd1000 to update mCurrentProgramInfo from RDS
Return<void> Tuner::getProgramInformation_1_1(getProgramInformation_1_1_cb _hidl_cb) {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);

    if (mIsClosed) {
        _hidl_cb(Result::NOT_INITIALIZED, {});
    } else if (mIsTuneCompleted) {
        _hidl_cb(Result::OK, mCurrentProgramInfo);
    } else {
        _hidl_cb(Result::NOT_INITIALIZED, makeDummyProgramInfo(mCurrentProgram));
    }
    return {};
}

Return<ProgramListResult> Tuner::startBackgroundScan() {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return ProgramListResult::NOT_INITIALIZED;

    return ProgramListResult::UNAVAILABLE;
}

Return<void> Tuner::getProgramList(const hidl_vec<VendorKeyValue>& vendorFilter,
                                   getProgramList_cb _hidl_cb) {
    ALOGD("%s(%s)", __func__, toString(vendorFilter).substr(0, 100).c_str());
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) {
        _hidl_cb(ProgramListResult::NOT_INITIALIZED, {});
        return {};
    }

    ALOGD("returning a list of 0 programs");
    _hidl_cb(ProgramListResult::OK, {});
    return {};
}

Return<Result> Tuner::setAnalogForced(bool isForced) {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::NOT_INITIALIZED;
//TODO ENABLE/DISABLE HD
    mIsAnalogForced = isForced;
    return Result::OK;
}

Return<void> Tuner::isAnalogForced(isAnalogForced_cb _hidl_cb) {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);

    if (mIsClosed) {
        _hidl_cb(Result::NOT_INITIALIZED, false);
    } else {
        _hidl_cb(Result::OK, mIsAnalogForced);
    }
    return {};
}

}  // namespace implementation
}  // namespace V1_1
}  // namespace broadcastradio
}  // namespace hardware
}  // namespace android
