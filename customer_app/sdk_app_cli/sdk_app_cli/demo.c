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
#include <stdio.h>
#include <stdlib.h>
#include <cli.h>
#include "bl_gpio.h"
#include "demo.h"
#include <FreeRTOS.h>
#include <task.h>

static void clear_leds()
{
    bl_gpio_output_set(11, 1);
    bl_gpio_output_set(17, 1);
    bl_gpio_output_set(14, 1);
}

static void cmd_cli(char *buf, int len, int argc, char **argv)
{
    printf("hello world\r\n");
}

static void test_cmd(char *buf, int len, int argc, char **argv)
{
    printf("test command");
}

static void blue_led(char *buf, int len, int argc, char **argv)
{
    if(argc != 2)
    {
        printf("usage: blue 1/0");
        return;
    }
    int blue_state = atoi(argv[1]);
    bl_gpio_output_set(11, blue_state);
    return;
}

static void red_led(char *buf, int len, int argc, char **argv)
{
    if(argc != 2)
    {
        printf("usage: red 1/0");
        return;
    }
    int red_state = atoi(argv[1]);
    bl_gpio_output_set(17, red_state);
    return;
}

static void green_led(char *buf, int len, int argc, char **argv)
{
    if(argc != 2)
    {
        printf("usage: red 1/0");
        return;
    }
    int green_state = atoi(argv[1]);
    bl_gpio_output_set(14, green_state);
    return;
}

static void led_flash(void *pvParameters)
{
    clear_leds();
    for(;;)
    {
        bl_gpio_output_set(11, 1);
        bl_gpio_output_set(17, 0);
        vTaskDelay(100);
        bl_gpio_output_set(17, 1);
        bl_gpio_output_set(14, 0);
        vTaskDelay(100);
        bl_gpio_output_set(14, 1);
        bl_gpio_output_set(11, 0);
        vTaskDelay(100);
    }
}

    TaskHandle_t ledflash_task;

static void ledflash_set(char *buf, int len, int argc, char **argv)
{
    if(argc != 2)
    {
        printf("usage: ledflash 1/0");
        return;
    }
    if(atoi(argv[1]) == 1)
    {
        printf("Resuming led flasher task");
        vTaskResume(ledflash_task);
    }
    else
    {
        printf("Suspending led flasher task");
        vTaskSuspend(ledflash_task);
        clear_leds();
    }
}

const static struct cli_command cmds_user[] STATIC_CLI_CMD_ATTRIBUTE = {
    {"test", "cli test", cmd_cli},
    {"test2", "test cmd", test_cmd},
    {"blue", "set blue led", blue_led},
    {"red", "set red led", red_led},
    {"green", "set green led", green_led},
    {"ledflash", "flash leds", ledflash_set}
};

int test_cli_init(void)
{
    // static command(s) do NOT need to call aos_cli_register_command(s) to register.
    // However, calling aos_cli_register_command(s) here is OK but is of no effect as cmds_user are included in cmds list.
    // XXX NOTE: Calling this *empty* function is necessary to make cmds_user in this file to be kept in the final link.
    //return aos_cli_register_commands(cmds_user, sizeof(cmds_user)/sizeof(cmds_user[0]));   
    xTaskCreate(led_flash, (char*)"led_flash", 1024, NULL, 14, &ledflash_task);
    vTaskSuspend(ledflash_task);          
    return 0;
}