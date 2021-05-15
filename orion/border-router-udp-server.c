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
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ip/uip.h"
#include "net/rpl/rpl.h"

#include "net/netstack.h"
#include "dev/button-sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "http-socket.h"
#include "jsonparse.h"
#include "ip64.h"

#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

#define UIP_IP_BUF ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

// define client and server ports.
#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

// define the period for sending http requests.
#define HTTP_REQUEST_TIME 1 * CLOCK_SECOND

// 5 seconds of http requests timeout.
#define HTTP_REQUESTS_TIMEOUT_TIME 5 * CLOCK_SECOND

// define number of motes if was not defined previosly.
#ifndef NUMBER_OF_MOTES
#define NUMBER_OF_MOTES 1
#endif

// when less than 3180 mV were registered.
#ifndef MOTE_LOW_BATTERY_LIMIT
#define MOTE_LOW_BATTERY_LIMIT 3180
#endif

// when, after a cycle, less than 80% of packets were received.
#ifndef MOTE_LOW_PDR_LIMIT
#define MOTE_LOW_PDR_LIMIT 80
#endif

// when more than 40 Celsius degrees were registered.
#ifndef MOTE_HIGH_TEMP_LIMIT
#define MOTE_HIGH_TEMP_LIMIT 40
#endif

// max 6 request for each mote, 5 for sentilo and 1 for telegram.
#define MAX_HTTP_REQUESTS 6*NUMBER_OF_MOTES

// define max data in and out.
#define MAX_HTTP_DATA_IN 512
#define MAX_HTTP_DATA_OUT 256

// {"chat_id":"-XXXXXXXXXXXXX","text":""} + \0
#define MIN_TELEGRAM_MSG_SIZE 39

// maximum chars for storing string data for each device. It depend on the max
// http output data.
#define MAX_DEVICE_STRING_DATA MAX_HTTP_DATA_OUT

// var to control if a request is still in process.
static int sending_http_request = 0;

// vars to control http responses.
static int http_bytes_received = 0;
static char http_data_received[MAX_HTTP_DATA_IN];

// the UDP connection.
static struct uip_udp_conn* server_conn;
// a json parser for parsing the content of the packets.
static struct jsonparse_state js_p_state;

// timers to manage times when sending http requests.
static struct etimer http_requests_timer;
static struct etimer http_requests_timeout_timer;

// a socket for sending http request.
static struct http_socket socket;

typedef enum {SENTILO, TELEGRAM} TARGET_TYPE;
typedef enum {TEMP, HUM, LIGHT, BATT, PDR, OTHER} DATA_TYPE;

// struct for storing the content of an http request.
struct http_request
{
    struct http_request* next;
    TARGET_TYPE target_type;
    int target_id;
    DATA_TYPE data_type;
    char data[6];
    // pointer to a char array that can contain extra data.
    char* large_data;
};

// struct for storing device info/data.
struct device_info {
    int device_id;
    char f_update_sensors_data_on_telegram;
    char f_low_pdr;
    int packets_received;
    int packets_sent;
    char data[MAX_DEVICE_STRING_DATA];
};

// declare a list of device info, one for each mote.
struct device_info device_info_list[NUMBER_OF_MOTES];

// declare a list of http requests.
LIST(http_request_list);
MEMB(http_request_mem, struct http_request, MAX_HTTP_REQUESTS);

PROCESS(border_router_and_udp_server_process, "Border Router and UDP server process");
AUTOSTART_PROCESSES(&border_router_and_udp_server_process);

// function that returns the info struct of a specific device.
static struct device_info* get_device_info(int target_device_id)
{
    struct device_info* info = NULL;

    for (int i = 0; i < NUMBER_OF_MOTES; i++)
    {
        if (device_info_list[i].device_id == target_device_id)
        {
            info = &device_info_list[i];

            break;
        }
    }

    return info;
}

static void get_data_type_as_string(DATA_TYPE dt, char* out)
{
    switch (dt)
    {
        case TEMP:
            sprintf(out, "temp");
            break;

        case HUM:
            sprintf(out, "hum");
            break;

        case LIGHT:
            sprintf(out, "light");
            break;

        case BATT:
            sprintf(out, "batt");
            break;

        case PDR:
            sprintf(out, "pdr");
            break;

        case OTHER:
            // do nothing.
            break;

        default:
            // do nothing.
            break;
    }
}

// callback for parsing http responses.
static void http_callback(struct http_socket *s, void *ptr,
    http_socket_event_t e, const uint8_t *data, uint16_t datalen)
{
    if (e == HTTP_SOCKET_ERR)
    {
        PRINTF("HTTP socket error\n");
        http_bytes_received = 0;
        http_data_received[0] = 0;
        http_socket_close(s);
        sending_http_request = 0;
    }
    else if (e == HTTP_SOCKET_TIMEDOUT)
    {
        PRINTF("HTTP socket error: timed out\n");
        http_bytes_received = 0;
        http_data_received[0] = 0;
        http_socket_close(s);
        sending_http_request = 0;
    }
    else if (e == HTTP_SOCKET_ABORTED)
    {
        PRINTF("HTTP socket error: aborted\n");
        http_bytes_received = 0;
        http_data_received[0] = 0;
        http_socket_close(s);
        sending_http_request = 0;
    }
    else if (e == HTTP_SOCKET_HOSTNAME_NOT_FOUND)
    {
        PRINTF("HTTP socket error: hostname not found\n");
        http_bytes_received = 0;
        http_data_received[0] = 0;
        http_socket_close(s);
        sending_http_request = 0;
    }
    else if (e == HTTP_SOCKET_CLOSED)
    {
        if (http_bytes_received > 0)
        {
            if (http_bytes_received > MAX_HTTP_DATA_IN - 1)
            {
                PRINTF("(Received data overflows the maximum!)\n");
            }

            PRINTF("HTTP socket received data:\n%s\n", http_data_received);
        }
        else
        {
            PRINTF("No bytes received.\n");
        }

        http_bytes_received = 0;
        http_data_received[0] = 0;
        http_socket_close(s);
        sending_http_request = 0;
    }
    else if (e == HTTP_SOCKET_DATA)
    {
        // if there are enough space (-1 because \0 char)...
        if (http_bytes_received < MAX_HTTP_DATA_IN - 1)
        {
            // if income data is less than the rest of the space...
            if (datalen < MAX_HTTP_DATA_IN - http_bytes_received - 1)
            {
                // just copy it.
                strncat(http_data_received, (const char *)data,
                    datalen);
            }
            else
            {
                // if more than the rest of the space, copy the maximum.
                strncat(http_data_received, (const char *)data,
                    MAX_HTTP_DATA_IN - http_bytes_received - 1);
            }
        }
        else
        {
            // not enough space, do not copy data.
        }

        http_bytes_received += datalen;

        printf("HTTP socket received %d bytes of data\n", datalen);
    }
    else
    {
        PRINTF("UNKNOWN event\n");
    }
}

static void send_http_requests()
{
    // if is not sending any request...
    if (!sending_http_request)
    {
        // get a request from the waiting list.
        struct http_request* r = list_chop(http_request_list);

        // if there is a request to send...
        if (r != NULL)
        {
            // check the target type.
            if (r->target_type == SENTILO)
            {
                // prepare the request.
                sending_http_request = 1;
                PRINTF("Preparing to send request to Sentilo...\n");

                char header[HTTP_SOCKET_CUSTOM_HEADER_LEN];
                char url[HTTP_SOCKET_URLLEN];

                snprintf(header, HTTP_SOCKET_CUSTOM_HEADER_LEN - 1,
                    "IDENTITY_KEY: %s", SENTILO_TOKEN);

                char data_type_string[8];
                get_data_type_as_string(r->data_type, data_type_string);

                snprintf(url, HTTP_SOCKET_URLLEN - 1,
                    "%s/mote_%d_%s/%s",
                    SENTILO_URL,
                    r->target_id,
                    data_type_string,
                    r->data);

                // do not need request info anymore.
                memb_free(&http_request_mem, r);

                // init the socket.
                http_socket_init(&socket);
                // set the identity key header.
                http_socket_set_custom_header(&socket, header);
                // do the request.
                http_socket_put(&socket, url, NULL, 0, "application/json",
                    http_callback, NULL);

                // set the timeout timer.
                etimer_set(&http_requests_timeout_timer, HTTP_REQUESTS_TIMEOUT_TIME);
            }
            else if (r->target_type == TELEGRAM)
            {
                sending_http_request = 1;

                PRINTF("Preparing to send request to Telegram API...\n");

                char url[HTTP_SOCKET_URLLEN];

                snprintf(url, HTTP_SOCKET_URLLEN - 1,
                    "%s/bot%s/sendMessage", TELEGRAM_API_URL, TELEGRAM_BOT_TOKEN);

                // init the socket.
                http_socket_init(&socket);
                // do the request.
                http_socket_post(&socket, url, r->large_data,
                    strlen(r->large_data), "application/json", http_callback,
                    NULL);

                // do not need request info anymore.
                memb_free(&http_request_mem, r);

                // set the timeout timer.
                etimer_set(&http_requests_timeout_timer, HTTP_REQUESTS_TIMEOUT_TIME);
            }
            else
            {
                // unknown target type, nothing to do.
            }
        }
        else
        {
            // there are no request in the list, nothing to do.
            //PRINTF("No HTTP requests remaining.\n");
        }
    }
    else
    {
        PRINTF("Still sending previous HTTP request.\n");

        if (etimer_expired(&http_requests_timeout_timer))
        {
            PRINTF("Previous HTTP request timeout.\n");
            http_socket_close(&socket);
            sending_http_request = 0;
            http_bytes_received = 0;
            http_data_received[0] = 0;
        }
    }
}

static void tcpip_handler(void)
{
    if (uip_newdata())
    {
        ((char *)uip_appdata)[uip_datalen()] = 0;
        //PRINTF("DATA recv '%s' from ", (char *)uip_appdata);
        PRINTF("Server received data from %d\n",
            UIP_IP_BUF->srcipaddr.u8[sizeof(UIP_IP_BUF->srcipaddr.u8) - 1]);

        jsonparse_setup(&js_p_state, (char *)uip_appdata, strlen((char *)uip_appdata));

        int json_type;

        int device_id = -1;
        int device_id_received = 0;
        int seq_id = 0;
        int seq_id_received = 0;
        int temp = 0;
        int temp_received = 0;
        int hum = 0;
        int hum_received = 0;
        int batt = 0;
        int batt_received = 0;
        int light = 0;
        int light_received = 0;
        int pdr = 0;

        // some flags that will help later deciding which messages are needed to
        // be sent.
        char f_mote_test = 0;
        char f_sensor_error = 0;
        char f_high_temperature = 0;
        char f_low_battery = 0;

        // parse the json and store its content.
        while ((json_type = jsonparse_next(&js_p_state)) != 0)
        {
            if (json_type == JSON_TYPE_PAIR_NAME)
            {
                if (jsonparse_strcmp_value(&js_p_state, "id") == 0)
                {
                    // get the device id.
                    json_type = jsonparse_next(&js_p_state);
                    device_id = jsonparse_get_value_as_int(&js_p_state);

                    device_id_received = 1;

                    PRINTF("id: %d\n", device_id);
                }
                else if (jsonparse_strcmp_value(&js_p_state, "typ") == 0)
                {
                    // get the message type.
                    json_type = jsonparse_next(&js_p_state);

                    if (jsonparse_strcmp_value(&js_p_state, "test") == 0)
                    {
                        PRINTF("type: test\n");

                        f_mote_test = 1;
                    }
                    else if (jsonparse_strcmp_value(&js_p_state, "data") == 0)
                    {
                        PRINTF("type: data\n");

                        f_mote_test = 0;
                    }
                    else
                    {
                        // unknown msg type, do nothing.
                    }

                }
                else if (jsonparse_strcmp_value(&js_p_state, "seq") == 0)
                {
                    // get the sequence id.
                    json_type = jsonparse_next(&js_p_state);
                    seq_id = jsonparse_get_value_as_int(&js_p_state);

                    seq_id_received = 1;

                    PRINTF("seq: %d\n", seq_id);
                }
                else if (jsonparse_strcmp_value(&js_p_state, "temp") == 0)
                {
                    // get the temperature value.
                    json_type = jsonparse_next(&js_p_state);

                    if (jsonparse_strcmp_value(&js_p_state, "error") == 0)
                    {
                        // mote indicates something went wrong while reading
                        // the sensor.
                        f_sensor_error = 1;
                        temp_received = 0;

                        PRINTF("temp: error");
                    }
                    else
                    {
                        temp = jsonparse_get_value_as_int(&js_p_state);
                        temp_received = 1;

                        PRINTF("temp: %02d.%d\n", temp / 10, temp % 10);

                        // if temperature is greater or equal than the limit
                        // register alert.
                        if ((temp/10) >= MOTE_HIGH_TEMP_LIMIT)
                        {
                            f_high_temperature = 1;
                        }
                    }
                }
                else if (jsonparse_strcmp_value(&js_p_state, "hum") == 0)
                {
                    // get the humidity value.
                    json_type = jsonparse_next(&js_p_state);

                    if (jsonparse_strcmp_value(&js_p_state, "error") == 0)
                    {
                        // mote indicates something went wrong while reading
                        // the sensor.
                        f_sensor_error = 1;
                        hum_received = 0;

                        PRINTF("temp: error");
                    }
                    else
                    {
                        hum = jsonparse_get_value_as_int(&js_p_state);
                        hum_received = 1;

                        PRINTF("hum: %02d.%d\n", hum / 10, hum % 10);
                    }
                }
                else if (jsonparse_strcmp_value(&js_p_state, "batt") == 0)
                {
                    // get the battery value.
                    json_type = jsonparse_next(&js_p_state);
                    batt = jsonparse_get_value_as_int(&js_p_state);

                    batt_received = 1;

                    PRINTF("batt: %d\n", batt);

                    // if battery is less or equal than the limit register
                    // alert.
                    if (batt <= MOTE_LOW_BATTERY_LIMIT)
                    {
                        f_low_battery = 1;
                    }
                }
                else if (jsonparse_strcmp_value(&js_p_state, "light") == 0)
                {
                    // get the light value.
                    json_type = jsonparse_next(&js_p_state);
                    light = jsonparse_get_value_as_int(&js_p_state);

                    light_received = 1;

                    PRINTF("light: %d\n", light);
                }
                else
                {
                    PRINTF("Unknown JSON parameter received.\n");
                }
            }
        }

        // after parsing the json, if a device id was received...
        if (device_id_received)
        {
            // get the info about this device.
            struct device_info* current_device_info =
                get_device_info(device_id);

            // if exists info for this device...
            if (current_device_info != NULL)
            {
                // if it was a test msg...
                if (f_mote_test)
                {
                    // continue the communication test through a request to
                    // telegram.
                    char msg[MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE] = "\0";

                    struct http_request* r = NULL;
                    r = (struct http_request*) memb_alloc(&http_request_mem);

                    if (r != NULL)
                    {
                        r->target_type = TELEGRAM;

                        snprintf(msg, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE -1,
                            "Mote %d communication test",
                            device_id);

                        snprintf(current_device_info->data, MAX_DEVICE_STRING_DATA -1,
                            "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
                            TELEGRAM_PRIVATE_CHAT_ID,
                            msg);

                        r->data_type = OTHER;
                        r->large_data = current_device_info->data;

                        list_push(http_request_list, r);
                    }
                }
                else
                {
                    // if received a sequence id...
                    if (seq_id_received)
                    {
                        // if device did not reset the sequence, update pdr counters.
                        if (seq_id > current_device_info->packets_sent)
                        {
                            current_device_info->packets_received++;
                            current_device_info->packets_sent = seq_id;
                        }
                        else
                        {
                            // else send info to sentilo.
                            struct http_request* r = NULL;
                            r = (struct http_request*) memb_alloc(&http_request_mem);

                            pdr = (100*current_device_info->packets_received)/current_device_info->packets_sent;

                            if (r != NULL)
                            {
                                r->target_type = SENTILO;
                                r->target_id = device_id;
                                r->data_type = PDR;
                                sprintf(r->data, "%d", pdr);

                                list_push(http_request_list, r);
                            }

                            // and then reset stats and update pdr counter again.
                            current_device_info->packets_received = 1;
                            current_device_info->packets_sent = seq_id;

                            // also it is time to send sensors data to telegram,
                            // activate flag and do it later.
                            current_device_info->f_update_sensors_data_on_telegram = 1;

                            // if pdr is low, register alert.
                            if (pdr <= MOTE_LOW_PDR_LIMIT)
                            {
                                current_device_info->f_low_pdr = 1;
                            }
                        }
                    }

                    // if received temperature...
                    if (temp_received)
                    {
                        // add a request to update sentilo info.
                        struct http_request* r = NULL;
                        r = (struct http_request*) memb_alloc(&http_request_mem);

                        if (r != NULL)
                        {
                            r->target_type = SENTILO;
                            r->target_id = device_id;
                            r->data_type = TEMP;
                            sprintf(r->data, "%02d.%d", temp / 10, temp % 10);

                            list_push(http_request_list, r);
                        }
                    }

                    if (hum_received)
                    {
                        // add a request to update sentilo info.
                        struct http_request* r = NULL;
                        r = (struct http_request*) memb_alloc(&http_request_mem);

                        if (r != NULL)
                        {
                            r->target_type = SENTILO;
                            r->target_id = device_id;
                            r->data_type = HUM;
                            sprintf(r->data, "%02d.%d", hum / 10, hum % 10);

                            list_push(http_request_list, r);
                        }
                    }

                    if (batt_received)
                    {
                        // add a request to update sentilo info.
                        struct http_request* r = NULL;
                        r = (struct http_request*) memb_alloc(&http_request_mem);

                        if (r != NULL)
                        {
                            r->target_type = SENTILO;
                            r->target_id = device_id;
                            r->data_type = BATT;
                            sprintf(r->data, "%d.%d", batt / 1000, (batt/10) % 100);

                            list_push(http_request_list, r);
                        }
                    }

                    if (light_received)
                    {
                        // add a request to update sentilo info.
                        struct http_request* r = NULL;
                        r = (struct http_request*) memb_alloc(&http_request_mem);

                        if (r != NULL)
                        {
                            r->target_type = SENTILO;
                            r->target_id = device_id;
                            r->data_type = LIGHT;
                            sprintf(r->data, "%d", light);

                            list_push(http_request_list, r);
                        }
                    }

                    // finished creating sentilo requests.

                    // preparing telegram request (if needed).

                    char msg[MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE] = "\0";
                    char tmp[MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE] = "\0";

                    if (f_sensor_error || f_high_temperature || f_low_battery ||
                        current_device_info->f_low_pdr)
                    {
                        // if some of these alerts were registered, then build
                        // and send an alert through telegram.
                        struct http_request* r = NULL;
                        r = (struct http_request*) memb_alloc(&http_request_mem);

                        if (r != NULL)
                        {
                            r->target_type = TELEGRAM;

                            snprintf(msg, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE -1,
                                "Mote %d:\n",
                                device_id);

                            if (f_high_temperature)
                            {
                                snprintf(tmp, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE -1,
                                    "- High temperature: %02d.%d °C\n",
                                    temp / 10,
                                    temp % 10);

                                strncat(msg, tmp, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE - strlen(msg) -1);
                            }

                            if (f_low_battery)
                            {
                                snprintf(tmp, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE -1,
                                    "- Low battery: %d.%d V\n", batt / 1000, (batt/10) % 100);

                                strncat(msg, tmp, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE - strlen(msg) -1);
                            }

                            if (current_device_info->f_low_pdr)
                            {
                                snprintf(tmp, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE -1,
                                    "- Low PDR: %d%%\n", pdr);

                                strncat(msg, tmp, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE - strlen(msg) -1);

                                // reset flag.
                                current_device_info->f_low_pdr = 0;
                            }

                            if (f_sensor_error)
                            {
                                snprintf(tmp, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE -1,
                                    "- Sensor error");

                                strncat(msg, tmp, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE - strlen(msg) -1);
                            }

                            snprintf(current_device_info->data, MAX_DEVICE_STRING_DATA -1,
                                "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
                                TELEGRAM_PRIVATE_CHAT_ID,
                                msg);

                            r->data_type = OTHER;
                            r->large_data = current_device_info->data;

                            // add telegram request.
                            list_push(http_request_list, r);
                        }
                    }
                    else
                    {
                        // if everything went ok and it is required to update data:
                        if (current_device_info->f_update_sensors_data_on_telegram)
                        {
                            // add telegram request.
                            struct http_request* r = NULL;
                            r = (struct http_request*) memb_alloc(&http_request_mem);

                            if (r != NULL)
                            {
                                r->target_type = TELEGRAM;

                                snprintf(msg, MAX_DEVICE_STRING_DATA - MIN_TELEGRAM_MSG_SIZE -1,
                                    "Mote %d:\n- Temperature: %02d.%d °C\n- Humidity: %02d.%d%%\n- Light: %d%%",
                                    device_id,
                                    temp / 10,
                                    temp % 10,
                                    hum / 10,
                                    hum % 10,
                                    light);

                                snprintf(current_device_info->data, MAX_DEVICE_STRING_DATA -1,
                                    "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
                                    TELEGRAM_PUBLIC_CHAT_ID,
                                    msg);

                                r->data_type = OTHER;
                                r->large_data = current_device_info->data;

                                list_push(http_request_list, r);
                            }

                            // reset flag.
                            current_device_info->f_update_sensors_data_on_telegram = 0;
                        }
                        else
                        {
                            // nothing to do.
                        }
                    }
                }
            }
            else
            {
                // if device is not in list, it is not possible to work with it.
                PRINTF("Received data from unlisted device '%d'.\n",
                    device_id);

                PRINTF("It may be necessary to set a greater value for 'NUMBER OF MOTES'?.\n");
            }
        }
        else
        {
            PRINTF("Received data from unknown device.\n");
        }

        // restore server connection to allow data from any node.
        memset(&server_conn->ripaddr, 0, sizeof(server_conn->ripaddr));
        server_conn->rport = 0;
    }
}

static void print_local_addresses(void)
{
    int i;
    uint8_t state;

    PRINTF("Server IPv6 addresses: ");
    for (i = 0; i < UIP_DS6_ADDR_NB; i++)
    {
        state = uip_ds6_if.addr_list[i].state;
        if (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)
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

static void print_app_config()
{
    PRINTF("=============================================================\n");
    PRINTF("= APP config                                                =\n");
    PRINTF("=============================================================\n");
    PRINTF("Max number of motes to manage:  %d\n", NUMBER_OF_MOTES);
    PRINTF("PDR Threshold:                  %d%% packets\n", MOTE_LOW_PDR_LIMIT);
    PRINTF("Battery threshold:              %d mV\n", MOTE_LOW_BATTERY_LIMIT);
    PRINTF("Temperature threshold:          %d °C\n", MOTE_HIGH_TEMP_LIMIT);
    PRINTF("Using Sentilo URL:              '%s'\n", SENTILO_URL);
    PRINTF("Using Telegram URL:             '%s'\n", TELEGRAM_API_URL);
    PRINTF("=============================================================\n");
}

PROCESS_THREAD(border_router_and_udp_server_process, ev, data)
{
    uip_ipaddr_t ipaddr;
    struct uip_ds6_addr *root_if;

    PROCESS_BEGIN();

    PROCESS_PAUSE();

    PRINTF("UDP server started. nbr:%d routes:%d\n",
           NBR_TABLE_CONF_MAX_NEIGHBORS, UIP_CONF_MAX_ROUTES);

    // init udp/rpl processes.
#if UIP_CONF_ROUTER
    uip_ip6addr(&ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0x00ff, 0xfe00, 1);

    uip_ds6_addr_add(&ipaddr, 0, ADDR_MANUAL);
    root_if = uip_ds6_addr_lookup(&ipaddr);

    if (root_if != NULL)
    {
        rpl_dag_t *dag;
        dag = rpl_set_root(RPL_DEFAULT_INSTANCE, (uip_ip6addr_t *)&ipaddr);
        uip_ip6addr(&ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
        rpl_set_prefix(dag, &ipaddr, 64);
        PRINTF("Created a new RPL dag\n");
    }
    else
    {
        PRINTF("Failed to create a new RPL DAG\n");
    }
#endif

    print_local_addresses();

    NETSTACK_MAC.off(1);

    server_conn = udp_new(NULL, UIP_HTONS(UDP_CLIENT_PORT), NULL);

    if (server_conn == NULL)
    {
        PRINTF("No UDP connection available, exiting the process!\n");
        PROCESS_EXIT();
    }

    udp_bind(server_conn, UIP_HTONS(UDP_SERVER_PORT));

    PRINTF("Created a server connection with remote address ");
    PRINT6ADDR(&server_conn->ripaddr);
    PRINTF(" local/remote port %u/%u\n", UIP_HTONS(server_conn->lport),
           UIP_HTONS(server_conn->rport));

    // init ip64 module (ethernet).
    ip64_init();

    print_app_config();

    // init some vars.
    http_data_received[0] = 0;

    // init list of pdr (packet delivery ratio).
    for (int i = 0; i < NUMBER_OF_MOTES; i++)
    {
        device_info_list[i].device_id = i+1;
        device_info_list[i].f_update_sensors_data_on_telegram = 0;
        device_info_list[i].packets_received = 0;
        device_info_list[i].packets_sent = 0;
    }

    // init http requests list.
    memb_init(&http_request_mem);
    list_init(http_request_list);

    // init timers.
    etimer_set(&http_requests_timer, HTTP_REQUEST_TIME);
    etimer_set(&http_requests_timeout_timer, HTTP_REQUESTS_TIMEOUT_TIME);

    while (1)
    {
        PROCESS_YIELD();

        // if received a packet from a udp client.
        if (ev == tcpip_event)
        {
            // handle the packet.
            tcpip_handler();
        }

        // if requests timer expired
        if (etimer_expired(&http_requests_timer))
        {
            // execute process for sending requests and reset the timer.
            send_http_requests();
            etimer_reset(&http_requests_timer);
        }
    }

    PROCESS_END();
}
