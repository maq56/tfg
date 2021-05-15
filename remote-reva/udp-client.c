/*
 * Copyright (c) 2015, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "contiki.h"
#include "sys/ctimer.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ip/uip-udp-packet.h"
#include "sys/ctimer.h"
#include <stdio.h>
#include <string.h>

#include "dev/serial-line.h"
#include "net/ipv6/uip-ds6-route.h"


#include "dev/adc-sensors.h"
#include "cc2538-sensors.h"
#include "dev/dht22.h"

#include "dev/leds.h"
#include "dev/button-sensor.h"

#define DEBUG DEBUG_FULL
#include "net/ip/uip-debug.h"

// define client and server ports.
#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

// defining a period in seconds.
#ifndef PERIOD
#define PERIOD 60
#endif

// interval of time between each packet send.
#define SEND_INTERVAL (PERIOD * CLOCK_SECOND)

// maximum msg
#define MAX_MSG_LEN 128

// defining the device id if not already defined.
#ifndef DEVICE_ID
#define DEVICE_ID 1
#endif

// maximum number of sequence id.
#ifndef MAX_SEQ_ID
#define MAX_SEQ_ID 60
#endif

// define the interval of light intensity reading.
#define LIGHT_SENSOR_READ_INTERVAL (1 * CLOCK_SECOND)

// maximum attempts for reading temperature and humidity.
#define TEMP_HUM_READ_MAX_ATTEMPTS 20

// define the led that indicates it is sending a test message packet.
#define TEST_MESSAGE_LED LEDS_BLUE

// the udp connection.
static struct uip_udp_conn *client_conn;
// the server address.
static uip_ipaddr_t server_ipaddr;

// var to store the current sequence id.
static int seq_id;

// flag to know if user requested to send a test message.
static char f_send_test_msg;
// var to store the last light value.
static int last_light;

// vars to manage light readings.
static long light_accumulated;
static int light_read_counter;

static void tcpip_handler(void)
{
    char *str;

    if (uip_newdata())
    {
        str = uip_appdata;
        str[uip_datalen()] = '\0';
        printf("DATA recv '%s'\n", str);
    }
}

static void send_packet(void *ptr)
{
    char buf[MAX_MSG_LEN];

    // if it is required to send a test message...
    if (f_send_test_msg)
    {
        // reset flag.
        f_send_test_msg = 0;

        // build it.
        snprintf(buf, MAX_MSG_LEN - 1,
            "{\"id\": %d, \"typ\": \"test\"}",
            DEVICE_ID);
    }
    else
    {
        // else send a data message.
        int temp = 0;
        int hum = 0;

        // activate temp/hum sensor.
        SENSORS_ACTIVATE(dht22);

        // read from the sensor.
        int error = dht22_read_all(&temp, &hum);
        uint8_t read_counter = 1;

        while (error == DHT22_ERROR &&
            read_counter < TEMP_HUM_READ_MAX_ATTEMPTS)
        {
            error = dht22_read_all(&temp, &hum);
            read_counter++;
        }

        // deactivate temp/hum sensor.
        SENSORS_DEACTIVATE(dht22);

        printf("Temp/Hum read attempts: %d.\n", read_counter);

        // if there was no error...
        if (error != DHT22_ERROR)
        {
            // build a data message with temp/hum.
            // -1 because of \0 char.
            snprintf(buf, MAX_MSG_LEN - 1,
                "{\"id\": %d, \"typ\": \"data\", \"seq\": %d, \"temp\": %d, \"hum\": %d, \"light\": %d, \"batt\": %d}",
                DEVICE_ID,
                seq_id,
                temp,
                hum,
                last_light,
                vdd3_sensor.value(CC2538_SENSORS_VALUE_TYPE_CONVERTED));
        }
        else
        {
            // build a data message indicating temp/hum error.
            printf("Failed to read the temp/hum sensor\n");

            // -1 because of \0 char.
            snprintf(buf, MAX_MSG_LEN - 1,
                "{\"id\": %d, \"typ\": \"data\", \"seq\": %d, \"temp\": \"%s\", \"hum\": \"%s\", \"light\": %d, \"batt\": %d}",
                DEVICE_ID,
                seq_id,
                "error",
                "error",
                last_light,
                vdd3_sensor.value(CC2538_SENSORS_VALUE_TYPE_CONVERTED));
        }

        // update sequence id.
        if (seq_id >= MAX_SEQ_ID)
        {
            // if seq_id exceeds the limit, restart it.
            seq_id = 1;
        }
        else
        {
            // otherwise, ++.
            seq_id++;
        }
    }

    printf(" (msg: %s)\n", buf);

    // send the message.
    uip_udp_packet_sendto(client_conn, buf, strlen(buf),
        &server_ipaddr, UIP_HTONS(UDP_SERVER_PORT));
}

static void print_local_addresses(void)
{
    int i;
    uint8_t state;

    PRINTF("Client IPv6 addresses: ");
    for (i = 0; i < UIP_DS6_ADDR_NB; i++)
    {
        state = uip_ds6_if.addr_list[i].state;
        if (uip_ds6_if.addr_list[i].isused &&
            (state == ADDR_TENTATIVE || state == ADDR_PREFERRED))
        {
            PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
            PRINTF("\n");
            /* hack to make address "final" */
            if (state == ADDR_TENTATIVE)
            {
                uip_ds6_if.addr_list[i].state = ADDR_PREFERRED;
            }
        }
    }
}

static void set_global_address(void)
{
    uip_ipaddr_t ipaddr;

    uip_ip6addr(&ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
    uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
    uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

    uip_ip6addr(&server_ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0x00ff, 0xfe00, 1);
}

static void activate_test_message_led()
{
    leds_on(TEST_MESSAGE_LED);
}

static void deactivate_test_message_led()
{
    leds_off(TEST_MESSAGE_LED);
}

static void print_app_config()
{
    PRINTF("=============================================================\n");
    PRINTF("= APP config                                                =\n");
    PRINTF("=============================================================\n");
    PRINTF("Device ID:                   %d\n", DEVICE_ID);
    PRINTF("Packet sending period time:  %d seconds\n", PERIOD);
    PRINTF("Maximum sequence ID:         %d\n", MAX_SEQ_ID);
    PRINTF("=============================================================\n");
}

PROCESS(udp_client_process, "UDP client process");
AUTOSTART_PROCESSES(&udp_client_process);

PROCESS_THREAD(udp_client_process, ev, data)
{
    static struct etimer send_packet_timer;
    static struct ctimer test_msg_led_timer;
    static struct etimer light_timer;

    PROCESS_BEGIN();

    PROCESS_PAUSE();

    // init udp/rpl processes.
    set_global_address();

    PRINTF("UDP client process started nbr:%d routes:%d\n",
           NBR_TABLE_CONF_MAX_NEIGHBORS, UIP_CONF_MAX_ROUTES);

    print_local_addresses();

    client_conn = udp_new(NULL, UIP_HTONS(UDP_SERVER_PORT), NULL);
    if (client_conn == NULL)
    {
        PRINTF("No UDP connection available, exiting the process!\n");
        PROCESS_EXIT();
    }

    udp_bind(client_conn, UIP_HTONS(UDP_CLIENT_PORT));

    PRINTF("Created a connection with the server ");
    PRINT6ADDR(&client_conn->ripaddr);
    PRINTF(" local/remote port %u/%u\n",
           UIP_HTONS(client_conn->lport), UIP_HTONS(client_conn->rport));

    print_app_config();

    // initialize some vars.
    f_send_test_msg = 0;

    // initialize packets sequence id.
    seq_id = 1;

    light_accumulated = 0;
    light_read_counter = 0;

    last_light = 0;

    etimer_set(&send_packet_timer, SEND_INTERVAL);
    etimer_set(&light_timer, LIGHT_SENSOR_READ_INTERVAL);

    // using PA2 (ADC3) for light sensor.
    adc_sensors.configure(ANALOG_GROVE_LIGHT, 2);

    while (1)
    {
        PROCESS_YIELD();
        if (ev == tcpip_event)
        {
            tcpip_handler();
        }
        else if((ev == sensors_event) && (data == &button_sensor))
        {
            // if user button is pressed...
            if(button_sensor.value(BUTTON_SENSOR_VALUE_TYPE_LEVEL) == BUTTON_SENSOR_PRESSED_LEVEL)
            {
                activate_test_message_led();
                printf("User button pressed, performing communication test.\n");
                ctimer_set(&test_msg_led_timer, CLOCK_SECOND * 1,
                    deactivate_test_message_led, NULL);

                // activate the "test message" flag and send a packet.
                f_send_test_msg = 1;
                send_packet(NULL);
            }
        }

        if (etimer_expired(&send_packet_timer))
        {
            etimer_reset(&send_packet_timer);
            send_packet(NULL);
        }

        if (etimer_expired(&light_timer))
        {
            etimer_reset(&light_timer);

            // get the value of light sensor.
            int ldr = adc_sensors.value(ANALOG_GROVE_LIGHT);

            // if there are no errors.
            if (ldr != ADC_WRAPPER_ERROR)
            {
                // accumulate the light value.
                light_read_counter++;
                light_accumulated += ldr;

                // if read 100 times or more...
                if (light_read_counter >= 100)
                {
                    // calculate the mean, store it and reset tmp vars.
                    last_light = 100*(light_accumulated/light_read_counter)/65535;
                    light_read_counter = 0;
                    light_accumulated = 0;
                }
            }
            else
            {
                printf("Error getting light data, enable the DEBUG flag in adc-wrapper.c for info\n");
            }
        }
    }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
