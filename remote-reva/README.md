UDP-RPL Client
==============

This app works only for Zolertia RE-Mote Rev A.


Usage
=====

Build app
---------
$ make


Build and upload app to device
------------------------------
$ make udp-client.upload PORT={your_port_here}

Some optional parameters can be used:
+ PERIOD:       It specifies the frequency with which packets are sent (60
                seconds by default).

+ DEVICE_ID:    It specifies the Device ID (which is 1 by default).

+ MAX_SEQ_ID:   It specifies the maximum sequence ID (which is 60 by default).

example:
$ make udp-client.upload PORT=/dev/ttyUSB0 PERIOD=30 DEVICE_ID=7 MAX_SEQ_ID=50



Show the serial output
----------------------
make PORT={your_port_here} login



Clean binaries
--------------
make clean
