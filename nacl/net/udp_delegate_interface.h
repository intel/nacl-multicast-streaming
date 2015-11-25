// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _UDP_DELEGATE_INTERFACE_
#define _UDP_DELEGATE_INTERFACE_

class UDPDelegateInterface {
 public:
  virtual void OnReceived(const char* buffer, int32_t size) = 0;
};

#endif  // _UDP_DELEGATE_INTERFACE_
