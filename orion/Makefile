all: border-router-udp-server
CONTIKI=../../..

TARGET=zoul
BOARD=orion

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

ifdef NUMBER_OF_MOTES
CFLAGS+=-DNUMBER_OF_MOTES=$(NUMBER_OF_MOTES)
endif

ifdef BATT_THLD
CFLAGS+=-DMOTE_LOW_BATTERY_LIMIT=$(BATT_THLD)
endif

ifdef PDR_THLD
CFLAGS+=-DMOTE_LOW_PDR_LIMIT=$(PDR_THLD)
endif

ifdef TEMP_THLD
CFLAGS+=-DMOTE_HIGH_TEMP_LIMIT=$(TEMP_THLD)
endif

CONTIKI_WITH_IPV6 = 1

WITH_IP64 = 1

APPS += json
MODULES += core/net/http-socket

include $(CONTIKI)/Makefile.include
