// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_LOG_IMPL_H_
#define BASE_LOG_IMPL_H_

#include "ppapi/cpp/instance.h"

#include <sstream>
#include <iostream>

namespace base {

class LoggedStream {

 public:
  explicit LoggedStream(pp::Instance* instance, int level, int curlevel);
  LoggedStream(LoggedStream&& l) = default;
  LoggedStream(const LoggedStream& l) = delete;
  LoggedStream& operator=(LoggedStream&& l) = default;
  LoggedStream& operator=(const LoggedStream& l) = delete;
  ~LoggedStream();

  std::ostringstream& s() { return stream_; }

 private:
  pp::Instance* instance_; // non-owning pointer
  std::ostringstream stream_;

  int log_level_;
  int msg_level_;
};

}

#endif // BASE_LOG_IMPL_H_
