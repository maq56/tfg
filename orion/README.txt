Border Router and UDP-RPL Server
================================

This app works only for Zolertia Orion Border Router.


Usage
=====

Build app
---------
$ make


Build and upload app to device
------------------------------
$ make border-router-udp-server.upload PORT={your_port_here}

Some additional parameters can be used:
+ NUMBER_OF_MOTES:  It specifies the number of motes that the app can manage
                    (1 by default).

example:
$ make border-router-udp-server.upload PORT=/dev/ttyUSB0 NUMBER_OF_MOTES=5


Show the serial output
----------------------
make PORT={your_port_here} login


Clean binaries
--------------
make clean
