This programs aims to server as example for the gstreamer 
conference talk here:

https://gstreamer.freedesktop.org/conference/2016/talks-and-speakers.html#dynamic-pipelines

Changing: BUGGY_CODE, FORCE_RACE_CONDITIONS and RECORD_H264 defines will 
change the behavior:

 * BUGGY_CODE: program will run bad code that doesn't work in all situations to
 show the difference between correct way of managing dynamic pipelines
 and incorrect way.
 * FORCE_RACE_CONDITONS: change this in order to enable sleeps to force
 race conditions when running buggy code. When runnin normal code it
 only delays the changes, but no errors may happen.
 * RECORD_H264: Changes the recording pipeline to use h264 instead of
 vp8 encoder