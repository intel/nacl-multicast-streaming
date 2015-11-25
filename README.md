NaCl Multicast Streaming
========================

NaCl module that uses multicast to share a track from a MediaStream Javascript
object. The track is passed to the module via the (currently) postMessage NaCl
API.

Build
-----

This project contains the NaCl module, as well as a sample App demonstrating
the interface.

In order to build the NaCl part, a NaCl SDK is required. At least version 46
must be used for this.

Just enter the nacl/ directory, and run make, also passing the NACL_SDK_ROOT
absolute path to it. Something like this:

make -j5 NACL_SDK_ROOT=<path to nacl sdk root>

Other options, like Release/Debug version, and what toolchain to use, could
also be given to the make command.


Installation
------------

 - Put the Chrome browser in developer mode (check the "developer mode"
   checkbox on the Extensions page.
 - Click on "Load unpacked extension..."
 - Select the app root directory and click "Open".

Notice that you don't have to go to the nacl/ directory, nor select any file,
just the app root folder itself.


Run
---

The app should appear on the list of apps/extensions. Click on the small
"launch"  link. A new window should appear.

There are two ways of testing this app: just on the localhost, or by sharing
content from one computer to another one. This second method will also show
some latency, and might not work well on poor networks. The network needs to
support multicast UDP (some networks fiter UDP packets out by default).

1) Testing on a single computer:

 - "Launch" the app
 - Check the "localhost" checkbox
 - Select a bitrate (default 2000kbps)
 - Click "Play stream"
    - nothing should happen yet, just a few messages might show up
 - Click on the "Share" button, and select a window to share with the receiver.

At this point, the content of the window shared should appear on the app.


2) Testing with different computers, sharing over the network:

 - "Launch" the app on both computers
 - On the computer that is going to share (sender):
    - Type the IP of the receiver on the text field
    - Do NOT check localhost
    - Select the bitrate (around 1000kbps should already work)
    - Click on the "Share" button, and select the window to share.
 - On the receiver:
    - Click on the "Play stream" button. Content should start playing.

Notice that the firewall might block the doors used by this app, so you might
need to allow the receiver to listen to UDP on the port 5004. The sender uses a
random port to listen to feedback from the receiver, so the firewall must be
disabled (this will be fixed later). On ChromeOS 47 and later, this process is
automatic (the required doors are open on the firewall) and nothing else needs
to be done.

Known Issues
------------

This is a work in progress, and many things are still not finished, including
the interface and the API.

Check the TODO file for more information about things to be done.

API usage
---------

The NaCl module can be loaded using the `nms.loadModule` API, usually
on an event listener to `DOMContentLoaded`:

```javascript
document.addEventListener('DOMContentLoaded', function() {
  var loadpromise = nms.loadModule(window, document.body, customlog);

  loadpromise.then(function() {
    moduleDidLoad();
  }).catch(function(lastError) {
    customlog('Error when loading module: ' + lastError);
  });
});
```

`nms.loadModule` returns a promise, that is fulfilled when the module
finishes loading. Notice that references to the window, the document
body, and a function used for logging must be passed as parameters.

Since the API is all async, all the functions will return es6-promises.
For instance, to create a `sharer`:

```javascript
var options = {
  ip: '239.255.42.99',
  bitrate: 200, // in kbps
  fps: 30
};

nms.createSharer(options).then(function(sharer) {
  console.log("Created sharer: " + sharer.sharer_id);
}).catch(function(lastError) {
  console.log("Could not create sharer: " + lastError);
});
```

Once the sharer is created, it's possible to start sharing a
MediaStream, for instance from the getUserMedia API:

```javascript
navigator.getUserMedia = navigator.getUserMedia ||
  navigator.webkitGetUserMedia || navigator.mozGetUserMedia;


var constraints = {
  video: {
    mandatory: {
      minWidth: capture_width,
      minHeight: capture_height
    }
  }
};

navigator.getUserMedia(constraints, function(captureStream) {
  sharer.shareTracks(captureStream).then(function() {
    console.log("Successfully started streaming.");
    return sharer.onStopped(); // Returns a promise that resolves when the
                               // stream is stopped
  }).then(function() { // chain promise returned from sharer.onStopped()
    console.log("Stopped the stream.");
  });
}, function(error) {
  console.log(error.name + ": " + error.message);
});
```

On the code above, the promise returned from `sharer.sharerTracks` will
resolve when the stream is all set and starts being shared. The
sharer.onStopped() method can be called, and returns a promise that will
resolve when the stream is stopped, either by someone calling
`sharer.stop()` or by video track on the stream being manually stopped.
It is recommended that `sharer.stop()` be used to stop it. It will
return a promise that only resolves when both the sharer and the stream
totally stop, at which point it's safe to request to share a different
stream.

The APIs for the player are quite simple: `nms.startPlayer()` and
`nms.stopPlayer()`. Both return promises that will resolve when the
player is finally stopped or started. They still need work, for instance
to allow specifying the IP and port which the content come from.
