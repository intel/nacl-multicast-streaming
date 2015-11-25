// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "clock_drift_smoother.h"

#include "base/logger.h"

#include "ppapi/cpp/logging.h"

ClockDriftSmoother::ClockDriftSmoother(base::TimeDelta time_constant)
    : time_constant_(time_constant), estimate_us_(0.0) {
  PP_DCHECK(time_constant_ > base::TimeDelta());
}

ClockDriftSmoother::~ClockDriftSmoother() {}

base::TimeDelta ClockDriftSmoother::Current() const {
  PP_DCHECK(!last_update_time_.is_null());
  return base::TimeDelta::FromMicroseconds(static_cast<int64_t>(
      estimate_us_ + 0.5));  // Round to nearest microsecond;
}

void ClockDriftSmoother::Reset(base::TimeTicks now,
                               base::TimeDelta measured_offset) {
  PP_DCHECK(!now.is_null());
  last_update_time_ = now;
  estimate_us_ = static_cast<double>(measured_offset.InMicroseconds());
}

void ClockDriftSmoother::Update(base::TimeTicks now,
                                base::TimeDelta measured_offset) {
  PP_DCHECK(!now.is_null());
  if (last_update_time_.is_null()) {
    Reset(now, measured_offset);
  } else if (now < last_update_time_) {
    // |now| is not monotonically non-decreasing.
    ERR() << "'now' shouldn't be higher than last update."
          << " now = " << now << ", last = " << last_update_time_;
    PP_NOTREACHED();
  } else {
    const double elapsed_us =
        static_cast<double>((now - last_update_time_).InMicroseconds());
    last_update_time_ = now;
    const double weight =
        elapsed_us / (elapsed_us + time_constant_.InMicroseconds());
    estimate_us_ = weight * measured_offset.InMicroseconds() +
                   (1.0 - weight) * estimate_us_;
  }
}

// static
base::TimeDelta ClockDriftSmoother::GetDefaultTimeConstant() {
  static const int kDefaultTimeConstantInSeconds = 30;
  return base::TimeDelta::FromSeconds(kDefaultTimeConstantInSeconds);
}
