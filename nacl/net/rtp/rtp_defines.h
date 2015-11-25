// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_RTP_RTP_DEFINES_H_
#define NET_RTP_RTP_DEFINES_H_

namespace sharer {

static const uint16_t kRtpHeaderLength = 12;
static const uint16_t kSharerHeaderLength = 7;
static const uint8_t kRtpExtensionBitMask = 0x10;
static const uint8_t kSharerKeyFrameBitMask = 0x80;
static const uint8_t kSharerReferenceFrameIdBitMask = 0x40;
static const uint8_t kRtpMarkerBitMask = 0x80;
static const uint8_t kSharerExtensionCountmask = 0x3f;

// Sharer RTP extensions.
static const uint8_t kSharerRtpExtensionAdaptiveLatency = 1;

}  // namespace sharer

#endif  // NET_RTP_RTP_DEFINES_H_
