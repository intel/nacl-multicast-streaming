// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHARER_ENVIRONMENT_H_
#define SHARER_ENVIRONMENT_H_

#include "base/macros.h"
#include "base/time/default_tick_clock.h"
#include "logging/log_event_dispatcher.h"

#include "ppapi/cpp/instance.h"

namespace sharer {

class SharerEnvironment {
 public:
  explicit SharerEnvironment(pp::Instance* instance);

  pp::Instance* instance() const { return instance_; }
  base::TickClock* clock() { return &clock_; }
  LogEventDispatcher* logger() { return &logger_; }

 private:
  pp::Instance* instance_;
  base::DefaultTickClock clock_;

  LogEventDispatcher logger_;

  DISALLOW_COPY_AND_ASSIGN(SharerEnvironment);
};

} // namespace sharer

#endif // SHARER_ENVIRONMENT_H_
