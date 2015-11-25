// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/log_impl.h"

#include "ppapi/cpp/var.h"
#include "ppapi/cpp/var_dictionary.h"

// Define LOGSTDOUTPUT to make log output go to stderr
#define LOGSTDOUTPUT 1

#if defined(LOGSTDOUTPUT)
#include <iostream>
#endif

namespace base {

LoggedStream::LoggedStream(pp::Instance* instance, int logLevel, int msgLevel)
    : instance_(instance),
      log_level_(logLevel),
      msg_level_(msgLevel) {
}

LoggedStream::~LoggedStream() {
#if defined(LOGSTDOUTPUT)
  std::cerr << stream_.str() << std::endl;
#endif
  pp::VarDictionary dict;
  dict.Set(pp::Var("log"), stream_.str());
  dict.Set(pp::Var("level"), msg_level_);
  if (instance_ && msg_level_ >= log_level_) instance_->PostMessage(dict);
  stream_.str("");
}

}
