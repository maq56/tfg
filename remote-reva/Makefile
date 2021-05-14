all: udp-client
CONTIKI=../../..

TARGET=zoul
BOARD=remote-reva

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

ifdef PERIOD
CFLAGS+=-DPERIOD=$(PERIOD)
endif

ifdef DEVICE_ID
CFLAGS+=-DDEVICE_ID=$(DEVICE_ID)
endif

CONTIKI_WITH_IPV6 = 1

MODULES += platform/zoul/dev2

include $(CONTIKI)/Makefile.include
