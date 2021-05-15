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

Some optional parameters can be used:
+ NUMBER_OF_MOTES:  It specifies the number of motes that the app can manage
                    (1 by default).

+ TEMP_THLD:        It specifies a threshold for warning for high temperature
                    values in Celsius degrees (40 degrees by default).

+ BATT_THLD:        It specifies a threshold for warning for low battery
                    values in mV (3180 mV by default).

+ PDR_THLD:         It specifies a threshold for warning for low PDR
                    (80% or less by default).

example:
$ make border-router-udp-server.upload PORT=/dev/ttyUSB0 NUMBER_OF_MOTES=5 BATT_THLD=3000 TEMP_THLD=30 PDR_THLD=90


Show the serial output
----------------------
make PORT={your_port_here} login


Clean binaries
--------------
make clean
