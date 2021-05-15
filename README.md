Integration of Wireless Sensor Networks (WSN) with Sentilo and Telegram through a Border Router
===============================================================================================

This project is a Contiki APP that manages sensors reading and sends the data
to the cloud, specifically to Sentilo and Telegram. This is done by using
Zolertia RE-Motes (Rev-A) and one Zolertia Orion Ethernet Router as border
router.

Tested with Contiki 3.0

Usage
=====
Before building and uploading the application to the devices, it is required to:

1. Download and copy this project into {CONTIKI_DIRECTORY}/examples

2. Replace the Contiki module 'http-socket' by the modified one present in
   this project ({CONTIKI_DIRECTORY}/core/net/http-socket). It will not change
   any previous functionality, it just implements some new features, so it
   should be compatible with other applications that use the original one.

3. Follow the instrucctions in README.md of 'orion' and 'remote-reva' for
   building and uploading the code to the devices.
