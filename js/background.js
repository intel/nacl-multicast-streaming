// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var logWindow = null;

function makeURL(toolchain, config) {
  return 'index.html?tc=' + toolchain + '&config=' + config;
}

function createWindow(url) {
  console.log('loading ' + url);
  chrome.app.window.create(url, {
    width: 720,
    height: 800,
    frame: 'none'
  });
}

function createLogWindow(createdCb, closedCb) {
  if (logWindow) {
    console.log('ERROR: log window already open.');
    return;
  }

  console.log('loading log window');

  chrome.app.window.create('log.html', {
    id: "log",
    width: 720,
    height: 800
  },
  function(win) {
    logWindow = win.id;
    win.onClosed.addListener(closedCb);
    win.contentWindow.logConfig = createdCb;
  });
}

function onLaunched(launchData) {
  // Send and XHR to get the URL to load from a configuration file.
  // Normally you won't need to do this; just call:
  //
  // chrome.app.window.create('<your url>', {...});
  //
  // In the SDK we want to be able to load different URLs (for different
  // toolchain/config combinations) from the commandline, so we to read
  // this information from the file "config.json".
  //
  // Use a JSON config file with the following format:
  // {
  //    'toolchain': <name of the toolchain to use>,
  //    'build_config': <array of config options to try to load>
  // }
  var xhr = new XMLHttpRequest();
  xhr.open('GET', 'config.json', true);
  xhr.onload = function() {
    var config = JSON.parse(this.responseText);
    var toolchain = null;
    var build_config = null;
    console.log(config);
    if (config.build_config && config.build_config.length > 0)
      build_config = config.build_config;

    if (config.toolchain)
      toolchain = config.toolchain;

    createWindow(makeURL(toolchain, build_config));
  };
  xhr.onerror = function() {
    // Can't find the config file, just load the default.
    createWindow('index.html');
  };
  xhr.send();
}

chrome.app.runtime.onLaunched.addListener(onLaunched);
