/*
 * Copyright 2013 The Android Open Source Project
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

#include "VirtualDisplaySurface.h"
#include "HWComposer.h"

// ---------------------------------------------------------------------------
namespace android {
// ---------------------------------------------------------------------------

VirtualDisplaySurface::VirtualDisplaySurface(HWComposer& hwc, int32_t dispId,
        const sp<IGraphicBufferProducer>& sink, const String8& name)
:   mHwc(hwc),
    mDisplayId(dispId),
    mName(name)
{
    if (mDisplayId >= 0) {
        mInterposer = new BufferQueueInterposer(sink, name);
        mSourceProducer = mInterposer;
    } else {
        mSourceProducer = sink;
    }
}

VirtualDisplaySurface::~VirtualDisplaySurface() {
    if (mAcquiredBuffer != NULL) {
        status_t result = mInterposer->releaseBuffer(Fence::NO_FENCE);
        ALOGE_IF(result != NO_ERROR, "VirtualDisplaySurface \"%s\": "
                "failed to release buffer: %d", mName.string(), result);
    }
}

sp<IGraphicBufferProducer> VirtualDisplaySurface::getIGraphicBufferProducer() const {
    return mSourceProducer;
}

status_t VirtualDisplaySurface::compositionComplete() {
    return NO_ERROR;
}

status_t VirtualDisplaySurface::advanceFrame() {
    if (mInterposer == NULL)
        return NO_ERROR;

    Mutex::Autolock lock(mMutex);
    status_t result = NO_ERROR;

    if (mAcquiredBuffer != NULL) {
        ALOGE("VirtualDisplaySurface \"%s\": "
                "advanceFrame called twice without onFrameCommitted",
                mName.string());
        return INVALID_OPERATION;
    }

    sp<Fence> fence;
    result = mInterposer->acquireBuffer(&mAcquiredBuffer, &fence);
    if (result == BufferQueueInterposer::NO_BUFFER_AVAILABLE) {
        result = mInterposer->pullEmptyBuffer();
        if (result != NO_ERROR)
            return result;
        result = mInterposer->acquireBuffer(&mAcquiredBuffer, &fence);
    }
    if (result != NO_ERROR)
        return result;

    result = mHwc.fbPost(mDisplayId, fence, mAcquiredBuffer);
    if (result == NO_ERROR) {
        result = mHwc.setOutputBuffer(mDisplayId, fence, mAcquiredBuffer);
    }
    return result;
}

void VirtualDisplaySurface::onFrameCommitted() {
    if (mInterposer == NULL)
        return;

    Mutex::Autolock lock(mMutex);
    if (mAcquiredBuffer != NULL) {
        // fbFence signals when reads from the framebuffer are finished
        // outFence signals when writes to the output buffer are finished
        // It's unlikely that there will be an implementation where fbFence
        // signals after outFence (in fact they'll typically be the same
        // sync_pt), but just to be pedantic we merge them so the sink will
        // be sure to wait until both are complete.
        sp<Fence> fbFence = mHwc.getAndResetReleaseFence(mDisplayId);
        sp<Fence> outFence = mHwc.getLastRetireFence(mDisplayId);
        sp<Fence> fence = Fence::merge(
                String8::format("HWC done: %.21s", mName.string()),
                fbFence, outFence);

        status_t result = mInterposer->releaseBuffer(fence);
        ALOGE_IF(result != NO_ERROR, "VirtualDisplaySurface \"%s\": "
                "failed to release buffer: %d", mName.string(), result);
        mAcquiredBuffer.clear();
    }
}

void VirtualDisplaySurface::dump(String8& result) const {
}

// ---------------------------------------------------------------------------
} // namespace android
// ---------------------------------------------------------------------------