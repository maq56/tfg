all: border-router-udp-server
CONTIKI=../../..

TARGET=zoul
BOARD=orion

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

ifdef NUMBER_OF_MOTES
CFLAGS+=-DNUMBER_OF_MOTES=$(NUMBER_OF_MOTES)
endif

CONTIKI_WITH_IPV6 = 1

WITH_IP64 = 1

APPS += json
MODULES += core/net/http-socket

include $(CONTIKI)/Makefile.include
