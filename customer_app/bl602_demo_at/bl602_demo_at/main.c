/*
 * Copyright (c) 2020 Bouffalolab.
 *
 * This file is part of
 *     *** Bouffalolab Software Dev Kit ***
 *      (see www.bouffalolab.com).
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *   3. Neither the name of Bouffalo Lab nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <vfs.h>
#include <aos/kernel.h>
#include <aos/yloop.h>
#include <event_device.h>
#include <cli.h>

#include <lwip/tcpip.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/tcp.h>
#include <lwip/err.h>
#include <http_client.h>
#include <netutils/netutils.h>

#include <bl602_glb.h>
#include <bl602_hbn.h>

#include <bl_sys.h>
#include <bl_uart.h>
#include <bl_chip.h>
#include <bl_wifi.h>
#include <hal_wifi.h>
#include <bl_sec.h>
#include <bl_cks.h>
#include <bl_irq.h>
#include <bl_dma.h>
#include <bl_gpio_cli.h>
#include <bl_wdt_cli.h>
#include <hal_uart.h>
#include <hal_sys.h>
#include <hal_gpio.h>
#include <hal_boot2.h>
#include <hal_board.h>
#include <looprt.h>
#include <loopset.h>
#include <sntp.h>
#include <bl_sys_time.h>
#include <bl_sys_ota.h>
#include <bl_romfs.h>
#include <fdt.h>

#include <easyflash.h>
#include <bl60x_fw_api.h>
#include <wifi_mgmr_ext.h>
#include <utils_log.h>
#include <libfdt.h>
#include <blog.h>
#include "at_server.h"
#include "blsync_ble_app.h"

#define TASK_PRIORITY_FW            ( 30 )
#define mainHELLO_TASK_PRIORITY     ( 20 )
#define UART_ID_2 (2)
#define WIFI_AP_PSM_INFO_SSID           SAVE_KEY_WIFI_SSID //"conf_ap_ssid"
#define WIFI_AP_PSM_INFO_PASSWORD       SAVE_KEY_WIFI_PASK //"conf_ap_psk"
#define WIFI_AP_PSM_INFO_PMK            "conf_ap_pmk"
#define WIFI_AP_PSM_INFO_BSSID          "conf_ap_bssid"
#define WIFI_AP_PSM_INFO_CHANNEL        "conf_ap_channel"
#define WIFI_AP_PSM_INFO_IP             "conf_ap_ip"
#define WIFI_AP_PSM_INFO_MASK           "conf_ap_mask"
#define WIFI_AP_PSM_INFO_GW             "conf_ap_gw"
#define WIFI_AP_PSM_INFO_DNS1           "conf_ap_dns1"
#define WIFI_AP_PSM_INFO_DNS2           "conf_ap_dns2"
#define WIFI_AP_PSM_INFO_IP_LEASE_TIME  "conf_ap_ip_lease_time"
#define WIFI_AP_PSM_INFO_GW_MAC         "conf_ap_gw_mac"
#define CLI_CMD_AUTOSTART1              "cmd_auto1"
#define CLI_CMD_AUTOSTART2              "cmd_auto2"

#define CODE_CLI_BLSYNC_START 0x01
#define CODE_CLI_BLSYNC_STOP  0x02

extern void ble_stack_start(void);

static wifi_conf_t conf =
{
    .country_code = "CN",
};
extern uint8_t _heap_start;
extern uint8_t _heap_size; // @suppress("Type cannot be resolved")
extern uint8_t _heap_wifi_start;
extern uint8_t _heap_wifi_size; // @suppress("Type cannot be resolved")
static HeapRegion_t xHeapRegions[] =
{
        { &_heap_start,  (unsigned int) &_heap_size}, //set on runtime
        { &_heap_wifi_start, (unsigned int) &_heap_wifi_size },
        { NULL, 0 }, /* Terminates the array. */
        { NULL, 0 } /* Terminates the array. */
};
static wifi_interface_t wifi_interface;

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName )
{
    puts("Stack Overflow checked\r\n");
    while (1) {
        /*empty here*/
    }
}

void vApplicationMallocFailedHook(void)
{
    printf("Memory Allocate Failed. Current left size is %d bytes\r\n",
        xPortGetFreeHeapSize()
    );
    while (1) {
        /*empty here*/
    }
}

void vApplicationIdleHook(void)
{
    __asm volatile(
            "   wfi     "
    );
    /*empty*/
}

static unsigned char char_to_hex(char asccode)
{
    unsigned char ret;

    if('0'<=asccode && asccode<='9')
        ret=asccode-'0';
    else if('a'<=asccode && asccode<='f')
        ret=asccode-'a'+10;
    else if('A'<=asccode && asccode<='F')
        ret=asccode-'A'+10;
    else
        ret=0;

    return ret;
}

static void _chan_str_to_hex(uint8_t *chan_band, uint16_t *chan_freq, char *chan)
{
    int i, freq_len, base=1;
    uint8_t band;
    uint16_t freq = 0;
    char *p, *q;

    /*should have the following format
     * 2412|0
     * */
    p = strchr(chan, '|') + 1;
    if (NULL == p) {
        return;
    }
    band = char_to_hex(p[0]);
    (*chan_band) = band;

    freq_len = strlen(chan) - strlen(p) - 1;
    q = chan;
    q[freq_len] = '\0';
    for (i=0; i< freq_len; i++) {
       freq = freq + char_to_hex(q[freq_len-1-i]) * base;
       base = base * 10;
    }
    (*chan_freq) = freq;
}

typedef struct _wifi_item {
    char ssid[32];
    uint32_t ssid_len;
    uint8_t bssid[6];
    uint8_t channel;
    uint8_t auth;
    int8_t rssi;
} _wifi_item_t;

struct _wifi_conn {
    char ssid[32];
    char ssid_tail[1];
    char pask[64];
};

struct _wifi_state {
    char ip[16];
    char gw[16];
    char mask[16];
    char ssid[32];
    char ssid_tail[1];
    uint8_t bssid[6];
    uint8_t state;
};

static void scan_item_cb(wifi_mgmr_ap_item_t *env, uint32_t *param1, wifi_mgmr_ap_item_t *item)
{
    _wifi_item_t wifi_item;
    void (*complete)(void *) = (void (*)(void *))param1;

    wifi_item.auth = item->auth;
    wifi_item.rssi = item->rssi;
    wifi_item.channel = item->channel;
    wifi_item.ssid_len = item->ssid_len;
    memcpy(wifi_item.ssid, item->ssid, sizeof(wifi_item.ssid));
    memcpy(wifi_item.bssid, item->bssid, sizeof(wifi_item.bssid));

    if (complete) {
        complete(&wifi_item);
    }
}

static void scan_complete_cb(void *p_arg, void *param)
{
    wifi_mgmr_scan_ap_all(NULL, p_arg, scan_item_cb);
}

static void wifiprov_scan(void *p_arg)
{
    wifi_mgmr_scan(p_arg, scan_complete_cb);
}

static void wifiprov_wifi_state_get(void *p_arg)
{
    int tmp_state;
    wifi_mgmr_sta_connect_ind_stat_info_t info;
    ip4_addr_t ip, gw, mask;
    struct _wifi_state state;
    void (*state_get_cb)(void *) = (void (*)(void *))p_arg;

    memset(&state, 0, sizeof(state));
    memset(&info, 0, sizeof(info));
    wifi_mgmr_state_get(&tmp_state);
    wifi_mgmr_sta_ip_get(&ip.addr, &gw.addr, &mask.addr);
    wifi_mgmr_sta_connect_ind_stat_get(&info);

    state.state = tmp_state;
    strcpy(state.ip, ip4addr_ntoa(&ip));
    strcpy(state.mask, ip4addr_ntoa(&mask));
    strcpy(state.gw, ip4addr_ntoa(&gw));
    memcpy(state.ssid, info.ssid, sizeof(state.ssid));
    memcpy(state.bssid, info.bssid, sizeof(state.bssid));
    state.ssid_tail[0] = 0;

    printf("IP  :%s \r\n", state.ip);
    printf("GW  :%s \r\n", state.gw);
    printf("MASK:%s \r\n", state.mask);

    if (state_get_cb) {
        state_get_cb(&state);
    }
}

static void _connect_wifi()
{
    /*XXX caution for BIG STACK*/
    char bssid[32], chan[10];
    char ssid[33], password[66];
    char val_buf[66];
    char val_len = sizeof(val_buf) - 1;
    uint8_t mac[6];
    uint8_t band = 0;
    uint16_t freq = 0;
    int wifi_mode = 0, wifi_auto = 0;

    at_key_value_get(SAVE_KEY_WIFI_MODE, &wifi_mode);
    if (wifi_mode != 1) {
        return;
    }
    at_key_value_get(SAVE_KEY_WIFI_AUTO, &wifi_auto);
    if (!wifi_auto) {
        return;
    }

    wifi_interface = wifi_mgmr_sta_enable();
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    memset(bssid, 0, sizeof(bssid));
    memset(mac, 0, sizeof(mac));
    memset(chan, 0, sizeof(chan));

    memset(val_buf, 0, sizeof(val_buf));
    ef_get_env_blob((const char *)WIFI_AP_PSM_INFO_SSID, val_buf, val_len, NULL);
    if (val_buf[0]) {
        /*We believe that when ssid is set, wifi_confi is OK*/
        strncpy(ssid, val_buf, sizeof(ssid) - 1);
    } else {
        /*Won't connect, since ssid config is empty*/
        puts("[APP]    Empty Config\r\n");
        puts("[APP]    Try to set the following ENV with psm_set command, then reboot\r\n");
        puts("[APP]    NOTE: " WIFI_AP_PSM_INFO_PMK " MUST be psm_unset when conf is changed\r\n");
        puts("[APP]    env: " WIFI_AP_PSM_INFO_SSID "\r\n");
        puts("[APP]    env: " WIFI_AP_PSM_INFO_PASSWORD "\r\n");
        puts("[APP]    env(optinal): " WIFI_AP_PSM_INFO_PMK "\r\n");
        return;
    }

    memset(val_buf, 0, sizeof(val_buf));
    ef_get_env_blob((const char *)WIFI_AP_PSM_INFO_PASSWORD, val_buf, val_len, NULL);
    if (val_buf[0]) {
        strncpy(password, val_buf, sizeof(password) - 1);
    }

    memset(val_buf, 0, sizeof(val_buf));

    memset(val_buf, 0, sizeof(val_buf));
    ef_get_env_blob((const char *)WIFI_AP_PSM_INFO_CHANNEL, val_buf, val_len, NULL);
    if (val_buf[0]) {
        strncpy(chan, val_buf, sizeof(chan) - 1);
        printf("connect wifi channel = %s\r\n", chan);
        _chan_str_to_hex(&band, &freq, chan);
    }
    memset(val_buf, 0, sizeof(val_buf));
//    ef_get_env_blob((const char *)WIFI_AP_PSM_INFO_BSSID, val_buf, val_len, NULL);
//    if (val_buf[0]) {
//        strncpy(bssid, val_buf, sizeof(bssid) - 1);
//        printf("connect wifi bssid = %s\r\n", bssid);
//        bssid_str_to_mac(mac, bssid, strlen(bssid));
//        printf("mac = %02X:%02X:%02X:%02X:%02X:%02X\r\n",
//                mac[0],
//                mac[1],
//                mac[2],
//                mac[3],
//                mac[4],
//                mac[5]
//        );
//    }
    printf("[APP] [WIFI] [T] %lld\r\n"
           "[APP]    SSID %s\r\n"
           "[APP]    SSID len %d\r\n"
           "[APP]    password %s\r\n"
           "[APP]    password len %d\r\n"
           "[APP]    bssid %s\r\n"
           "[APP]    channel band %d\r\n"
           "[APP]    channel freq %d\r\n",
           aos_now_ms(),
           ssid,
           strlen(ssid),
           password,
           strlen(password),
           bssid,
           band,
           freq
    );
    //wifi_mgmr_sta_connect(wifi_interface, ssid, pmk, NULL);
    wifi_mgmr_sta_connect(wifi_interface, ssid, password, NULL, NULL, band, freq);
}

static void wifi_sta_connect(char *ssid, char *password)
{
    int auto_conn = 0;
    wifi_interface_t wifi_interface;

    wifi_interface = wifi_mgmr_sta_enable();
    wifi_mgmr_sta_connect(wifi_interface, ssid, password, NULL, NULL, 0, 0);

    at_key_value_get(SAVE_KEY_WIFI_AUTO, &auto_conn);
    if (auto_conn) {
        wifi_mgmr_sta_autoconnect_enable();
    } else {
        wifi_mgmr_sta_autoconnect_disable();
    }
}

static uint8_t ble_sync_auto = 0;
static void event_cb_cli(input_event_t *event, void *p_arg)
{
    switch (event->code) {
        case CODE_CLI_BLSYNC_START :
            if (event->value) {
                ble_sync_auto = 1;
            } else {
                ble_sync_auto = 0;
            }
            blsync_ble_start();
            break;
        case CODE_CLI_BLSYNC_STOP :
            blsync_ble_stop();
            printf("blsync ble stop\r\n");
            break;
        default :
            break;
    }
}

static void event_cb_wifi_event(input_event_t *event, void *private_data)
{
    struct _wifi_conn *conn_info;

    switch (event->code) {
        case CODE_WIFI_ON_INIT_DONE:
        {
            printf("[APP] [EVT] INIT DONE %lld\r\n", aos_now_ms());
            wifi_mgmr_start_background(&conf);
        }
        break;
        case CODE_WIFI_ON_MGMR_DONE:
        {
            printf("[APP] [EVT] MGMR DONE %lld\r\n", aos_now_ms());
            _connect_wifi();
        }
        break;
        case CODE_WIFI_ON_SCAN_DONE:
        {
            printf("[APP] [EVT] SCAN Done %lld, SCAN Result: %s\r\n",
                aos_now_ms(),
                WIFI_SCAN_DONE_EVENT_OK == event->value ? "OK" : "Busy now"
            );
            wifi_mgmr_cli_scanlist();
        }
        break;
        case CODE_WIFI_ON_DISCONNECT:
        {
            int state;
            printf("[APP] [EVT] disconnect %lld, Reason: %s\r\n",
                aos_now_ms(),
                wifi_mgmr_status_code_str(event->value)
            );
            wifi_mgmr_state_get(&state);
            if (state == WIFI_STATE_PSK_ERROR) {
                at_server_notify(AT_ASYNC_PSK_ERROR);
            }
            if (state == WIFI_STATE_NO_AP_FOUND) {
                at_server_notify(AT_ASYNC_NO_AP_FOUND);
            }
            at_server_notify(AT_WIFI_DISCONNECT);
        }
        break;
        case CODE_WIFI_ON_CONNECTING:
        {
            printf("[APP] [EVT] Connecting %lld\r\n", aos_now_ms());
        }
        break;
        case CODE_WIFI_CMD_RECONNECT:
        {
            printf("[APP] [EVT] Reconnect %lld\r\n", aos_now_ms());
        }
        break;
        case CODE_WIFI_ON_CONNECTED:
        {
            printf("[APP] [EVT] connected %lld\r\n", aos_now_ms());

            // notify AT server
            at_server_notify(AT_ASYNC_WIFI_CONNECTED);
        }
        break;
        case CODE_WIFI_ON_PRE_GOT_IP:
        {
            printf("[APP] [EVT] connected %lld\r\n", aos_now_ms());
        }
        break;
        case CODE_WIFI_ON_GOT_IP:
        {
            printf("[APP] [EVT] GOT IP %lld\r\n", aos_now_ms());
            printf("[SYS] Memory left is %d Bytes\r\n", xPortGetFreeHeapSize());
            at_server_notify(AT_WIFI_IP_GET);
        }
        break;
        case CODE_WIFI_ON_PROV_BSSID:
        {
            printf("[APP] [EVT] [PROV] [BSSID] %lld: %s\r\n",
                    aos_now_ms(),
                    event->value ? (const char*)event->value : "UNKNOWN"
            );
            if (event->value) {
                vPortFree((void*)event->value);
            }
        }
        break;
        case CODE_WIFI_ON_PROV_CONNECT:
        {
            printf("[APP] [EVT] [PROV] [CONNECT] %lld\r\n", aos_now_ms());
            conn_info = (struct _wifi_conn *)event->value;
            at_key_value_set(WIFI_AP_PSM_INFO_SSID, conn_info->ssid);
            at_key_value_set(WIFI_AP_PSM_INFO_PASSWORD, conn_info->pask);
            at_server_notify_with_ctx(AT_WIFI_PRVO_DUMP, NULL, 0);
            if (ble_sync_auto) {
                wifi_sta_connect(conn_info->ssid, conn_info->pask);
            }
        }
        break;
        case CODE_WIFI_ON_PROV_DISCONNECT:
        {
            printf("[APP] [EVT] [PROV] [DISCONNECT] %lld\r\n", aos_now_ms());
            wifi_mgmr_sta_disconnect();
            vTaskDelay(1000);
            wifi_mgmr_sta_disable(NULL);
        }
        break;
        case CODE_WIFI_ON_PROV_SCAN_START:
        {
            printf("[APP] [EVT] [PROV] [SCAN] %lld\r\n", aos_now_ms());
            wifiprov_scan((void *)event->value);
        }
        break;
        case CODE_WIFI_ON_PROV_STATE_GET:
        {
            printf("[APP] [EVT] [PROV] [STATE] %lld\r\n", aos_now_ms());
            wifiprov_wifi_state_get((void *)event->value);
        }
        break;
        default:
        {
            printf("[APP] [EVT] Unknown code %u, %lld\r\n", event->code, aos_now_ms());
            /*nothing*/
        }
    }
}

static void __attribute__((unused)) cmd_aws(char *buf, int len, int argc, char **argv)
{
void aws_main_entry(void *arg);
    xTaskCreate(aws_main_entry, (char*)"aws_iot", 4096, NULL, 10, NULL);
}

#define MAXBUF          128
#define BUFFER_SIZE     (12*1024)

#define PORT 80

static void cmd_stack_wifi(char *buf, int len, int argc, char **argv)
{
    /*wifi fw stack and thread stuff*/
    static uint8_t stack_wifi_init  = 0;


    if (1 == stack_wifi_init) {
        puts("Wi-Fi Stack Started already!!!\r\n");
        return;
    }
    stack_wifi_init = 1;

    hal_wifi_start_firmware_task();
    /*Trigger to start Wi-Fi*/
    aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);

}

static void cmd_stack_ble(char *buf, int len, int argc, char **argv)
{
    ble_stack_start();
}
static void cmd_free_mem(char *buf, int len, int argc, char **argv)
{
    printf("free memory is %d\r\n", xPortGetFreeHeapSize());
}

//xPortGetFreeHeapSize
const static struct cli_command cmds_user[] STATIC_CLI_CMD_ATTRIBUTE = {
        /*Stack Command*/
        { "stack_wifi", "Wi-Fi Stack", cmd_stack_wifi},
        { "stack_ble", "BLE Stack", cmd_stack_ble},
        { "free", "Remain free memory", cmd_free_mem},
};

static void _cli_init()
{
    /*Put CLI which needs to be init here*/
int codex_debug_cli_init(void);
    codex_debug_cli_init();
    easyflash_cli_init();
    network_netutils_iperf_cli_register();
    sntp_cli_init();
    bl_sys_time_cli_init();
    bl_sys_ota_cli_init();
    blfdt_cli_init();
    wifi_mgmr_cli_init();
    bl_wdt_cli_init();
    bl_gpio_cli_init();
    looprt_test_cli_init();
}

static int get_dts_addr(const char *name, uint32_t *start, uint32_t *off)
{
    uint32_t addr = hal_board_get_factory_addr();
    const void *fdt = (const void *)addr;
    uint32_t offset;

    if (!name || !start || !off) {
        return -1;
    }

    offset = fdt_subnode_offset(fdt, 0, name);
    if (offset <= 0) {
       log_error("%s NULL.\r\n", name);
       return -1;
    }

    *start = (uint32_t)fdt;
    *off = offset;

    return 0;
}

static void __opt_feature_init(void)
{
#ifdef CONF_USER_ENABLE_VFS_ROMFS
    romfs_register();
#endif
}

extern void usr_at_cmd_register(void);

static void aos_loop_proc(void *pvParameters)
{
    int fd_console;
    uint32_t fdt = 0, offset = 0;
    static StackType_t proc_stack_looprt[512];
    static StaticTask_t proc_task_looprt;

    /*Init bloop stuff*/
    looprt_start(proc_stack_looprt, 512, &proc_task_looprt);
    loopset_led_hook_on_looprt();

    easyflash_init();
    vfs_init();
    vfs_device_init();

    /* uart */
#if 1
    if (0 == get_dts_addr("uart", &fdt, &offset)) {
        vfs_uart_init(fdt, offset);
    }
#else
    vfs_uart_init_simple_mode(0, 7, 16, 2 * 1000 * 1000, "/dev/ttyS0");
#endif
    if (0 == get_dts_addr("gpio", &fdt, &offset)) {
        hal_gpio_init_from_dts(fdt, offset);
    }

    __opt_feature_init();
    aos_loop_init();

    fd_console = aos_open("/dev/ttyS0", 0);
    if (fd_console >= 0) {
        printf("Init CLI with event Driven\r\n");
        aos_cli_init(0);
        aos_poll_read_fd(fd_console, aos_cli_event_cb_read_get(), (void*)0x12345678);
        _cli_init();
    }

    aos_register_event_filter(EV_WIFI, event_cb_wifi_event, NULL);
    aos_register_event_filter(EV_CLI, event_cb_cli, NULL);

    at_server_init();
    usr_at_cmd_register();
    cmd_stack_wifi(NULL, 0, 0, NULL);
    cmd_stack_ble(NULL, 0, 0, NULL);
    aos_loop_run();

    puts("------------------------------------------\r\n");
    puts("+++++++++Critical Exit From Loop++++++++++\r\n");
    puts("******************************************\r\n");
    vTaskDelete(NULL);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize)
{
    /* If the buffers to be provided to the Idle task are declared inside this
    function then they must be declared static - otherwise they will be allocated on
    the stack and so not exists after this function exits. */
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
    state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task's stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/* configSUPPORT_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
application must provide an implementation of vApplicationGetTimerTaskMemory()
to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize)
{
    /* If the buffers to be provided to the Timer task are declared inside this
    function then they must be declared static - otherwise they will be allocated on
    the stack and so not exists after this function exits. */
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

    /* Pass out a pointer to the StaticTask_t structure in which the Timer
    task's state will be stored. */
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

    /* Pass out the array that will be used as the Timer task's stack. */
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;

    /* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configTIMER_TASK_STACK_DEPTH is specified in words, not bytes. */
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

void user_vAssertCalled(void) __attribute__ ((weak, alias ("vAssertCalled")));
void vAssertCalled(void)
{
    volatile uint32_t ulSetTo1ToExitFunction = 0;

    taskDISABLE_INTERRUPTS();
    while( ulSetTo1ToExitFunction != 1 ) {
        __asm volatile( "NOP" );
    }
}

static void _dump_boot_info(void)
{
    char chip_feature[40];
    const char *banner;

    puts("Booting BL602 Chip...\r\n");

    /*Display Banner*/
    if (0 == bl_chip_banner(&banner)) {
        puts(banner);
    }
    puts("\r\n");
    /*Chip Feature list*/
    puts("\r\n");
    puts("------------------------------------------------------------\r\n");
    puts("RISC-V Core Feature:");
    bl_chip_info(chip_feature);
    puts(chip_feature);
    puts("\r\n");

    puts("Build Version: ");
    puts(BL_SDK_VER); // @suppress("Symbol is not resolved")
    puts("\r\n");

    puts("Build Version: ");
    puts(BL_SDK_VER); // @suppress("Symbol is not resolved")
    puts("\r\n");

    puts("PHY   Version: ");
    puts(BL_SDK_PHY_VER); // @suppress("Symbol is not resolved")
    puts("\r\n");

    puts("RF    Version: ");
    puts(BL_SDK_RF_VER); // @suppress("Symbol is not resolved")
    puts("\r\n");

    puts("Build Date: ");
    puts(__DATE__);
    puts("\r\n");
    puts("Build Time: ");
    puts(__TIME__);
    puts("\r\n");
    puts("------------------------------------------------------------\r\n");

}

static void system_init(void)
{
    blog_init();
    bl_irq_init();
    bl_sec_init();
    bl_sec_test();
    bl_dma_init();
    hal_boot2_init();

    /* board config is set after system is init*/
    hal_board_cfg(0);
}

static void system_thread_init()
{
    /*nothing here*/
}

void bfl_main()
{
    static StackType_t aos_loop_proc_stack[1024];
    static StaticTask_t aos_loop_proc_task;

    bl_sys_early_init();

    /*Init UART In the first place*/
    bl_uart_init(0, 16, 7, 255, 255, 2 * 1000 * 1000);
    puts("Starting bl602 now....\r\n");
    bl_sys_init();
    _dump_boot_info();

    vPortDefineHeapRegions(xHeapRegions);
    printf("Heap %u@%p, %u@%p\r\n",
            (unsigned int)&_heap_size, &_heap_start,
            (unsigned int)&_heap_wifi_size, &_heap_wifi_start
    );

    system_init();
    system_thread_init();

    puts("[OS] Starting proc_hellow_entry task...\r\n");

    puts("[OS] Starting aos_loop_proc task...\r\n");
    xTaskCreateStatic(aos_loop_proc, (char*)"event_loop", 1024, NULL, 15, aos_loop_proc_stack, &aos_loop_proc_task);
    puts("[OS] Starting TCP/IP Stack...\r\n");
    tcpip_init(NULL, NULL);

    puts("[OS] Starting OS Scheduler...\r\n");
    vTaskStartScheduler();
}
