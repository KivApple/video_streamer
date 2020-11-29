# video_streamer

MJPEG TCP streamer which can use Web Camera hardware JPEG encoder.

This software requires Linux and v4l2-compatible webcam.

## Dependencies

* CMake
* STL
* pthread
* libjpeg **or** libjpeg-turbo

## Usage

    video_streamer [--width NNN] [--height NNN] 
        [--stats] [--log-config FILE-NAME] 
        [--trace-libjpeg] [--send-buffer NNN]
        --device /dev/video0 
        --listen 127.0.0.1:1234 --listen [::]:1234

Log configuration file uses [EasyLogging++ configuration format](https://github.com/amrayn/easyloggingpp#using-configuration-file).

You can play the stream using [VLC](https://www.videolan.org/) (or any other compatible player). 
Just click "File" -> "Open Network Stream..." and enter a URL like `tcp://127.0.0.1:1234`.

You can also use [ffplay](https://ffmpeg.org/ffplay.html) utility:

    ffplay -fflags nobuffer -framerate 30 tcp://127.0.0.1:1234/

## Library usage

    #include <easylogging++.h>
    #include "v4l2_device.h"
    
    INITIALIZE_EASYLOGGINGPP
    
    int main(int argc, char *argv[]) {
        return video_streamer::main(argc, argv, [](video_streamer::uncompressed_frame frame) {
            // You can do something with frame. The frame is in RGB format.
            return frame;
        });
    }

## TODO

* Support for YUV pixel format (we need to compress it manually using libjpeg)
* Allow to specify target bitrate and recompress JPEG if necessary
* Support for H264 if webcam can encode it (just pass the stream as it)
* Write a simple utility and library to play stream using SDL and OpenGL
