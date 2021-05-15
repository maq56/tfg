Modified 'http-socket' Contiki module
=====================================

This is a modified 'http-socket' module of Contiki. It does not change any
functionality of the original version, it just implements some new features, so
it should be compatible with applications that use the original one.

Tested with Contiki 3.0

Features
--------

+ Adding PUT and DELETE methods: This module allows to perform PUT and DELETE
    requests, unlike the original module. It can be done similarly as GET
    and POST using the methods "http_socket_put" and "http_socket_delete".

+ Adding custom headers to the request: It allows to add some custom headers by
    using the method "http_socket_set_custom_header".


Installation
============

In order to install this modules you just have to replace the original one which
can be found at {CONTIKI_DIRECTORY}/core/net/http-socket.


License
=======
TODO