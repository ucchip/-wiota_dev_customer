/*
 * Copyright (c) 2006-2020, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date                 Author                Notes
 * 20201-8-17     ucchip-wz          v0.00
 */
#include <rtthread.h>
#ifdef RT_USING_AT
#ifndef WIOTA_APP_DEMO
#ifdef UC8288_MODULE
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include "uc_wiota_api.h"
#include "uc_wiota_static.h"
#include "at.h"
#include "ati_prs.h"
#include "uc_string_lib.h"
#include "uc_adda.h"
//#include "uc_boot_download.h"
#include "at_wiota.h"

#ifdef GATEWAY_MODE_SUPPORT
#include "uc_cbor.h"
#include "uc_coding.h"
#include "uc_ota_flash.h"
boolean at_gateway_get_mode(void);
void at_gateway_stop_heart(void);
void at_gateway_recv_data_msg(u8_t *dl_data, int dl_data_len);
boolean at_gateway_whether_data_can_be_sent(void);
boolean at_gateway_whether_data_can_be_coding(void);
at_result_t at_gateway_mode_send_data(unsigned char *data, unsigned int data_len, int timeout);
at_result_t at_gateway_handle_send_ota_req_msg(void);
#endif

const u16_t symLen_mcs_byte[4][8] = {{7, 9, 52, 66, 80, 0, 0, 0},
                                     {7, 15, 22, 52, 108, 157, 192, 0},
                                     {7, 15, 31, 42, 73, 136, 255, 297},
                                     {7, 15, 31, 63, 108, 220, 451, 619}};

enum at_wiota_lpm
{
    AT_WIOTA_SLEEP = 0,
    AT_WIOTA_GATING,
};

enum at_wiota_log
{
    AT_LOG_CLOSE = 0,
    AT_LOG_OPEN,
    AT_LOG_UART0,
    AT_LOG_UART1,
    AT_LOG_SPI_CLOSE,
    AT_LOG_SPI_OPEN,
};

#define ADC_DEV_NAME "adc"
#define WIOTA_TRANS_END_STRING "EOF"

#define WIOTA_SCAN_FREQ_TIMEOUT 120000
#define WIOTA_SEND_TIMEOUT 60000
#define WIOTA_WAIT_DATA_TIMEOUT 10000
#define WIOTA_TRANS_AUTO_SEND 1000
#define WIOTA_SEND_DATA_MUX_LEN 1024
#define WIOTA_DATA_END 0x1A
#define WIOTA_TRANS_MAX_LEN 310
#define WIOTA_TRANS_END_STRING_MAX 8
#define WIOTA_TRANS_BUFF (WIOTA_TRANS_MAX_LEN + WIOTA_TRANS_END_STRING_MAX + CRC16_LEN + 1)

#define WIOTA_MUST_INIT(state)             \
    if (state != AT_WIOTA_INIT)            \
    {                                      \
        return AT_RESULT_REPETITIVE_FAILE; \
    }

#define WIOTA_CHECK_AUTOMATIC_MANAGER()   \
    if (uc_wiota_get_auto_connect_flag()) \
        return AT_RESULT_REFUSED;

enum at_test_mode_data_type
{
    AT_TEST_MODE_RECVDATA = 0,
    AT_TEST_MODE_QUEUE_EXIT,
};

typedef struct at_test_queue_data
{
    enum at_test_mode_data_type type;
    void *data;
    void *paramenter;
} t_at_test_queue_data;

typedef struct at_test_statistical_data
{
    int type;
    int dev;

    int upcurrentrate;
    int upaverate;
    int upminirate;
    int upmaxrate;

    int downcurrentrate;
    int downavgrate;
    int downminirate;
    int downmaxrate;

    int send_fail;
    int recv_fail;
    int max_mcs;
    int msc;
    int power;
    int rssi;
    int snr;
} t_at_test_statistical_data;

typedef struct at_test_data
{
    char type;
    char mode;
    short test_data_len;
    int time;
    int num;
    rt_timer_t test_mode_timer;
    rt_thread_t test_mode_task;
    //rt_thread_t test_data_task;
    rt_mq_t test_queue;
    rt_sem_t test_sem;
    char tast_state;
    t_at_test_statistical_data statistical;
} t_at_test_data;

enum at_test_communication_command
{
    AT_TEST_COMMAND_DEFAULT = 0,
    AT_TEST_COMMAND_UP_TEST,
    AT_TEST_COMMAND_DOWN_TEST,
    AT_TEST_COMMAND_LOOP_TEST,
    AT_TEST_COMMAND_DATA_MODE,
    AT_TEST_COMMAND_DATA_DOWN,
    AT_TEST_COMMAND_STOP,
};

#define AT_TEST_COMMUNICATION_HEAD_LEN 9
//#define AT_TEST_COMMUNICATION_RESERVED_LEN 4
#define AT_TEST_COMMUNICATION_HEAD "testMode"
typedef struct at_test_communication
{
    char head[AT_TEST_COMMUNICATION_HEAD_LEN];
    char command;
    char timeout;
    char mcs_num;
    short test_len;
    short all_len;
} t_at_test_communication;

#define AT_TEST_COMMUNICATION_DATA_LEN 40
#define AT_TEST_TIMEROUT 200

#define AT_TEST_GET_RATE(TIME, NUM, LEN, CURRENT, AVER, MIN, MAX) \
    {                                                             \
        CURRENT = LEN * 1000 / TIME;                              \
        if (AVER == 0)                                            \
        {                                                         \
            AVER = CURRENT;                                       \
        }                                                         \
        else                                                      \
        {                                                         \
            AVER = (AVER * NUM + CURRENT) / (NUM + 1);            \
        }                                                         \
        if (MIN > CURRENT || MIN == 0)                            \
        {                                                         \
            MIN = CURRENT;                                        \
        }                                                         \
        if (MAX < CURRENT || MAX == 0)                            \
        {                                                         \
            MAX = CURRENT;                                        \
        }                                                         \
    }

#define AT_TEST_CALCUTLATE(RESULT, ALL, BASE)                \
    {                                                        \
        if (0 != ALL)                                        \
        {                                                    \
            float get_result = ((float)BASE) / ((float)ALL); \
            get_result = get_result * 100.0;                 \
            RESULT = (int)get_result;                        \
        }                                                    \
        else                                                 \
        {                                                    \
            RESULT = 0;                                      \
        }                                                    \
    }

extern dtu_send_t g_dtu_send;
extern at_server_t at_get_server(void);
extern char *parse(char *b, char *f, ...);
static u8_t at_test_mode_wiota_recv_fun(uc_recv_back_p recv_data);

static int wiota_state = AT_WIOTA_DEFAULT;
static t_at_test_data g_test_data = {0};
void at_wiota_set_state(int state)
{
    wiota_state = state;
}

int at_wiota_get_state(void)
{
    return wiota_state;
}

static rt_err_t get_char_timeout(rt_tick_t timeout, char *chr)
{
    at_server_t at_server = at_get_server();
    return at_server->get_char(at_server, chr, timeout);
}

static at_result_t at_wiota_version_query(void)
{
    u8_t version[15] = {0};
    u8_t git_info[36] = {0};
    u8_t time[36] = {0};
    u32_t cce_version = 0;

    uc_wiota_get_version(version, git_info, time, &cce_version);

    at_server_printfln("+WIOTAVERSION:%s", version);
    at_server_printfln("+GITINFO:%s", git_info);
    at_server_printfln("+TIME:%s", time);
    at_server_printfln("+CCEVERSION:%x", cce_version);

    return AT_RESULT_OK;
}

static at_result_t at_freq_query(void)
{
    at_server_printfln("+WIOTAFREQ=%d", uc_wiota_get_freq_info());

    return AT_RESULT_OK;
}

static at_result_t at_freq_setup(const char *args)
{
    int freq = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    WIOTA_MUST_INIT(wiota_state)

    args = parse((char *)(++args), "d", &freq);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_freq_info(freq);

    return AT_RESULT_OK;
}

static u32_t nth_power(u32_t num, u32_t n)
{
    u32_t s = 1;

    for (u32_t i = 0; i < n; i++)
    {
        s *= num;
    }
    return s;
}

static void convert_string_to_int(u8_t numLen, u8_t num, const u8_t *pStart, u8_t *array)
{
    u8_t *temp = NULL;
    u8_t len = 0;
    u8_t nth = numLen;

    temp = (u8_t *)rt_malloc(numLen);
    if (temp == NULL)
    {
        rt_kprintf("convert_string_to_int malloc failed\n");
        return;
    }

    for (len = 0; len < numLen; len++)
    {
        temp[len] = pStart[len] - '0';
        array[num] += nth_power(10, nth - 1) * temp[len];
        nth--;
    }
    rt_free(temp);
    temp = NULL;
}

static u8_t convert_string_to_array(u8_t *string, u8_t *array)
{
    u8_t *pStart = string;
    u8_t *pEnd = string;
    u8_t num = 0;
    u8_t numLen = 0;

    while (*pStart != '\0')
    {
        while (*pEnd != '\0')
        {
            if (*pEnd == ',')
            {
                convert_string_to_int(numLen, num, pStart, array);
                num++;
                pEnd++;
                pStart = pEnd;
                numLen = 0;
            }
            numLen++;
            pEnd++;
        }

        convert_string_to_int(numLen, num, pStart, array);
        num++;
        pStart = pEnd;
    }
    return num;
}

static at_result_t at_scan_freq_setup(const char *args)
{
    u8_t freqNum = 0;
    u32_t timeout = 0;
    u8_t *freqString = RT_NULL;
    u8_t *tempFreq = RT_NULL;
    uc_recv_back_t result;
    u8_t convertNum = 0;
    u8_t *freqArry = NULL;
    u32_t dataLen = 0;
    u32_t strLen = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    if (wiota_state != AT_WIOTA_RUN)
    {
        return AT_RESULT_REPETITIVE_FAILE;
    }

    args = parse((char *)(++args), "d,d,d", &timeout, &dataLen, &freqNum);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    strLen = dataLen;

    if (freqNum > 0)
    {
        freqString = (u8_t *)rt_malloc(dataLen);
        if (freqString == RT_NULL)
        {
            at_server_printfln("SEND FAIL");
            return AT_RESULT_NULL;
        }
        tempFreq = freqString;
        at_server_printfln("OK");
        at_server_printf(">");
        while (dataLen)
        {
            if (get_char_timeout(rt_tick_from_millisecond(WIOTA_WAIT_DATA_TIMEOUT), (char *)tempFreq) != RT_EOK)
            {
                at_server_printfln("get char failed!");
                rt_free(freqString);
                freqString = NULL;
                return AT_RESULT_NULL;
            }
            dataLen--;
            tempFreq++;
        }

        freqArry = (u8_t *)rt_malloc(freqNum * sizeof(u8_t));
        if (freqArry == NULL)
        {
            rt_free(freqString);
            freqString = NULL;
            return AT_RESULT_NULL;
        }
        rt_memset(freqArry, 0, freqNum * sizeof(u8_t));

        freqString[strLen - 2] = '\0';

        convertNum = convert_string_to_array(freqString, freqArry);
        if (convertNum != freqNum)
        {
            rt_free(freqString);
            freqString = NULL;
            rt_free(freqArry);
            freqArry = NULL;
            return AT_RESULT_FAILE;
        }
        rt_free(freqString);
        freqString = NULL;

        uc_wiota_scan_freq(freqArry, freqNum, timeout, RT_NULL, &result);

        rt_free(freqArry);
        freqArry = NULL;
    }
    else
    {
        // uc_wiota_scan_freq(RT_NULL, 0, WIOTA_SCAN_FREQ_TIMEOUT, RT_NULL, &result);
        uc_wiota_scan_freq(RT_NULL, 0, 0, RT_NULL, &result); // scan all wait for ever
    }

    if (UC_OP_SUCC == result.result)
    {
        uc_freq_scan_result_p freqlinst = (uc_freq_scan_result_p)result.data;
        int freq_num = result.data_len / sizeof(uc_freq_scan_result_t);

        at_server_printfln("+WIOTASCANFREQ:");

        for (int i = 0; i < freq_num; i++)
        {
            at_server_printfln("%d,%d,%d,%d", freqlinst->freq_idx, freqlinst->rssi, freqlinst->snr, freqlinst->is_synced);
            freqlinst++;
        }

        rt_free(result.data);
    }
    else
    {
        return AT_RESULT_NULL;
    }

    return AT_RESULT_OK;
}

static at_result_t at_dcxo_setup(const char *args)
{
    int dcxo = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    WIOTA_MUST_INIT(wiota_state)

    args = parse((char *)(++args), "y", &dcxo);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    // rt_kprintf("dcxo=0x%x\n", dcxo);
    uc_wiota_set_dcxo(dcxo);

    return AT_RESULT_OK;
}

static at_result_t at_userid_query(void)
{
    unsigned int id[2] = {0};
    unsigned char len = 0;

    uc_wiota_get_userid(&(id[0]), &len);
    at_server_printfln("+WIOTAUSERID=0x%x", id[0]);

    return AT_RESULT_OK;
}

static at_result_t at_userid_setup(const char *args)
{
    unsigned int userid[2] = {0};

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    WIOTA_MUST_INIT(wiota_state)

    args = parse((char *)(++args), "y", &userid[0]);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    // rt_kprintf("userid:%x\n", userid[0]);

    uc_wiota_set_userid(userid, 4);

    return AT_RESULT_OK;
}

static rt_uint32_t at_temp_query(void)
{
    rt_device_t adc_dev;

    adc_dev = rt_device_find(ADC_DEV_NAME);
    if (RT_NULL == adc_dev)
    {
        rt_kprintf("ad find %s fail\n", ADC_DEV_NAME);
        return 0;
    }

    rt_adc_enable((rt_adc_device_t)adc_dev, ADC_CONFIG_CHANNEL_CHIP_TEMP);
    return rt_adc_read((rt_adc_device_t)adc_dev, ADC_CONFIG_CHANNEL_CHIP_TEMP);
}

static at_result_t at_radio_query(void)
{
    rt_uint32_t temp = 0;
    radio_info_t radio;

    if (AT_WIOTA_RUN != wiota_state)
    {
        rt_kprintf("%s line %d wiota state error %d\n", __FUNCTION__, __LINE__, wiota_state);
        return AT_RESULT_FAILE;
    }

    temp = at_temp_query();

    uc_wiota_get_radio_info(&radio);
    //temp,rssi,ber,snr,cur_power,max_pow,cur_mcs,max_mcs
    at_server_printfln("+WIOTARADIO=%d,-%d,%d,%d,%d,%d,%d,%d,%d,%d",
                       temp, radio.rssi, radio.ber, radio.snr, radio.cur_power,
                       radio.min_power, radio.max_power, radio.cur_mcs, radio.max_mcs, radio.frac_offset);

    return AT_RESULT_OK;
}

static at_result_t at_system_config_query(void)
{
    sub_system_config_t config;
    uc_wiota_get_system_config(&config);

    at_server_printfln("+WIOTASYSTEMCONFIG=%d,%d,%d,%d,%d,%d,%d,0x%x,0x%x",
                       config.id_len, config.symbol_length, config.dlul_ratio,
                       config.btvalue, config.group_number, config.ap_max_pow,
                       config.spectrum_idx, config.systemid, config.subsystemid);

    return AT_RESULT_OK;
}

static at_result_t at_system_config_setup(const char *args)
{
    sub_system_config_t config;
    unsigned int temp[7];

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    WIOTA_MUST_INIT(wiota_state)

    args = parse((char *)(++args), "d,d,d,d,d,d,d,y,y",
                 &temp[0], &temp[1], &temp[2],
                 &temp[3], &temp[4], &temp[5],
                 &temp[6], &config.systemid, &config.subsystemid);

    config.id_len = (unsigned char)temp[0];
    config.symbol_length = (unsigned char)temp[1];
    config.dlul_ratio = (unsigned char)temp[2];
    config.btvalue = (unsigned char)temp[3];
    config.group_number = (unsigned char)temp[4];
    config.ap_max_pow = (char)(temp[5] - 20);
    config.spectrum_idx = (unsigned char)temp[6];

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    // default config
    config.pp = 1;

    // rt_kprintf("id_len=%d,symbol_len=%d,dlul=%d,bt=%d,group_num=%d,ap_max_pow=%d,spec_idx=%d,systemid=0x%x,subsystemid=0x%x\n",
    //            config.id_len, config.symbol_length, config.dlul_ratio,
    //            config.btvalue, config.group_number, config.ap_max_pow,
    //            config.spectrum_idx, config.systemid, config.subsystemid);

    uc_wiota_set_system_config(&config);

    return AT_RESULT_OK;
}

static at_result_t at_wiota_init_exec(void)
{
    WIOTA_CHECK_AUTOMATIC_MANAGER();

    if (wiota_state == AT_WIOTA_DEFAULT || wiota_state == AT_WIOTA_EXIT)
    {
        uc_wiota_init();
        wiota_state = AT_WIOTA_INIT;
        return AT_RESULT_OK;
    }

    return AT_RESULT_REPETITIVE_FAILE;
}

static u8_t at_test_mode_wiota_recv_fun(uc_recv_back_p recv_data)
{
    unsigned int send_data_address = 0;
    t_at_test_queue_data *queue_data = RT_NULL;
    uc_recv_back_p copy_recv_data = RT_NULL;
    rt_err_t re;

    t_at_test_communication *test_mode_data = (t_at_test_communication *)(recv_data->data);
    if (g_test_data.type != AT_TEST_COMMAND_DEFAULT && g_test_data.type != AT_TEST_COMMAND_LOOP_TEST)
    {
        if (0 == recv_data->result)
            rt_free(recv_data->data);
        return 1;
    }

    if (!(recv_data->data_len >= sizeof(t_at_test_communication) &&
          0 == strcmp(test_mode_data->head, AT_TEST_COMMUNICATION_HEAD)))
        return 0;

    if (!g_test_data.time)
    {
        if (0 == recv_data->result)
            rt_free(recv_data->data);
        return 1;
    }

    copy_recv_data = rt_malloc(sizeof(uc_recv_back_t));
    queue_data = rt_malloc(sizeof(t_at_test_queue_data));

    memcpy(copy_recv_data, recv_data, sizeof(uc_recv_back_t));

    queue_data->type = AT_TEST_MODE_RECVDATA;
    queue_data->data = copy_recv_data;

    send_data_address = (unsigned int)queue_data;

    re = rt_mq_send(g_test_data.test_queue, &send_data_address, 4);
    //at_send_queue(g_test_data.test_queue, data,  2000);
    if (RT_EOK != re)
    {
        rt_kprintf("%s line %d rt_mq_send error %d\n", __FUNCTION__, __LINE__, re);
        rt_free(copy_recv_data);
        rt_free(queue_data);

        if (0 == recv_data->result)
            rt_free(recv_data->data);
    }

    return 1;
}

void wiota_recv_callback(uc_recv_back_p data)
{
    // rt_kprintf("wiota_recv_callback result %d\n", data->result);

    if (0 == data->result)
    {
        if (/*g_test_data.time > 0 && */ at_test_mode_wiota_recv_fun(data))
            return;
#ifdef GATEWAY_MODE_SUPPORT
        at_gateway_recv_data_msg(data->data, data->data_len);
#endif
        if (g_dtu_send->flag && (!g_dtu_send->at_show))
        {
            at_send_data(data->data, data->data_len);
            rt_free(data->data);
            return;
        }
        if (data->type < UC_RECV_SCAN_FREQ)
        {
            if (AT_TEST_COMMAND_DATA_DOWN == g_test_data.type)
            {
                rt_free(data->data);
                return;
            }
#ifdef GATEWAY_MODE_SUPPORT
            if (!at_gateway_get_mode())
#endif
                at_server_printf("+WIOTARECV,%d,%d,", data->type, data->data_len);
        }
        else if (data->type == UC_RECV_SYNC_LOST)
        {
            at_server_printf("+WIOTASYNC,LOST");
#ifdef GATEWAY_MODE_SUPPORT
            at_gateway_stop_heart();
#endif
        }
#ifdef GATEWAY_MODE_SUPPORT
        if (!at_gateway_get_mode())
#endif
        {
            at_send_data(data->data, data->data_len);
            at_server_printfln("");
        }
        rt_free(data->data);
    }
}

static at_result_t at_wiota_cfun_setup(const char *args)
{
    int state = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    args = parse((char *)(++args), "d", &state);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    // rt_kprintf("state = %d\n", state);

    if (1 == state && wiota_state == AT_WIOTA_INIT)
    {
        uc_wiota_run();
        uc_wiota_register_recv_data_callback(wiota_recv_callback, UC_CALLBACK_NORAMAL_MSG);
        uc_wiota_register_recv_data_callback(wiota_recv_callback, UC_CALLBACK_STATE_INFO);
        wiota_state = AT_WIOTA_RUN;
    }
    else if (0 == state && wiota_state == AT_WIOTA_RUN)
    {
        uc_wiota_exit();
        wiota_state = AT_WIOTA_EXIT;
    }
    else
        return AT_RESULT_REPETITIVE_FAILE;

    return AT_RESULT_OK;
}

static at_result_t at_connect_query(void)
{
    at_server_printfln("+WIOTACONNECT=%d,%d", uc_wiota_get_state(), uc_wiota_get_active_time());

    return AT_RESULT_OK;
}

static at_result_t at_connect_setup(const char *args)
{
    int state = 0, timeout = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    args = parse((char *)(++args), "d,d", &state, &timeout);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    // rt_kprintf("state = %d, timeout=%d\n", state, timeout);

    if (wiota_state != AT_WIOTA_RUN)
        return AT_RESULT_REPETITIVE_FAILE;

    // rt_kprintf("state = %d, timeout=%d\n", state, timeout);

    if (timeout)
        uc_wiota_set_active_time((unsigned int)timeout);

    if (state)
    {
        uc_wiota_connect();
    }
    else
    {
        uc_wiota_disconnect();
    }

    return AT_RESULT_OK;
}

static at_result_t at_wiotasend_exec(void)
{
    uint8_t *sendbuffer = NULL;
    uint8_t *psendbuffer;
    rt_err_t result = RT_EOK;
    int length = 0;
    int send_result = 0;

    if (AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }

#ifdef GATEWAY_MODE_SUPPORT
    if (!at_gateway_whether_data_can_be_sent())
    {
        rt_kprintf("the gateway mode is not enabled or authentication is not suc, data cannot be sent\n");
        return AT_RESULT_FAILE;
    }
#endif

    sendbuffer = (uint8_t *)rt_malloc(WIOTA_SEND_DATA_MUX_LEN + CRC16_LEN); // reserve CRC16_LEN for low mac
    if (sendbuffer == RT_NULL)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    at_server_printf("\r\n>");
    //while(1)
    {
        psendbuffer = sendbuffer;
        length = WIOTA_SEND_DATA_MUX_LEN;

        while (length)
        {
            result = get_char_timeout(rt_tick_from_millisecond(WIOTA_WAIT_DATA_TIMEOUT), (char *)psendbuffer);
            if (result != RT_EOK)
            {
                break;
            }
            length--;
            psendbuffer++;
            //rt_kprintf("length=%d,psendbuffer=0x%x,psendbuffer=%c , 0x%x\n", length, psendbuffer, *psendbuffer, *psendbuffer);
            if (WIOTA_DATA_END == *psendbuffer)
            {
                break;
            }
        }
        if ((psendbuffer - sendbuffer) > 0)
        {
#ifdef GATEWAY_MODE_SUPPORT
            if (at_gateway_whether_data_can_be_coding())
            {
                send_result = at_gateway_mode_send_data(sendbuffer, psendbuffer - sendbuffer, WIOTA_SEND_TIMEOUT);
                rt_free(sendbuffer);
                sendbuffer = NULL;
                return send_result;
            }
            else
            {
#endif
                send_result = uc_wiota_send_data(sendbuffer, psendbuffer - sendbuffer, WIOTA_SEND_TIMEOUT, RT_NULL);
#ifdef GATEWAY_MODE_SUPPORT
            }
#endif
            //rt_kprintf("len=%d, sendbuffer=%s\n", psendbuffer - sendbuffer, sendbuffer);
            if (UC_OP_SUCC != send_result)
            {
                rt_free(sendbuffer);
                sendbuffer = RT_NULL;
                at_server_printfln("SEND FAIL");
                return AT_RESULT_FAILE;
            }
        }
    }
    at_server_printfln("SEND OK");
    rt_free(sendbuffer);
    sendbuffer = RT_NULL;
    return AT_RESULT_OK;
}

static at_result_t at_wiotasend_setup(const char *args)
{
    int length = 0, timeout = 0;
    unsigned char *sendbuffer = NULL;
    unsigned char *psendbuffer;
    int send_result = 0;

    if (AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }

    args = parse((char *)(++args), "d,d", &timeout, &length);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

#ifdef GATEWAY_MODE_SUPPORT
    if (!at_gateway_whether_data_can_be_sent())
    {
        rt_kprintf("the gateway mode is not enabled or authentication is not suc, data cannot be sent\n");
        return AT_RESULT_FAILE;
    }
#endif

    if (length > 0)
    {
        sendbuffer = (unsigned char *)rt_malloc(length + CRC16_LEN); // reserve CRC16_LEN for low mac
        if (sendbuffer == NULL)
        {
            at_server_printfln("SEND FAIL");
            return AT_RESULT_NULL;
        }
        psendbuffer = sendbuffer;
        //at_server_printfln("SUCC");
        at_server_printf(">");

        while (length)
        {
            if (get_char_timeout(rt_tick_from_millisecond(WIOTA_WAIT_DATA_TIMEOUT), (char *)psendbuffer) != RT_EOK)
            {
                at_server_printfln("SEND FAIL");
                rt_free(sendbuffer);
                return AT_RESULT_NULL;
            }
            length--;
            psendbuffer++;
        }
#ifdef GATEWAY_MODE_SUPPORT
        if (at_gateway_whether_data_can_be_coding())
        {
            send_result = at_gateway_mode_send_data(sendbuffer, psendbuffer - sendbuffer, timeout > 0 ? timeout : WIOTA_SEND_TIMEOUT);
            rt_free(sendbuffer);
            sendbuffer = NULL;
            return send_result;
        }
        else
        {
#endif
            send_result = uc_wiota_send_data(sendbuffer, psendbuffer - sendbuffer, timeout > 0 ? timeout : WIOTA_SEND_TIMEOUT, RT_NULL);
#ifdef GATEWAY_MODE_SUPPORT
        }
#endif
        if (UC_OP_SUCC == send_result)
        {
            rt_free(sendbuffer);
            sendbuffer = NULL;
            at_server_printfln("SEND SUCC");
            return AT_RESULT_OK;
        }
        else
        {
            rt_free(sendbuffer);
            sendbuffer = NULL;
            at_server_printfln("SEND FAIL");
            return AT_RESULT_NULL;
        }
    }
    return AT_RESULT_OK;
}

static at_result_t at_wiotatrans_process(u16_t timeout, char *strEnd)
{
    uint8_t *pBuff = RT_NULL;
    int result = 0;
    timeout = (timeout == 0) ? WIOTA_SEND_TIMEOUT : timeout;
    if ((RT_NULL == strEnd) || ('\0' == strEnd[0]))
    {
        strEnd = WIOTA_TRANS_END_STRING;
    }
    uint8_t nLenEnd = strlen(strEnd);
    //    uint8_t nStrEndCount = 0;
    int16_t nSeekRx = 0;
    char nRun = 1;
    //    char nCatchEnd = 0;
    char nSendFlag = 0;

    pBuff = (uint8_t *)rt_malloc(WIOTA_TRANS_BUFF);
    if (pBuff == RT_NULL)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    memset(pBuff, 0, WIOTA_TRANS_BUFF);
    at_server_printfln("\r\nEnter transmission mode >");

    while (nRun)
    {
        get_char_timeout(rt_tick_from_millisecond(-1), (char *)&pBuff[nSeekRx]);
        ++nSeekRx;
        if ((nSeekRx > 2) && ('\n' == pBuff[nSeekRx - 1]) && ('\r' == pBuff[nSeekRx - 2]))
        {
            nSendFlag = 1;
            nSeekRx -= 2;
            if ((nSeekRx >= nLenEnd) && pBuff[nSeekRx - 1] == strEnd[nLenEnd - 1])
            {
                int i = 0;
                for (i = 0; i < nLenEnd; ++i)
                {
                    if (pBuff[nSeekRx - nLenEnd + i] != strEnd[i])
                    {
                        break;
                    }
                }
                if (i >= nLenEnd)
                {
                    nSeekRx -= nLenEnd;
                    nRun = 0;
                }
            }
        }

        if ((nSeekRx > (WIOTA_TRANS_MAX_LEN + nLenEnd + 2)) || (nSendFlag && (nSeekRx > WIOTA_TRANS_MAX_LEN)))
        {
            at_server_printfln("\r\nThe message's length can not over 310 characters.");
            do
            {
                // discard any characters after the end string
                result = get_char_timeout(rt_tick_from_millisecond(200), (char *)&pBuff[0]);
            } while (RT_EOK == result);
            nSendFlag = 0;
            nSeekRx = 0;
            nRun = 1;
            memset(pBuff, 0, WIOTA_TRANS_BUFF);
            continue;
        }

        if (nSendFlag)
        {
            nSeekRx = (nSeekRx > WIOTA_TRANS_MAX_LEN) ? WIOTA_TRANS_MAX_LEN : nSeekRx;
            if (nSeekRx > 0)
            {
                if (UC_OP_SUCC == uc_wiota_send_data(pBuff, nSeekRx, timeout, RT_NULL))
                {
                    at_server_printfln("SEND SUCC");
                }
                else
                {
                    at_server_printfln("SEND FAIL");
                }
            }
            nSeekRx = 0;
            nSendFlag = 0;
            memset(pBuff, 0, WIOTA_TRANS_BUFF);
        }
    }

    do
    {
        // discard any characters after the end string
        result = get_char_timeout(rt_tick_from_millisecond(200), (char *)&pBuff[0]);
    } while (RT_EOK == result);

    at_server_printfln("\r\nLeave transmission mode");
    if (RT_NULL != pBuff)
    {
        rt_free(pBuff);
        pBuff = RT_NULL;
    }
    return AT_RESULT_OK;
}

static at_result_t at_wiotatrans_setup(const char *args)
{
    if (AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }
    int timeout = 0;
    char strEnd[WIOTA_TRANS_END_STRING_MAX + 1] = {0};

    args = parse((char *)(++args), "d,s", &timeout, (sl32_t)8, strEnd);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    int i = 0;
    for (i = 0; i < WIOTA_TRANS_END_STRING_MAX; i++)
    {
        if (('\r' == strEnd[i]) || ('\n' == strEnd[i]))
        {
            strEnd[i] = '\0';
            break;
        }
    }

    if (i <= 0)
    {
        strcpy(strEnd, WIOTA_TRANS_END_STRING);
    }
    return at_wiotatrans_process(timeout & 0xFFFF, strEnd);
}

static at_result_t at_wiotatrans_exec(void)
{
    if (AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }
    return at_wiotatrans_process(0, RT_NULL);
}

void dtu_send_process(void)
{
    int16_t nSeekRx = 0;
    uint8_t buff[WIOTA_TRANS_BUFF] = {0};
    rt_err_t result;

    int i = 0;
    for (i = 0; i < WIOTA_TRANS_END_STRING_MAX; i++)
    {
        if (('\0' == g_dtu_send->exit_flag[i]) ||
            ('\r' == g_dtu_send->exit_flag[i]) ||
            ('\n' == g_dtu_send->exit_flag[i]))
        {
            g_dtu_send->exit_flag[i] = '\0';
            break;
        }
    }
    g_dtu_send->flag_len = i;
    if (g_dtu_send->flag_len <= 0)
    {
        strcpy(g_dtu_send->exit_flag, WIOTA_TRANS_END_STRING);
        g_dtu_send->flag_len = strlen(WIOTA_TRANS_END_STRING);
    }
    g_dtu_send->timeout = g_dtu_send->timeout ? g_dtu_send->timeout : 5000;
    g_dtu_send->wait = g_dtu_send->wait ? g_dtu_send->wait : 200;

    while (g_dtu_send->flag)
    {
        result = get_char_timeout(rt_tick_from_millisecond(g_dtu_send->wait), (char *)&buff[nSeekRx]);
        if (RT_EOK == result)
        {
            nSeekRx++;
            if ((nSeekRx >= g_dtu_send->flag_len) && (buff[nSeekRx - 1] == g_dtu_send->exit_flag[g_dtu_send->flag_len - 1]))
            {

                int i = 0;
                for (i = 0; i < g_dtu_send->flag_len; ++i)
                {
                    if (buff[nSeekRx - g_dtu_send->flag_len + i] != g_dtu_send->exit_flag[i])
                    {
                        break;
                    }
                }
                if (i >= g_dtu_send->flag_len)
                {
                    nSeekRx -= g_dtu_send->flag_len;
                    g_dtu_send->flag = 0;
                }
            }
            if (g_dtu_send->flag && (nSeekRx > (WIOTA_TRANS_MAX_LEN + g_dtu_send->flag_len)))
            {
                // too long to send
                result = RT_ETIMEOUT;
            }
        }
        if ((nSeekRx > 0) && ((RT_EOK != result) || (0 == g_dtu_send->flag)))
        {
            // timeout to send
            if (nSeekRx > WIOTA_TRANS_MAX_LEN)
            {
                nSeekRx = WIOTA_TRANS_MAX_LEN;
                do
                {
                    // discard any characters after the end string
                    char ch;
                    result = get_char_timeout(rt_tick_from_millisecond(100), &ch);
                } while (RT_EOK == result);
            }

            if ((AT_WIOTA_RUN == wiota_state) &&
                (UC_OP_SUCC == uc_wiota_send_data(buff, nSeekRx, g_dtu_send->timeout, RT_NULL)))
            {
                if (g_dtu_send->at_show)
                {
                    at_server_printfln("SEND:%4d.", nSeekRx);
                }
            }
            else
            {
                at_server_printfln("SEND FAIL");
            }
            nSeekRx = 0;
            memset(buff, 0, WIOTA_TRANS_BUFF);
        }
    }
    do
    {
        // discard any characters after the end string
        result = get_char_timeout(rt_tick_from_millisecond(100), (char *)&buff[0]);
    } while (RT_EOK == result);
    if (0 == g_dtu_send->flag)
    {
        at_server_printfln("OK");
    }
}

static at_result_t at_wiota_dtu_send_setup(const char *args)
{
    if ((AT_WIOTA_RUN != wiota_state) || (RT_NULL == g_dtu_send))
    {
        return AT_RESULT_FAILE;
    }
    memset(g_dtu_send->exit_flag, 0, WIOTA_TRANS_END_STRING_MAX);
    int timeout = 0;
    int wait = 0;
    args = parse((char *)(++args), "d,d,s", &timeout, &wait, (sl32_t)WIOTA_TRANS_END_STRING_MAX, g_dtu_send->exit_flag);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    g_dtu_send->timeout = timeout & 0xFFFF;
    g_dtu_send->wait = wait & 0xFFFF;
    g_dtu_send->flag = 1;
    return AT_RESULT_OK;
}

static at_result_t at_wiota_dtu_send_exec(void)
{
    if (AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }
    g_dtu_send->flag = 1;
    return AT_RESULT_OK;
}

static at_result_t at_wiotarecv_setup(const char *args)
{
    unsigned short timeout = 0;
    uc_recv_back_t result;

    if (AT_WIOTA_DEFAULT == wiota_state)
    {
        return AT_RESULT_FAILE;
    }

    args = parse((char *)(++args), "d", &timeout);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (timeout < 1)
        timeout = WIOTA_WAIT_DATA_TIMEOUT;

    // rt_kprintf("timeout = %d\n", timeout);

    uc_wiota_recv_data(&result, timeout, RT_NULL);
    if (!result.result)
    {
        if (result.type < UC_RECV_SCAN_FREQ)
        {
            at_server_printf("+WIOTARECV,%d,%d,", result.type, result.data_len);
        }
        else if (result.type == UC_RECV_SYNC_LOST)
        {
            at_server_printf("+WIOTASYNC,LOST");
        }
        at_send_data(result.data, result.data_len);
        at_server_printfln("");
        rt_free(result.data);
        return AT_RESULT_OK;
    }
    else
    {
        return AT_RESULT_FAILE;
    }
}

static at_result_t at_wiota_recv_exec(void)
{
    uc_recv_back_t result;

    if (AT_WIOTA_DEFAULT == wiota_state)
    {
        return AT_RESULT_FAILE;
    }

    uc_wiota_recv_data(&result, WIOTA_WAIT_DATA_TIMEOUT, RT_NULL);
    if (!result.result)
    {
        if (result.type < UC_RECV_SCAN_FREQ)
        {
            at_server_printf("+WIOTARECV,%d,%d,", result.type, result.data_len);
        }
        else if (result.type == UC_RECV_SYNC_LOST)
        {
            at_server_printf("+WIOTASYNC,LOST");
        }
        at_send_data(result.data, result.data_len);
        at_server_printfln("");
        rt_free(result.data);
        return AT_RESULT_OK;
    }
    else
    {
        return AT_RESULT_FAILE;
    }
}

static at_result_t at_wiotalpm_setup(const char *args)
{
    int mode = 0, state = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    args = parse((char *)(++args), "d,d", &mode, &state);

    switch (mode)
    {
    case AT_WIOTA_SLEEP:
    {
        at_server_printfln("OK");

        while (1)
            ;
    }
    case AT_WIOTA_GATING:
    {
        uc_wiota_set_is_gating(state);
        break;
    }
    default:
        return AT_RESULT_FAILE;
    }
    return AT_RESULT_OK;
}

static at_result_t at_wiotarate_setup(const char *args)
{
    int rate_mode = 0xFF;
    int rate_value = 0xFF;

    args = parse((char *)(++args), "d,d", &rate_mode, &rate_value);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    at_server_printfln("+WIOTARATE: %d, %d", (unsigned char)rate_mode, (unsigned short)rate_value);
    uc_wiota_set_data_rate((unsigned char)rate_mode, (unsigned short)rate_value);

    return AT_RESULT_OK;
}

static at_result_t at_wiotapow_setup(const char *args)
{
    int mode = 0;
    int power = 0x7F;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    args = parse((char *)(++args), "d,d", &mode, &power);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    // at can't parse minus value for now
    if (mode == 0)
    {
        uc_wiota_set_cur_power((signed char)(power - 20));
    }
    else if (mode == 1)
    {
        uc_wiota_set_max_power((signed char)(power - 20));
    }

    return AT_RESULT_OK;
}

#if defined(RT_USING_CONSOLE) && defined(RT_USING_DEVICE)

void at_handle_log_uart(int uart_number)
{
    rt_device_t device = NULL;
    //    rt_device_t old_device = NULL;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT; /*init default parment*/

    device = rt_device_find(AT_SERVER_DEVICE);

    if (device)
    {
        rt_device_close(device);
    }

    if (0 == uart_number)
    {
        config.baud_rate = BAUD_RATE_460800;
        rt_console_set_device(AT_SERVER_DEVICE);
        //boot_set_uart0_baud_rate(BAUD_RATE_460800);
    }
    else if (1 == uart_number)
    {
        config.baud_rate = BAUD_RATE_115200;
        rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
        //boot_set_uart0_baud_rate(BAUD_RATE_115200);
    }

    if (device)
    {
        rt_device_control(device, RT_DEVICE_CTRL_CONFIG, &config);
        rt_device_open(device, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    }
}

#endif

static at_result_t at_wiotalog_setup(const char *args)
{
    int mode = 0;

    args = parse((char *)(++args), "d", &mode);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    switch (mode)
    {
    case AT_LOG_CLOSE:
    case AT_LOG_OPEN:
        uc_wiota_log_switch(UC_LOG_UART, mode - AT_LOG_CLOSE);
        break;

    case AT_LOG_UART0:
    case AT_LOG_UART1:
#if defined(RT_USING_CONSOLE) && defined(RT_USING_DEVICE)
        at_handle_log_uart(mode - AT_LOG_UART0);
#endif
        break;

    case AT_LOG_SPI_CLOSE:
    case AT_LOG_SPI_OPEN:
        uc_wiota_log_switch(UC_LOG_SPI, mode - AT_LOG_SPI_CLOSE);
        break;

    default:
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

static at_result_t at_wiotastats_query(void)
{
    uc_stats_info_t local_stats_t = {0};

    uc_wiota_get_all_stats(&local_stats_t);

    at_server_printfln("+WIOTASTATS=0,%d,%d,%d,%d,%d,%d,%d", local_stats_t.rach_fail, local_stats_t.active_fail, local_stats_t.ul_succ,
                       local_stats_t.dl_fail, local_stats_t.dl_succ, local_stats_t.bc_fail, local_stats_t.bc_succ);

    return AT_RESULT_OK;
}

static at_result_t at_wiotastats_setup(const char *args)
{
    int mode = 0;
    int type = 0;
    unsigned int back_stats;

    args = parse((char *)(++args), "d,d", &mode, &type);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (UC_STATS_READ == mode)
    {
        if (UC_STATS_TYPE_ALL == type)
        {
            at_wiotastats_query();
        }
        else
        {
            back_stats = uc_wiota_get_stats((unsigned char)type);
            at_server_printfln("+WIOTASTATS=%d,%d", type, back_stats);
        }
    }
    else if (UC_STATS_WRITE == mode)
    {
        uc_wiota_reset_stats((unsigned char)type);
    }
    else
    {
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

static at_result_t at_wiotacrc_query(void)
{
    at_server_printfln("+WIOTACRC=%d", uc_wiota_get_crc());

    return AT_RESULT_OK;
}

static at_result_t at_wiotacrc_setup(const char *args)
{
    int crc_limit = 0;

    args = parse((char *)(++args), "d", &crc_limit);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_crc((unsigned short)crc_limit);

    return AT_RESULT_OK;
}

static void at_test_get_wiota_info(t_at_test_statistical_data *info, int i_time)
{
    uc_throughput_info_t throughput_info;
    uc_stats_info_t stats_info_ptr;
    radio_info_t radio;

    uc_wiota_get_throughput(&throughput_info);
    uc_wiota_reset_throughput(UC_STATS_TYPE_ALL);
    // rt_kprintf("throughput_info.ul_succ_data_len = %d\n", throughput_info.ul_succ_data_len);
    AT_TEST_GET_RATE(i_time, g_test_data.num, throughput_info.ul_succ_data_len,
                     info->upcurrentrate, info->upaverate, info->upminirate, info->upmaxrate);
    // rt_kprintf("upcurren %d  upave %d min %d max %d\n", info->upcurrentrate, info->upaverate, info->upminirate, info->upmaxrate);

    // rt_kprintf("throughput_info.dl_succ_data_len = %d\n", throughput_info.dl_succ_data_len);
    AT_TEST_GET_RATE(i_time, g_test_data.num, throughput_info.dl_succ_data_len,
                     info->downcurrentrate, info->downavgrate, info->downminirate, info->downmaxrate);
    g_test_data.num++;

    uc_wiota_get_all_stats(&stats_info_ptr);
    //    uc_wiota_reset_stats(UC_STATS_TYPE_ALL);

    // rt_kprintf("rach_fail=%d active_fail=%d ul_succ=%d\n", stats_info_ptr.rach_fail, stats_info_ptr.active_fail, stats_info_ptr.ul_succ);
    AT_TEST_CALCUTLATE(info->send_fail,
                       stats_info_ptr.rach_fail + stats_info_ptr.active_fail + stats_info_ptr.ul_succ,
                       stats_info_ptr.rach_fail + stats_info_ptr.active_fail);

    // rt_kprintf("dl_fail=%d dl_succ=%d\n", stats_info_ptr.dl_fail, stats_info_ptr.dl_succ);
    AT_TEST_CALCUTLATE(info->recv_fail,
                       stats_info_ptr.dl_fail + stats_info_ptr.dl_succ,
                       stats_info_ptr.dl_fail);

    uc_wiota_get_radio_info(&radio);
    info->max_mcs = radio.max_mcs;
    info->msc = radio.cur_mcs;
    info->power = radio.cur_power;
    info->rssi = radio.rssi;
    info->snr = radio.snr;
}

static int at_test_mcs_rate(int mcs)
{
    sub_system_config_t config;
    uc_wiota_get_system_config(&config);
    int frameLen = uc_wiota_get_frame_len();
    int result = 8 * symLen_mcs_byte[config.symbol_length][mcs] * 1000 / frameLen;
    return result;
}

static void at_test_report_to_uart(void)
{
    at_test_get_wiota_info(&g_test_data.statistical, g_test_data.time);
    int rate_mcs = at_test_mcs_rate(g_test_data.statistical.max_mcs);
    //    unsigned int total;
    //    unsigned int used;
    //    unsigned int max_used;
    unsigned int dl_fail = uc_wiota_get_stats(UC_STATS_DL_FAIL);
    unsigned int dl_succ = uc_wiota_get_stats(UC_STATS_DL_SUCC);
    unsigned int dl_rato = 0;
    //    uc_stats_info_t local_stats_t = {0};

    //    uc_wiota_get_all_stats(&local_stats_t);
    //    dl_fail = local_stats_t.dl_fail;
    //    dl_succ = local_stats_t.dl_succ;
    if (0 != (dl_succ + dl_fail))
    {
        dl_rato = dl_fail * 100 / (dl_succ + dl_fail);
    }
    uc_wiota_reset_stats(UC_STATS_TYPE_ALL);

    //    rt_memory_info(&total,&used,&max_used);
    switch (g_test_data.type)
    {
    case AT_TEST_COMMAND_UP_TEST:
        at_server_printfln("+UP: %dbps %dbps %d rssi -%d snr %d",
                           g_test_data.statistical.upcurrentrate / 1000 * 8, rate_mcs,
                           g_test_data.statistical.msc, g_test_data.statistical.rssi, g_test_data.statistical.snr);
        break;
    case AT_TEST_COMMAND_DOWN_TEST:
        at_server_printfln("+DOWN: %dbps %d rssi -%d snr %d",
                           g_test_data.statistical.downcurrentrate / 1000 * 8,
                           g_test_data.statistical.msc, g_test_data.statistical.rssi, g_test_data.statistical.snr);
        break;
    case AT_TEST_COMMAND_LOOP_TEST:
        at_server_printfln("+LOOP: %dbps %dbps %dbps %d rssi -%d snr %d",
                           g_test_data.statistical.upcurrentrate / 1000 * 8,
                           g_test_data.statistical.downcurrentrate / 1000 * 8, rate_mcs,
                           g_test_data.statistical.msc, g_test_data.statistical.rssi, g_test_data.statistical.snr);
        break;
    case AT_TEST_COMMAND_DATA_MODE:
        at_server_printfln("+DATA: %dbps %dbps %d rssi -%d snr %d",
                           g_test_data.statistical.downcurrentrate / 1000 * 8, rate_mcs,
                           g_test_data.statistical.msc, g_test_data.statistical.rssi, g_test_data.statistical.snr);
        break;
    case AT_TEST_COMMAND_DATA_DOWN:
        at_server_printfln("+DATADOWN: %dbps %d rssi -%d snr %d dl succ %d fail %d rato %d%",
                           g_test_data.statistical.downcurrentrate / 1000 * 8,
                           g_test_data.statistical.msc, g_test_data.statistical.rssi, g_test_data.statistical.snr,
                           dl_succ, dl_fail, dl_rato);
        break;
    default:
        break;
    }
}

static void at_test_mode_time_fun(void *parameter)
{
    at_test_report_to_uart();
}

static void at_test_mode_task_fun(void *parameter)
{
    t_at_test_data *test_data = &g_test_data;
    t_at_test_queue_data *recv_queue_data = RT_NULL;
    unsigned int queue_data_on = 0;
    int send_flag = 0;
    t_at_test_communication *communication = RT_NULL;
    int data_test_flag = 1;

    test_data->test_data_len = sizeof(t_at_test_communication);

    communication = rt_malloc(sizeof(t_at_test_communication) + CRC16_LEN);
    if (RT_NULL == communication)
    {
        rt_kprintf("at_test_mode_task_fun rt_malloc error\n");
        return;
    }
    memset(communication, 0, sizeof(t_at_test_communication));
    memcpy(communication->head, AT_TEST_COMMUNICATION_HEAD, strlen(AT_TEST_COMMUNICATION_HEAD));
    communication->command = AT_TEST_COMMAND_DEFAULT;
    communication->timeout = 0;
    communication->all_len = sizeof(t_at_test_communication);
    //test_data->test_mode_timer = RT_NULL;
    test_data->type = AT_TEST_COMMAND_DEFAULT;
    int time_start_flag = 1;
    while (1)
    {
        // recv queue data. wait start test
        if (RT_EOK == rt_mq_recv(test_data->test_queue, &queue_data_on, 4, 100)) // RT_WAITING_NO
        {
            recv_queue_data = (t_at_test_queue_data *)queue_data_on;
            rt_kprintf("data->type = %d\n", recv_queue_data->type);

            switch ((int)recv_queue_data->type)
            {
            case AT_TEST_MODE_RECVDATA:
            {
                uc_recv_back_p recv_data = recv_queue_data->data;
                t_at_test_communication *communication_data = (t_at_test_communication *)(recv_data->data);
                //pasre data
                test_data->time = communication_data->timeout;
                test_data->type = communication_data->command;
                test_data->test_data_len = communication_data->test_len;

                if (communication->all_len != communication_data->all_len || communication->command != communication_data->command)
                {
                    // re malloc
                    if (AT_TEST_COMMAND_DATA_MODE != test_data->type)
                    {
                        rt_free(communication);
                        communication = rt_malloc(communication_data->all_len + CRC16_LEN);
                        memset(communication, 0, communication_data->all_len);
                        memcpy(communication, communication_data, communication_data->all_len);
                        if (RT_NULL == communication)
                        {
                            rt_kprintf("at_test_mode_task_fun line %d rt_malloc error\n", __LINE__);
                            return;
                        }
                    }
                }

                if (RT_NULL != test_data->test_mode_timer && AT_TEST_COMMAND_DATA_MODE != test_data->type && time_start_flag)
                {
                    //if (test_data->test_mode_timer)
                    // {
                    int timeout = test_data->time * 1000;
                    rt_timer_control(test_data->test_mode_timer, RT_TIMER_CTRL_SET_TIME, &timeout);
                    rt_timer_start(test_data->test_mode_timer);
                    time_start_flag = 0;
                    // }
                }

                // loop data to ap
                if (AT_TEST_COMMAND_LOOP_TEST == test_data->type)
                {
                    send_flag = 0;
                }

                //free data
                rt_free(communication_data);
                rt_free(recv_data);
                rt_free(recv_queue_data);

                if (AT_TEST_COMMAND_DATA_MODE == test_data->type && 1 == data_test_flag)
                {
                    uc_wiota_set_data_rate(UC_RATE_NORMAL, communication_data->mcs_num);
                    uc_wiota_test_loop(1);
                    data_test_flag = 0;
                }
                break;
            }
            case AT_TEST_MODE_QUEUE_EXIT:
            {
                if (AT_TEST_COMMAND_DATA_MODE == g_test_data.type)
                {
                    uc_wiota_set_data_rate(UC_RATE_NORMAL, UC_MCS_AUTO);
                    uc_wiota_test_loop(0);
                }
                else
                {
                    communication->command = AT_TEST_COMMAND_STOP; // also stop cmd
                    uc_wiota_send_data((unsigned char *)communication, communication->all_len, 20000, RT_NULL);
                }

                rt_free(recv_queue_data);
                rt_free(communication);
                rt_sem_release(test_data->test_sem);
                return;
            }
            }
        }

        if (test_data->type == AT_TEST_COMMAND_UP_TEST || send_flag == 0)
        {
            extern void algo_srand(u32_t seedSet);
            extern u32_t algo_rand(void);
            extern u32_t l1_read_dfe_counter(void);
            UC_OP_RESULT res;

            if (test_data->type == AT_TEST_COMMAND_UP_TEST)
            {
                unsigned char *send_test = rt_malloc(test_data->test_data_len + CRC16_LEN);

                algo_srand(l1_read_dfe_counter());
                for (int i = 0; i < test_data->test_data_len; i++)
                {
                    *((u8_t *)send_test + i) = algo_rand() & 0xFF;
                }
                res = uc_wiota_send_data(send_test, test_data->test_data_len, 20000, RT_NULL);
                rt_free(send_test);
            }
            else
            {
                res = uc_wiota_send_data((unsigned char *)communication, communication->all_len, 20000, RT_NULL);
            }

            if (res == UC_OP_SUCC)
            {
                send_flag = 1;
            }
        }
    }
}

static at_result_t at_test_mode_start_setup(const char *args)
{
    int mode = 0;
    int time = 0;

    args = parse((char *)(++args), "d,d", &mode, &time);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (wiota_state != AT_WIOTA_RUN)
        return AT_RESULT_REPETITIVE_FAILE;

    if (g_test_data.time > 0)
    {
        rt_kprintf("%s line %d repeated use\n", __FUNCTION__, __LINE__);
        return AT_RESULT_PARSE_FAILE;
    }

    g_test_data.mode = mode;
    g_test_data.time = 1;

    //create timer
    if (g_test_data.test_mode_timer == NULL)
    {
        g_test_data.test_mode_timer = rt_timer_create("teMode",
                                                      at_test_mode_time_fun,
                                                      RT_NULL,
                                                      g_test_data.time * 1000,
                                                      RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);

        if (RT_NULL == g_test_data.test_mode_timer)
        {
            rt_kprintf("%s line %d rt_timer_create error\n", __FUNCTION__, __LINE__);
            return AT_RESULT_PARSE_FAILE;
        }
    }

    if (mode == 0)
    {
        g_test_data.type = AT_TEST_COMMAND_DATA_DOWN;

        /* fix bug237: watchdog timeout when throughput test */
        g_test_data.time = (time == 0) ? 3 : time;

        int timeout = g_test_data.time * 1000;
        rt_timer_control(g_test_data.test_mode_timer, RT_TIMER_CTRL_SET_TIME, &timeout);
        rt_timer_start(g_test_data.test_mode_timer);
        uc_wiota_test_loop(2);
    }
    else if (mode == 1)
    {
        //create queue
        g_test_data.test_queue = rt_mq_create("teMode", 4, 8, RT_IPC_FLAG_PRIO);
        if (RT_NULL == g_test_data.test_queue)
        {
            rt_kprintf("%s line %d at_create_queue error\n", __FUNCTION__, __LINE__);
            return AT_RESULT_PARSE_FAILE;
        }

        //create sem
        g_test_data.test_sem = rt_sem_create("teMode", 0, RT_IPC_FLAG_PRIO);
        if (RT_NULL == g_test_data.test_sem)
        {
            rt_kprintf("%s line %d rt_sem_create error\n", __FUNCTION__, __LINE__);
            return AT_RESULT_PARSE_FAILE;
        }

        //create task
        g_test_data.test_mode_task = rt_thread_create("teMode",
                                                      at_test_mode_task_fun,
                                                      RT_NULL,
                                                      2048,
                                                      RT_THREAD_PRIORITY_MAX / 3 - 1,
                                                      3);
        if (RT_NULL == g_test_data.test_mode_task)
        {
            rt_kprintf("%s line %d rt_thread_create error\n", __FUNCTION__, __LINE__);
            return AT_RESULT_PARSE_FAILE;
        }
        rt_thread_startup(g_test_data.test_mode_task);
    }
    else
    {
        return AT_RESULT_PARSE_FAILE;
    }

    return AT_RESULT_OK;
}

static at_result_t at_test_mode_stop_exec(void)
{
    if (g_test_data.mode == 0)
    {
        uc_wiota_test_loop(0);

        if (RT_NULL != g_test_data.test_mode_timer)
        {
            rt_timer_delete(g_test_data.test_mode_timer);
        }

        rt_memset(&g_test_data, 0, sizeof(g_test_data));
    }
    else
    {
        unsigned int send_data_address;

        if (g_test_data.time < 1)
        {
            rt_kprintf("%s line %d no run\n", __FUNCTION__, __LINE__);
            return AT_RESULT_PARSE_FAILE;
        }

        g_test_data.time = 0;

        t_at_test_queue_data *data = rt_malloc(sizeof(t_at_test_queue_data));

        if (RT_NULL == data)
        {
            rt_kprintf("%s line %d rt_malloc error\n", __FUNCTION__, __LINE__);
            return AT_RESULT_PARSE_FAILE;
        }
        data->type = AT_TEST_MODE_QUEUE_EXIT;
        send_data_address = (unsigned int)data;

        rt_err_t res = rt_mq_send_wait(g_test_data.test_queue, &send_data_address, 4, 1000);
        if (0 != res)
        {
            rt_kprintf("%s line %d rt_mq_send_wait error\n", __FUNCTION__, __LINE__);
            rt_free(data);
            return AT_RESULT_PARSE_FAILE;
        }

        //wait  RT_WAITING_FOREVER
        if (RT_EOK != rt_sem_take(g_test_data.test_sem, RT_WAITING_FOREVER))
        {
            rt_kprintf("%s line %d rt_sem_take error\n", __FUNCTION__, __LINE__);
            return AT_RESULT_PARSE_FAILE;
        }

        if (RT_NULL != g_test_data.test_mode_timer)
        {
            rt_timer_stop(g_test_data.test_mode_timer);
            rt_timer_delete(g_test_data.test_mode_timer);
        }

        rt_mq_delete(g_test_data.test_queue);
        rt_sem_delete(g_test_data.test_sem);
        rt_thread_delete(g_test_data.test_mode_task);
        rt_memset(&g_test_data, 0, sizeof(g_test_data));
    }
    return AT_RESULT_OK;
}

static at_result_t at_wiotaosc_query(void)
{
    at_server_printfln("+WIOTAOSC=%d", uc_wiota_get_is_osc());

    return AT_RESULT_OK;
}

static at_result_t at_wiotaosc_setup(const char *args)
{
    int mode = 0;

    args = parse((char *)(++args), "d", &mode);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_is_osc((unsigned char)mode);

    return AT_RESULT_OK;
}

static at_result_t at_wiotalight_setup(const char *args)
{
    int mode = 0;

    args = parse((char *)(++args), "d", &mode);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_light_func_enable((unsigned char)mode);

    return AT_RESULT_OK;
}

static at_result_t at_wiota_save_static_exec(void)
{
    WIOTA_CHECK_AUTOMATIC_MANAGER();

    uc_wiota_save_static_info();

    return AT_RESULT_OK;
}

#ifdef GATEWAY_MODE_SUPPORT
#define AUTH_CODE_LEN 18
#define GATEWAY_DEV_TYPE_LEN 14
#define GATEWAY_VERSION_LEN 16
#define GATEWAY_SOFT_VERSION "v1.0"

#define GATEWAY_OTA_VER_PERIOD (7200000) // 2 hours
#define GATEWAY_OTA_FLASH_BIN_SIZE 328
#define GATEWAY_OTA_FLASH_REVERSE_SIZE 136
#define GATEWAY_OTA_FLASH_OTA_SIZE 40
#define GATEWAY_OTA_BLOCK_SIZE 512

#define GATEWAY_MODE_OTA_START_ADDR ((GATEWAY_OTA_FLASH_BIN_SIZE + GATEWAY_OTA_FLASH_REVERSE_SIZE) * 1024)

#define GATEWAY_MSG_CODE_DL_RECV (1L << 1)
#define GATEWAY_MSG_CODE_UL_HEART (1L << 2)
#define GATEWAY_MSG_CODE_OTA_REQ (1L << 3)
#define GATEWAY_MSG_CODE_UL_MISS_DATA_REQ (1L << 4)

#define SET_BIT(value, bit) (value |= (1 << bit))
#define CLEAR_BIT(value, bit) (value &= ~(1 << bit))
#define JUDGMENT_BIT(value, bit) (value >> bit & 1)

typedef struct at_gateway_msg
{
    void *data;
    unsigned int data_len;
    int msg_code;
} at_gateway_msg_t;

typedef enum
{
    GATEWAYMODE_SEND_SUC = 0,
    GATEWAYMODE_SEND_FAIL = 1,
    GATEWAYMODE_NET_NOT_CON = 2
} at_gateway_send_res_e;

typedef enum
{
    GATEWAY_OTA_DEFAULT = 0,
    GATEWAY_OTA_DOWNLOAD = 1,
    GATEWAY_OTA_PROGRAM = 2,
    GATEWAY_OTA_STOP = 3
} at_gateway_ota_state_e;

typedef struct at_gateway_mode
{
    rt_thread_t gateway_handler;
    rt_mq_t gateway_mq;
    rt_timer_t heart_timer;
    rt_sem_t gateway_sem;
    at_gateway_send_res_e send_result;
    e_auth_state auth_state;
    char auth_code[16];
    int heart_timeout;
    unsigned int wiota_id;
    unsigned int dev_id;
    boolean heart_state;
    boolean gateway_mode;
    boolean reboot_flag;

    // ota used
    rt_timer_t ota_timer;
    rt_timer_t ver_timer;
    at_gateway_ota_state_e ota_state;
    int upgrade_type;
    char new_version[16];
    char device_type[12];
    unsigned int block_count;
    unsigned int block_size;
    unsigned char *mask_map;
    int miss_data_num;
    boolean miss_data_req;
} at_gateway_mode_t;

static at_gateway_mode_t gateway_mode = {0};

static void at_gateway_set_auth_state(e_auth_state state)
{
    gateway_mode.auth_state = state;
}

static e_auth_state at_gateway_get_auth_state(void)
{
    return gateway_mode.auth_state;
}

static void at_gateway_set_heart(boolean state, int timeout)
{
    gateway_mode.heart_state = state;
    gateway_mode.heart_timeout = timeout;
}

static boolean at_gateway_get_heart_state(void)
{
    return gateway_mode.heart_state;
}

static int at_gateway_get_heart_timeout(void)
{
    return gateway_mode.heart_timeout;
}

boolean at_gateway_get_mode(void)
{
    return gateway_mode.gateway_mode;
}

static void at_gateway_set_mode(boolean gw_mode)
{
    gateway_mode.gateway_mode = gw_mode;
}

boolean at_gateway_get_reboot(void)
{
    return gateway_mode.reboot_flag;
}

void at_gateway_set_reboot(boolean reboot_flag)
{
    gateway_mode.reboot_flag = reboot_flag;
}

static unsigned int at_gateway_get_dev_id(void)
{
    return gateway_mode.dev_id;
}

static void at_gateway_set_dev_id(unsigned int dev_id)
{
    gateway_mode.dev_id = dev_id;
}

unsigned int at_gateway_get_wiota_id(void)
{
    return gateway_mode.wiota_id;
}

void at_gateway_set_wiota_id(unsigned int wiota_id)
{
    gateway_mode.wiota_id = wiota_id;
}

void at_gateway_stop_heart(void)
{
    if (at_gateway_get_heart_state())
    {
        at_gateway_set_heart(0, 0);
        rt_timer_stop(gateway_mode.heart_timer);
    }
}

boolean at_gateway_whether_data_can_be_sent(void)
{
    if (!at_gateway_get_mode())
    {
        return TRUE;
    }
    else
    {
        if (at_gateway_get_auth_state() == AUTHENTICATION_SUC)
        {
            return TRUE;
        }
    }

    return FALSE;
}

boolean at_gateway_whether_data_can_be_coding(void)
{
    if (at_gateway_get_mode() && at_gateway_get_auth_state() == AUTHENTICATION_SUC)
    {
        return TRUE;
    }

    return FALSE;
}

void at_gateway_release_sem(void)
{
    if (gateway_mode.gateway_sem)
    {
        rt_sem_release(gateway_mode.gateway_sem);
    }
}

at_result_t at_gateway_mode_send_data(unsigned char *data, unsigned int data_len, int timeout)
{
    unsigned char *data_coding = RT_NULL;
    unsigned int data_coding_len = 0;
    app_ps_header_t ps_header = {0};
    int send_result = 0;
    const char *res_str[3] = {"SEND SUC", "SEND FAIL", "NET_NOT_CONNECT"};

    app_set_header_property(PRO_SRC_ADDR, 1, &ps_header.property);
    app_set_header_property(PRO_NEED_RES, 1, &ps_header.property);
    ps_header.cmd_type = IOTE_USER_DATA;
    ps_header.addr.src_addr = at_gateway_get_dev_id();
    ps_header.packet_num = app_packet_num();

    if (0 != app_data_coding(&ps_header, data, data_len, &data_coding, &data_coding_len))
    {
        rt_kprintf("%s line %d app_data_coding fail\n", __FUNCTION__, __LINE__);
        return AT_RESULT_FAILE;
    }

    send_result = uc_wiota_send_data(data_coding, data_coding_len, timeout, RT_NULL);
    rt_free(data_coding);

    if (RT_EOK != rt_sem_take(gateway_mode.gateway_sem, timeout))
    {
        at_server_printfln("SEND TIMEOUT");
        return AT_RESULT_FAILE;
    }
    else
    {
        at_server_printfln("%s", res_str[gateway_mode.send_result]);
        if ((gateway_mode.send_result == GATEWAYMODE_SEND_SUC) && (send_result == UC_OP_SUCC))
        {
            return AT_RESULT_OK;
        }
        else
        {
            return AT_RESULT_FAILE;
        }
    }
}

static void *at_gateway_create_queue(const char *name, unsigned int max_msgs, unsigned char flag)
{
    rt_mq_t mq = rt_malloc(sizeof(struct rt_messagequeue));
    void *msgpool = rt_malloc(4 * max_msgs);

    if (RT_NULL == mq || RT_NULL == msgpool)
        return RT_NULL;

    if (RT_EOK != rt_mq_init(mq, name, msgpool, 4, 4 * max_msgs, flag))
        return RT_NULL;

    return (void *)mq;
}

static int at_gateway_recv_queue(void *queue, void **buf, signed int timeout)
{
    unsigned int address = 0;
    int result = 0;
    result = rt_mq_recv(queue, &address, 4, timeout);
    *buf = (void *)address;

    return result;
}

static int at_gateway_send_queue(void *queue, void *buf, signed int timeout)
{
    unsigned int address = (unsigned int)buf;

    return rt_mq_send_wait(queue, &address, 4, timeout);
}

static int at_gateway_dele_queue(void *queue)
{
    rt_err_t ret = rt_mq_detach(queue);
    rt_free(((rt_mq_t)queue)->msg_pool);
    rt_free(queue);
    return ret;
}

void at_gateway_recv_data_msg(u8_t *dl_data, int dl_data_len)
{
    if (at_gateway_get_mode())
    {
        at_gateway_msg_t *recv_data = rt_malloc(sizeof(at_gateway_msg_t));
        if (RT_NULL == recv_data)
        {
            rt_kprintf("%s line %d malloc fail\n", __FUNCTION__, __LINE__);
            return;
        }
        rt_memset(recv_data, 0, sizeof(at_gateway_msg_t));

        u8_t *temp_data = rt_malloc(dl_data_len);
        if (RT_NULL == temp_data)
        {
            rt_kprintf("%s line %d malloc fail\n", __FUNCTION__, __LINE__);
            rt_free(recv_data);
            return;
        }
        rt_memset(temp_data, 0, dl_data_len);
        rt_memcpy(temp_data, dl_data, dl_data_len);

        recv_data->data = temp_data;
        recv_data->data_len = dl_data_len;
        recv_data->msg_code = GATEWAY_MSG_CODE_DL_RECV;
        if (RT_EOK != at_gateway_send_queue(gateway_mode.gateway_mq, recv_data, 10000))
        {
            rt_free(temp_data);
            rt_free(recv_data);
            rt_kprintf("%s line %d gateway send queue fail\n", __FUNCTION__, __LINE__);
            return;
        }
    }
    return;
}

static void at_gateway_print_send_result(unsigned char cmd_type, int send_result)
{
    const char *res_str[3] = {"SEND SUC", "SEND FAIL", "SEND_TIMEOUT"};

    switch (cmd_type)
    {
    case AUTHENTICATION_REQ:
        at_server_printfln("+GATEWAYAUTH:%s", res_str[send_result]);
        break;

    case IOTE_STATE_UPDATE:
        at_server_printfln("+GATEWAYHEART:%s", res_str[send_result]);
        break;

    case VERSION_VERIFY:
        at_server_printfln("+GATEWAYOTAREQ:%s", res_str[send_result]);
        break;

    case IOTE_MISSING_DATA_REQ:
        at_server_printfln("+GATEWAYMISSREQ:%s", res_str[send_result]);
        break;

    default:
        break;
    }
}

static at_result_t at_gateway_send_ps_cmd_data(unsigned char *data, app_ps_header_t *ps_header)
{
    unsigned char *cmd_coding = RT_NULL;
    unsigned int cmd_coding_len = 0;
    unsigned char *data_coding = RT_NULL;
    unsigned int data_coding_len = 0;
    int send_result = 0;

    if (app_cmd_coding(ps_header->cmd_type, data, &cmd_coding, &cmd_coding_len) < 0)
    {
        rt_kprintf("%s line %d app_cmd_coding error\n", __FUNCTION__, __LINE__);
        return AT_RESULT_FAILE;
    }

    if (0 != app_data_coding(ps_header, cmd_coding, cmd_coding_len, &data_coding, &data_coding_len))
    {
        rt_kprintf("%s line %d app_data_coding error\n", __FUNCTION__, __LINE__);
        rt_free(cmd_coding);
        return AT_RESULT_FAILE;
    }
    send_result = uc_wiota_send_data(data_coding, data_coding_len, 10000, RT_NULL);

    at_gateway_print_send_result(ps_header->cmd_type, send_result);

    rt_free(cmd_coding);
    rt_free(data_coding);

    return (send_result == UC_OP_SUCC ? AT_RESULT_OK : AT_RESULT_FAILE);
}

static at_result_t at_gateway_send_auth_info(void)
{
    app_ps_auth_req_t auth_req_data = {0};
    app_ps_header_t ps_header = {0};
    unsigned int src_addr = 0;
    unsigned char src_addr_len = 0;

    auth_req_data.auth_type = 0;
    rt_strncpy(auth_req_data.aut_code, gateway_mode.auth_code, rt_strlen(gateway_mode.auth_code));

    app_set_header_property(PRO_SRC_ADDR, 1, &ps_header.property);
    uc_wiota_get_userid(&src_addr, &src_addr_len);
    at_gateway_set_dev_id(src_addr);

    ps_header.addr.src_addr = at_gateway_get_dev_id();
    ps_header.cmd_type = AUTHENTICATION_REQ;
    ps_header.packet_num = app_packet_num();

    return at_gateway_send_ps_cmd_data((unsigned char *)&auth_req_data, &ps_header);
}

static void at_gateway_handle_state_update_info_msg(void)
{
    app_ps_iote_state_update_t state_update_data = {0};
    app_ps_header_t ps_header = {0};
    radio_info_t radio = {0};
    char device_type[16] = {"iote"};

    uc_wiota_get_radio_info(&radio);

    rt_strncpy(state_update_data.device_type, device_type, rt_strlen(device_type));
    state_update_data.rssi = radio.rssi;
    state_update_data.temperature = at_temp_query();
    rt_kprintf("device_type %s, rssi %d, temp %d\n", state_update_data.device_type, state_update_data.rssi, state_update_data.temperature);

    app_set_header_property(PRO_SRC_ADDR, 1, &ps_header.property);
    ps_header.cmd_type = IOTE_STATE_UPDATE;
    ps_header.addr.src_addr = at_gateway_get_dev_id();
    ps_header.packet_num = app_packet_num();

    at_gateway_send_ps_cmd_data((unsigned char *)&state_update_data, &ps_header);
}

static void at_gateway_handle_ota_miss_data_req_msg(void)
{
    app_ps_header_t ps_header = {0};
    app_ps_iote_missing_data_req_t miss_data_req = {0};

    for (int offset = 0; offset < gateway_mode.block_count; offset++)
    {
        if (0x0 == JUDGMENT_BIT(gateway_mode.mask_map[offset / 8], offset % 8))
        {
            miss_data_req.miss_data_offset[miss_data_req.miss_data_num] = offset * GATEWAY_OTA_BLOCK_SIZE;
            miss_data_req.miss_data_length[miss_data_req.miss_data_num] = GATEWAY_OTA_BLOCK_SIZE;
            rt_kprintf("miss_data_offset[%d] %d, miss_data_length[%d] %d\n",
                       miss_data_req.miss_data_num, miss_data_req.miss_data_offset[miss_data_req.miss_data_num],
                       miss_data_req.miss_data_num, miss_data_req.miss_data_length[miss_data_req.miss_data_num]);
            miss_data_req.miss_data_num++;
            if (offset == APP_MAX_MISSING_DATA_BLOCK_NUM - 1)
            {
                break;
            }
        }
    }

    miss_data_req.upgrade_type = gateway_mode.upgrade_type;
    rt_strncpy(miss_data_req.device_type, gateway_mode.device_type, rt_strlen(gateway_mode.device_type));
    rt_strncpy(miss_data_req.new_version, gateway_mode.new_version, rt_strlen(gateway_mode.new_version));
    rt_strncpy(miss_data_req.old_version, GATEWAY_SOFT_VERSION, rt_strlen(GATEWAY_SOFT_VERSION));

    app_set_header_property(PRO_SRC_ADDR, 1, &ps_header.property);
    ps_header.addr.src_addr = at_gateway_get_dev_id();
    ps_header.cmd_type = IOTE_MISSING_DATA_REQ;
    ps_header.packet_num = app_packet_num();

    at_gateway_send_ps_cmd_data((unsigned char *)&miss_data_req, &ps_header);
}

static void at_gateway_send_msg_to_queue(int msg_code)
{
    static at_gateway_msg_t recv_data = {0};

    recv_data.msg_code = msg_code;

    if (RT_EOK != at_gateway_send_queue(gateway_mode.gateway_mq, &recv_data, 10000))
    {
        rt_kprintf("%s line %d gateway send queue fail\n", __FUNCTION__, __LINE__);
        return;
    }
}

static void at_gateway_handle_heart_timer_msg(void *para)
{
    at_gateway_send_msg_to_queue(GATEWAY_MSG_CODE_UL_HEART);
}

static void at_gateway_send_miss_data_req_to_queue(void)
{
    if (!gateway_mode.miss_data_req)
    {
        at_gateway_send_msg_to_queue(GATEWAY_MSG_CODE_UL_MISS_DATA_REQ);
        gateway_mode.miss_data_req = TRUE;
    }
}

static void at_gateway_handle_ota_recv_timer_msg(void *para)
{
    at_gateway_send_miss_data_req_to_queue();
}

static void at_gateway_handle_ota_req_timer_msg(void *para)
{
    at_gateway_send_msg_to_queue(GATEWAY_MSG_CODE_OTA_REQ);
}

static void at_gateway_handle_auth_res_msg(unsigned char *data, unsigned int data_len)
{
    unsigned char *cmd_decoding = RT_NULL;
    app_ps_auth_res_t *auth_res_data = RT_NULL;

    if (app_cmd_decoding(AUTHENTICATION_RES, data, data_len, &cmd_decoding) < 0)
    {
        rt_kprintf("%s line %d app_cmd_decoding error\n", __FUNCTION__, __LINE__);
        return;
    }
    auth_res_data = (app_ps_auth_res_t *)cmd_decoding;

    if (auth_res_data->state == AUTHENTICATION_INFO_CHANGE &&
        at_gateway_get_auth_state() != AUTHENTICATION_SUC)
    {
        rt_free(cmd_decoding);
        return;
    }

    if (auth_res_data->state != AUTHENTICATION_SUC)
    {
        rt_timer_stop(gateway_mode.ver_timer);
        rt_timer_stop(gateway_mode.heart_timer);
        if (gateway_mode.ota_state == GATEWAY_OTA_DOWNLOAD)
        {
            rt_free(gateway_mode.mask_map);
            gateway_mode.mask_map = RT_NULL;
            rt_timer_stop(gateway_mode.ota_timer);
            gateway_mode.ota_state = GATEWAY_OTA_STOP;
        }
    }

    switch (auth_res_data->state)
    {
    case AUTHENTICATION_SUC:
        at_gateway_set_wiota_id(auth_res_data->wiota_id);
        uc_wiota_set_freq_list(auth_res_data->freq_list, APP_MAX_FREQ_LIST_NUM);
        uc_wiota_save_static_info();
        at_server_printfln("+GATEWAYAUTH:SUC");
        if (uc_wiota_get_auto_connect_flag())
        {
            at_gateway_set_reboot(TRUE);
            rt_sem_take(gateway_mode.gateway_sem, RT_WAITING_FOREVER);
        }
        else
        {
            uc_wiota_exit();
            uc_wiota_init();
            uc_wiota_set_wiotaid(auth_res_data->wiota_id);
            uc_wiota_run();
            uc_wiota_connect();
        }
        // at_gateway_handle_send_ota_req_msg();
        rt_timer_start(gateway_mode.ver_timer);
        break;

    case AUTHENTICATION_NO_DATA:
        at_server_printfln("+GATEWAYAUTH:NO DATA");
        break;

    case AUTHENTICATION_FAIL:
        at_server_printfln("+GATEWAYAUTH:FAIL");
        break;

    case AUTHENTICATION_INFO_CHANGE:
        at_gateway_set_wiota_id(auth_res_data->wiota_id);
        uc_wiota_set_freq_list(auth_res_data->freq_list, APP_MAX_FREQ_LIST_NUM);
        uc_wiota_save_static_info();
        at_server_printfln("+GATEWAYAUTH:INFO CHANGE");
        break;

    default:
        rt_kprintf("error auth state %d\n", auth_res_data->state);
        break;
    }
    at_gateway_set_auth_state(auth_res_data->state);

    rt_free(cmd_decoding);
}

static boolean at_gateway_check_if_upgrade_required(app_ps_ota_upgrade_req_t *ota_upgrade_req)
{
    boolean is_upgrade_range = FALSE;
    boolean is_required = FALSE;
    unsigned int dev_id = at_gateway_get_dev_id();

    if (ota_upgrade_req->upgrade_range == 1)
    {
        for (int i = 0; i < APP_MAX_IOTE_UPGRADE_NUM; i++)
        {
            if (dev_id == ota_upgrade_req->iote_list[i])
            {
                if (gateway_mode.ota_state == GATEWAY_OTA_STOP)
                {
                    gateway_mode.ota_state = GATEWAY_OTA_DEFAULT;
                }

                is_upgrade_range = TRUE;
                break;
            }
        }
    }
    else if (ota_upgrade_req->upgrade_range == 0 && gateway_mode.ota_state != GATEWAY_OTA_STOP)
    {
        is_upgrade_range = TRUE;
    }

    if (is_upgrade_range)
    {
        if (0 == rt_strncmp(GATEWAY_SOFT_VERSION, ota_upgrade_req->old_version, rt_strlen(ota_upgrade_req->old_version)) &&
            0 == rt_strncmp(gateway_mode.device_type, ota_upgrade_req->device_type, rt_strlen(ota_upgrade_req->device_type)))
        {
            is_required = TRUE;
        }
    }

    return is_required;
}

static boolean at_gateway_whether_the_ota_upgrade_data_is_recved(void)
{
    unsigned int offset = 0;
    unsigned int block_count = 0;

    for (; offset < gateway_mode.block_count; offset++)
    {
        if (0x1 == JUDGMENT_BIT(gateway_mode.mask_map[offset / 8], offset % 8))
        {
            block_count++;
        }
    }

    at_server_printfln("GATEWAY_OTA_DOWNLOAD %d/%d", block_count, gateway_mode.block_count);

    if (block_count >= gateway_mode.block_count)
    {
        rt_kprintf("ota data recv end\n");
        return TRUE;
    }

    return FALSE;
}

static void at_gateway_handle_ota_upgrade_res_msg(unsigned char *data, unsigned int data_len)
{
    unsigned char *cmd_decoding = RT_NULL;
    app_ps_ota_upgrade_req_t *ota_upgrade_req = RT_NULL;

    if (app_cmd_decoding(OTA_UPGRADE_REQ, data, data_len, &cmd_decoding) < 0)
    {
        rt_kprintf("%s line %d app_cmd_decoding error\n", __FUNCTION__, __LINE__);
        return;
    }
    ota_upgrade_req = (app_ps_ota_upgrade_req_t *)cmd_decoding;

    if (at_gateway_check_if_upgrade_required(ota_upgrade_req))
    {
        int file_size = ota_upgrade_req->file_size;

        if (gateway_mode.ota_state == GATEWAY_OTA_DEFAULT)
        {
            unsigned int mask_map_size = file_size / GATEWAY_OTA_BLOCK_SIZE / 8 + 1;

            gateway_mode.mask_map = rt_malloc(mask_map_size);
            RT_ASSERT(gateway_mode.mask_map);
            rt_memset(gateway_mode.mask_map, 0x00, mask_map_size);

            gateway_mode.block_size = GATEWAY_OTA_BLOCK_SIZE;
            gateway_mode.block_count = file_size / GATEWAY_OTA_BLOCK_SIZE;
            if (file_size % GATEWAY_OTA_BLOCK_SIZE)
            {
                gateway_mode.block_count++;
            }

            uc_wiota_ota_flash_erase(GATEWAY_MODE_OTA_START_ADDR, file_size);
            gateway_mode.ota_state = GATEWAY_OTA_DOWNLOAD;
            gateway_mode.upgrade_type = ota_upgrade_req->upgrade_type;
            rt_strncpy(gateway_mode.new_version, ota_upgrade_req->new_version, rt_strlen(ota_upgrade_req->new_version));

            rt_timer_control(gateway_mode.ota_timer, RT_TIMER_CTRL_SET_TIME, (void *)&ota_upgrade_req->upgrade_time);
            rt_timer_start(gateway_mode.ota_timer);

            rt_kprintf("GATEWAY_OTA_DEFAULT file_size %d, mask_map_size %d, block_size %d, block_count %d, ota_state %d, upgrade_type %d, upgrade_time %d\n",
                       file_size, mask_map_size, gateway_mode.block_size, gateway_mode.block_count, gateway_mode.ota_state, gateway_mode.upgrade_type, ota_upgrade_req->upgrade_time);
            rt_kprintf("new_version %s, old_version %s, device_type %s\n", gateway_mode.new_version, GATEWAY_SOFT_VERSION, gateway_mode.device_type);
        }

        if (gateway_mode.ota_state == GATEWAY_OTA_DOWNLOAD)
        {
            unsigned int offset = ota_upgrade_req->data_offset / GATEWAY_OTA_BLOCK_SIZE;

            if (gateway_mode.miss_data_req)
            {
                int timeout = ota_upgrade_req->upgrade_time / gateway_mode.block_count * gateway_mode.miss_data_num + 5000;

                rt_timer_control(gateway_mode.ota_timer, RT_TIMER_CTRL_SET_TIME, (void *)&timeout);
                rt_timer_start(gateway_mode.ota_timer);
                gateway_mode.miss_data_req = FALSE;
                rt_kprintf("miss_data_req recv begin, upgrade_time %d\n", timeout);
            }

            if (0x0 == JUDGMENT_BIT(gateway_mode.mask_map[offset / 8], offset % 8))
            {
                uc_wiota_ota_flash_write(ota_upgrade_req->data, GATEWAY_MODE_OTA_START_ADDR + ota_upgrade_req->data_offset, ota_upgrade_req->data_length);
                SET_BIT(gateway_mode.mask_map[offset / 8], offset % 8);
                rt_kprintf("GATEWAY_OTA_DOWNLOAD offset %d mask_map[%d] = 0x%x\n", offset, offset / 8, gateway_mode.mask_map[offset / 8]);
            }

            if (at_gateway_whether_the_ota_upgrade_data_is_recved())
            {
                if (0 == uc_wiota_ota_check_flash_data(GATEWAY_MODE_OTA_START_ADDR, file_size, ota_upgrade_req->md5))
                {
                    rt_kprintf("ota data checkout ok, jump to program\n");

                    gateway_mode.ota_state = GATEWAY_OTA_PROGRAM;
                    rt_free(gateway_mode.mask_map);
                    gateway_mode.mask_map = RT_NULL;
                    rt_timer_stop(gateway_mode.ota_timer);

                    set_partition_size(GATEWAY_OTA_FLASH_BIN_SIZE, GATEWAY_OTA_FLASH_REVERSE_SIZE, GATEWAY_OTA_FLASH_OTA_SIZE);
                    uc_wiota_ota_jump_program(file_size, ota_upgrade_req->upgrade_type);
                }
                else
                {
                    rt_kprintf("ota data checkout error, upgrade fail\n");

                    gateway_mode.ota_state = GATEWAY_OTA_DEFAULT;
                    rt_free(gateway_mode.mask_map);
                    gateway_mode.mask_map = RT_NULL;
                    rt_timer_stop(gateway_mode.ota_timer);
                }
            }
        }
    }

    rt_free(cmd_decoding);
}

static void at_gateway_handle_ota_upgrade_stop_msg(unsigned char *data, unsigned int data_len)
{
    unsigned char *cmd_decoding = RT_NULL;
    app_ps_ota_upgrade_stop_t *ota_upgrade_stop = RT_NULL;
    unsigned int dev_id = at_gateway_get_dev_id();

    if (app_cmd_decoding(OTA_UPGRADE_STOP, data, data_len, &cmd_decoding) < 0)
    {
        rt_kprintf("%s line %d app_cmd_decoding error\n", __FUNCTION__, __LINE__);
        return;
    }
    ota_upgrade_stop = (app_ps_ota_upgrade_stop_t *)cmd_decoding;

    for (int i = 0; i < APP_MAX_IOTE_UPGRADE_STOP_NUM; i++)
    {
        rt_kprintf("iote_list 0x%x\n", ota_upgrade_stop->iote_list[i]);
        if (dev_id == ota_upgrade_stop->iote_list[i])
        {
            if (gateway_mode.ota_state == GATEWAY_OTA_DOWNLOAD)
            {
                rt_free(gateway_mode.mask_map);
                gateway_mode.mask_map = RT_NULL;
                rt_timer_stop(gateway_mode.ota_timer);
            }
            gateway_mode.ota_state = GATEWAY_OTA_STOP;
            rt_kprintf("0x%x stop ota upgrade\n", dev_id);
            break;
        }
    }

    rt_free(cmd_decoding);
}

static void at_gateway_handle_ota_upgrade_state_msg(unsigned char *data, unsigned int data_len)
{
    unsigned char *cmd_decoding = RT_NULL;
    app_ps_ota_upgrade_state_t *ota_upgrade_state = RT_NULL;

    if (app_cmd_decoding(OTA_UPGRADE_STATE, data, data_len, &cmd_decoding) < 0)
    {
        rt_kprintf("%s line %d app_cmd_decoding error\n", __FUNCTION__, __LINE__);
        return;
    }
    ota_upgrade_state = (app_ps_ota_upgrade_state_t *)cmd_decoding;

    if (0 == rt_strncmp(ota_upgrade_state->old_version, GATEWAY_SOFT_VERSION, rt_strlen(GATEWAY_SOFT_VERSION)) &&
        0 == rt_strncmp(ota_upgrade_state->new_version, gateway_mode.new_version, rt_strlen(gateway_mode.new_version)) &&
        0 == rt_strncmp(ota_upgrade_state->device_type, gateway_mode.device_type, rt_strlen(gateway_mode.device_type)) &&
        ota_upgrade_state->upgrade_type == gateway_mode.upgrade_type)
    {
        if (ota_upgrade_state->process_state == GATEWAY_OTA_END && gateway_mode.ota_state == GATEWAY_OTA_DOWNLOAD)
        {
            rt_kprintf("recv GATEWAY_OTA_END cmd, checkout mask_map\n");
            rt_timer_stop(gateway_mode.ota_timer);
            for (int offset = 0; offset < gateway_mode.block_count; offset++)
            {
                if (0x0 == JUDGMENT_BIT(gateway_mode.mask_map[offset / 8], offset % 8))
                {
                    gateway_mode.miss_data_num++;
                }
            }

            if (gateway_mode.miss_data_num > 0)
            {
                rt_kprintf("there are %d packets recvived not completely, send miss data req\n", gateway_mode.miss_data_num);
                at_gateway_send_miss_data_req_to_queue();
            }
        }
    }

    rt_free(cmd_decoding);
}

static void at_gateway_handle_recv_dl_msg(unsigned char *data, unsigned int data_len, int response_flag)
{
    if (response_flag)
    {
        unsigned char *cmd_decoding = RT_NULL;
        int *send_result = RT_NULL;

        if (app_cmd_decoding(IOTE_RESPON_STATE, data, data_len, &cmd_decoding) < 0)
        {
            rt_kprintf("%s line %d app_cmd_decoding error\n", __FUNCTION__, __LINE__);
            return;
        }

        send_result = (int *)cmd_decoding;

        gateway_mode.send_result = *send_result;
        at_gateway_release_sem();

        rt_free(cmd_decoding);
    }
    else
    {
        at_server_printf("+GATEWAYRECV,%d:", data_len);
        at_send_data(data, data_len);
        at_server_printf("\r\n");
    }
}

static void at_gateway_handle_recv_msg(unsigned char *data, unsigned int data_len)
{
    app_ps_header_t ps_header = {0};
    unsigned char *data_decoding = RT_NULL;
    unsigned int data_decoding_len = 0;

    if (0 != app_data_decoding(data, data_len, &data_decoding, &data_decoding_len, &ps_header))
    {
        rt_kprintf("%s line %d app_data_decoding error\n", __FUNCTION__, __LINE__);
        return;
    }
    rt_kprintf("recv gateway cmd %d\n", ps_header.cmd_type);

    switch (ps_header.cmd_type)
    {
    case AUTHENTICATION_RES:
        at_gateway_handle_auth_res_msg(data_decoding, data_decoding_len);
        break;

    case OTA_UPGRADE_REQ:
        at_gateway_handle_ota_upgrade_res_msg(data_decoding, data_decoding_len);
        break;

    case OTA_UPGRADE_STOP:
        at_gateway_handle_ota_upgrade_stop_msg(data_decoding, data_decoding_len);
        break;

    case OTA_UPGRADE_STATE:
        at_gateway_handle_ota_upgrade_state_msg(data_decoding, data_decoding_len);
        break;

    case IOTE_USER_DATA:
        at_gateway_handle_recv_dl_msg(data_decoding, data_decoding_len, ps_header.property.response_flag);
        break;

    default:
        rt_kprintf("cmd_type %d\n", ps_header.cmd_type);
        break;
    }

    rt_free(data_decoding);
}

static void at_gateway_handle_msg(void *para)
{
    at_gateway_msg_t *recv_data = RT_NULL;

    while (1)
    {
        if (RT_EOK != at_gateway_recv_queue(gateway_mode.gateway_mq, (void **)&recv_data, RT_WAITING_FOREVER))
        {
            continue;
        }

        rt_kprintf("recv msg_code %d\n", recv_data->msg_code);
        switch (recv_data->msg_code)
        {
        case GATEWAY_MSG_CODE_DL_RECV:
            at_gateway_handle_recv_msg(recv_data->data, recv_data->data_len);

            rt_free(recv_data->data);
            recv_data->data = RT_NULL;
            rt_free(recv_data);
            recv_data = RT_NULL;

            break;

        case GATEWAY_MSG_CODE_UL_HEART:
            at_gateway_handle_state_update_info_msg();
            break;

        case GATEWAY_MSG_CODE_OTA_REQ:
            at_gateway_handle_send_ota_req_msg();
            break;

        case GATEWAY_MSG_CODE_UL_MISS_DATA_REQ:
            at_gateway_handle_ota_miss_data_req_msg();
            break;

        default:
            break;
        }
    }
}

static void at_gateway_mode_init(void)
{
    rt_memset(&gateway_mode, 0, sizeof(at_gateway_mode_t));
    gateway_mode.auth_state = AUTHENTICATION_FAIL;
    gateway_mode.ota_state = GATEWAY_OTA_DEFAULT;
    rt_strncpy(gateway_mode.auth_code, "123456", 6);
    rt_strncpy(gateway_mode.device_type, "iote", 4);
}

static void at_gateway_mode_deinit(void)
{
    if (RT_NULL != gateway_mode.gateway_mq)
        at_gateway_dele_queue(gateway_mode.gateway_mq);

    if (RT_NULL != gateway_mode.heart_timer)
        rt_timer_delete(gateway_mode.heart_timer);

    if (RT_NULL != gateway_mode.ota_timer)
        rt_timer_delete(gateway_mode.ota_timer);

    if (RT_NULL != gateway_mode.ver_timer)
        rt_timer_delete(gateway_mode.ver_timer);

    if (RT_NULL != gateway_mode.gateway_sem)
        rt_sem_delete(gateway_mode.gateway_sem);

    if (RT_NULL != gateway_mode.gateway_handler)
        rt_thread_delete(gateway_mode.gateway_handler);

    if (RT_NULL != gateway_mode.mask_map)
    {
        rt_free(gateway_mode.mask_map);
        gateway_mode.mask_map = RT_NULL;
    }
}

static at_result_t at_single_gateway_mode_setup(const char *args)
{
    int mode = 0;

    args = parse((char *)(++args), "d", &mode);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (wiota_state != AT_WIOTA_RUN)
        return AT_RESULT_FAILE;

    if (mode == 1)
    {
        if (at_gateway_get_mode())
        {
            return AT_RESULT_REPETITIVE_FAILE;
        }

        at_gateway_mode_init();

        do
        {
            gateway_mode.gateway_mq = at_gateway_create_queue("gw_mq", 4, RT_IPC_FLAG_PRIO);
            if (RT_NULL == gateway_mode.gateway_mq)
            {
                rt_kprintf("%s line %d create mq fail\n", __FUNCTION__, __LINE__);
                return AT_RESULT_FAILE;
            }

            gateway_mode.heart_timer = rt_timer_create("t_heart",
                                                       at_gateway_handle_heart_timer_msg,
                                                       RT_NULL,
                                                       10000,
                                                       RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
            if (RT_NULL == gateway_mode.heart_timer)
            {
                rt_kprintf("%s line %d create timer fail\n", __FUNCTION__, __LINE__);
                break;
            }

            gateway_mode.ota_timer = rt_timer_create("t_ota",
                                                     at_gateway_handle_ota_recv_timer_msg,
                                                     RT_NULL,
                                                     10000,
                                                     RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
            if (RT_NULL == gateway_mode.ota_timer)
            {
                rt_kprintf("%s line %d create timer fail\n", __FUNCTION__, __LINE__);
                break;
            }

            gateway_mode.ver_timer = rt_timer_create("t_ver",
                                                     at_gateway_handle_ota_req_timer_msg,
                                                     RT_NULL,
                                                     GATEWAY_OTA_VER_PERIOD,
                                                     RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
            if (RT_NULL == gateway_mode.ver_timer)
            {
                rt_kprintf("%s line %d create timer fail\n", __FUNCTION__, __LINE__);
                break;
            }

            gateway_mode.gateway_sem = rt_sem_create("gw_sem", 0, RT_IPC_FLAG_PRIO);
            if (RT_NULL == gateway_mode.gateway_sem)
            {
                rt_kprintf("%s line %d create sem fail\n", __FUNCTION__, __LINE__);
                break;
            }

            gateway_mode.gateway_handler = rt_thread_create("gateway",
                                                            at_gateway_handle_msg,
                                                            RT_NULL,
                                                            1024,
                                                            5,
                                                            3);
            if (RT_NULL == gateway_mode.gateway_handler)
            {
                rt_kprintf("%s line %d create thread fail\n", __FUNCTION__, __LINE__);
                break;
            }
            rt_thread_startup(gateway_mode.gateway_handler);

            at_gateway_set_mode(TRUE);
            at_server_printfln("+GATEWAYMODE:RUN");
            at_server_printfln("+GATEWAYVERSION:%s", GATEWAY_SOFT_VERSION);
            return AT_RESULT_OK;
        } while (0);

        at_gateway_mode_deinit();
        return AT_RESULT_FAILE;
    }
    else if (mode == 0)
    {
        if (at_gateway_get_mode())
        {
            at_gateway_mode_deinit();
            at_gateway_mode_init();
            at_server_printfln("+GATEWAYMODE:EXIT");
            return AT_RESULT_OK;
        }
        else
        {
            at_server_printfln("+GATEWAYMODE:ERROE");
            return AT_RESULT_FAILE;
        }
    }
    else
    {
        at_server_printfln("+GATEWAYMODE:ERROE");
        return AT_RESULT_FAILE;
    }
}

static at_result_t at_single_gateway_auth_setup(const char *args)
{
    char auth_code[AUTH_CODE_LEN] = {0};

    args = parse((char *)(++args), "s", AUTH_CODE_LEN, auth_code);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (wiota_state != AT_WIOTA_RUN)
        return AT_RESULT_FAILE;

    if (!at_gateway_get_mode())
    {
        at_server_printfln("+GATEWAYAUTH:ERROR");
        return AT_RESULT_FAILE;
    }

    // if (at_gateway_get_auth_state() == AUTHENTICATION_SUC)
    // {
    //     at_server_printfln("+GATEWAYAUTH:REPEAT AUTH");
    //     return AT_RESULT_REPETITIVE_FAILE;
    // }

    rt_strncpy(gateway_mode.auth_code, auth_code, rt_strlen(auth_code) - 2); // del \r\n

    return at_gateway_send_auth_info();
}

static at_result_t at_single_gateway_heart_setup(const char *args)
{
    int heart_state = 0;
    int timeout = 0;

    args = parse((char *)(++args), "d,d", &heart_state, &timeout);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (wiota_state != AT_WIOTA_RUN)
        return AT_RESULT_FAILE;

    if (!at_gateway_get_mode() || at_gateway_get_auth_state() != AUTHENTICATION_SUC)
    {
        rt_kprintf("unable to set heart if authentication fails\n");
        return AT_RESULT_FAILE;
    }

    if (heart_state == 1)
    {
        if (!at_gateway_get_heart_state())
        {
            rt_timer_control(gateway_mode.heart_timer, RT_TIMER_CTRL_SET_TIME, &timeout);
            rt_timer_start(gateway_mode.heart_timer);
        }
        else if (at_gateway_get_heart_state())
        {
            if (timeout != at_gateway_get_heart_timeout())
            {
                rt_timer_control(gateway_mode.heart_timer, RT_TIMER_CTRL_SET_TIME, &timeout);
            }
        }
    }
    else if (heart_state == 0)
    {
        if (at_gateway_get_heart_state())
        {
            rt_timer_stop(gateway_mode.heart_timer);
        }
    }
    else
    {
        rt_kprintf("error heart_state\n");
        return AT_RESULT_FAILE;
    }

    at_gateway_set_heart(heart_state, timeout);
    at_server_printfln("+GATEWAYHEART:%d,%d", heart_state, timeout);

    return AT_RESULT_OK;
}

at_result_t at_gateway_handle_send_ota_req_msg(void)
{
    app_ps_version_verify_t version_verify = {0};
    app_ps_header_t ps_header = {0};

    rt_strncpy(version_verify.software_version, GATEWAY_SOFT_VERSION, rt_strlen(GATEWAY_SOFT_VERSION));
    uc_wiota_get_hardware_ver((unsigned char *)version_verify.hardware_version);
    rt_strncpy(version_verify.device_type, gateway_mode.device_type, rt_strlen(gateway_mode.device_type));

    app_set_header_property(PRO_SRC_ADDR, 1, &ps_header.property);
    ps_header.addr.src_addr = at_gateway_get_dev_id();
    ps_header.cmd_type = VERSION_VERIFY;
    ps_header.packet_num = app_packet_num();

    return at_gateway_send_ps_cmd_data((unsigned char *)&version_verify, &ps_header);
}

static at_result_t at_single_gateway_ota_req_setup(const char *args)
{
    char device_type[GATEWAY_DEV_TYPE_LEN] = {0};

    args = parse((char *)(++args), "s", GATEWAY_DEV_TYPE_LEN, device_type);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (wiota_state != AT_WIOTA_RUN)
        return AT_RESULT_FAILE;

    if (!at_gateway_whether_data_can_be_coding())
        return AT_RESULT_FAILE;

    rt_strncpy(gateway_mode.device_type, device_type, rt_strlen(device_type) - 2); // del \r\n

    return at_gateway_handle_send_ota_req_msg();
}
#endif // GATEWAY_MODE_SUPPORT

AT_CMD_EXPORT("AT+WIOTAVERSION", RT_NULL, RT_NULL, at_wiota_version_query, RT_NULL, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAINIT", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_wiota_init_exec);
AT_CMD_EXPORT("AT+WIOTALPM", "=<mode>,<state>", RT_NULL, RT_NULL, at_wiotalpm_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARATE", "=<rate_mode>,<rate_value>", RT_NULL, RT_NULL, at_wiotarate_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAPOW", "=<mode>,<power>", RT_NULL, RT_NULL, at_wiotapow_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASCANFREQ", "=<timeout>,<dataLen>,<freqnum>", RT_NULL, RT_NULL, at_scan_freq_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAFREQ", "=<freqpint>", RT_NULL, at_freq_query, at_freq_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTADCXO", "=<dcxo>", RT_NULL, RT_NULL, at_dcxo_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAUSERID", "=<id0>", RT_NULL, at_userid_query, at_userid_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARADIO", "=<temp>,<rssi>,<ber>,<snr>,<cur_pow>,<max_pow>,<cur_mcs>,<frac>", RT_NULL, at_radio_query, RT_NULL, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACONFIG", "=<id_len>,<symbol>,<dlul>,<bt>,<group_num>,<ap_max_pow>,<spec_idx>,<systemid>,<subsystemid>",
              RT_NULL, at_system_config_query, at_system_config_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARUN", "=<state>", RT_NULL, RT_NULL, at_wiota_cfun_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACONNECT", "=<state>,<activetime>", RT_NULL, at_connect_query, at_connect_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASEND", "=<timeout>,<len>", RT_NULL, RT_NULL, at_wiotasend_setup, at_wiotasend_exec);
AT_CMD_EXPORT("AT+WIOTATRANS", "=<timeout>,<end>", RT_NULL, RT_NULL, at_wiotatrans_setup, at_wiotatrans_exec);
AT_CMD_EXPORT("AT+WIOTADTUSEND", "=<timeout>,<wait>,<end>", RT_NULL, RT_NULL, at_wiota_dtu_send_setup, at_wiota_dtu_send_exec);
AT_CMD_EXPORT("AT+WIOTARECV", "=<timeout>", RT_NULL, RT_NULL, at_wiotarecv_setup, at_wiota_recv_exec);
AT_CMD_EXPORT("AT+WIOTALOG", "=<mode>", RT_NULL, RT_NULL, at_wiotalog_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASTATS", "=<mode>,<type>", RT_NULL, at_wiotastats_query, at_wiotastats_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACRC", "=<crc_limit>", RT_NULL, at_wiotacrc_query, at_wiotacrc_setup, RT_NULL);
AT_CMD_EXPORT("AT+THROUGHTSTART", "=<mode>,<time>", RT_NULL, RT_NULL, at_test_mode_start_setup, RT_NULL);
AT_CMD_EXPORT("AT+THROUGHTSTOP", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_test_mode_stop_exec);
AT_CMD_EXPORT("AT+WIOTAOSC", "=<mode>", RT_NULL, at_wiotaosc_query, at_wiotaosc_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTALIGHT", "=<mode>", RT_NULL, RT_NULL, at_wiotalight_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASAVESTATIC", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_wiota_save_static_exec);

#ifdef GATEWAY_MODE_SUPPORT
AT_CMD_EXPORT("AT+GATEWAYMODE", "=<mode>", RT_NULL, RT_NULL, at_single_gateway_mode_setup, RT_NULL);
AT_CMD_EXPORT("AT+GATEWAYAUTH", "=<auth_code>", RT_NULL, RT_NULL, at_single_gateway_auth_setup, RT_NULL);
AT_CMD_EXPORT("AT+GATEWAYHEART", "=<heart_state>,<timeout>", RT_NULL, RT_NULL, at_single_gateway_heart_setup, RT_NULL);
AT_CMD_EXPORT("AT+GATEWAYOTAREQ", "=<device_type>", RT_NULL, RT_NULL, at_single_gateway_ota_req_setup, RT_NULL);
#endif // GATEWAY_MODE_SUPPORT

#endif //UC8288_MODULE
#endif // WIOTA_APP_DEMO
#endif // RT_USING_AT