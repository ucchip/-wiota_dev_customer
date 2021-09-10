/*
 * Copyright (c) 2006-2020, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date                 Author                Notes
 * 20201-8-17     ucchip-wz          v0.00
 */
//#ifdef AT_USING_SERVER
#ifdef UC8288_MODULE

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "uc_wiota_api.h"
#include "at.h"
#include "ati_prs.h"
//#include "uc_string_lib.h"

enum at_wiota_state
{
    AT_WIOTA_DEFAULT = 0,
    AT_WIOTA_INIT ,                 
    AT_WIOTA_RUN,
    AT_WIOTA_EXIT,
};

enum at_test_type
{
    AT_TEST_PRINTF = 0,
    AT_TEST_CMD,
};

#define WIOTA_SEND_TIMEOUT 60000
#define WIOTA_WAIT_DATA_TIMEOUT 10000

#define WIOTA_MUST_INIT(state) \
if(state != AT_WIOTA_INIT) \
{ \
     return AT_RESULT_REPETITIVE_FAILE; \
}

extern at_server_t at_get_server(void);
extern void set_rt_kprintf_switch(unsigned char sw);
extern char *parse (char *b, char *f, ...);

static int wiota_state = AT_WIOTA_DEFAULT;

static at_result_t at_freq_query(void)
{
    at_server_printfln("+WIOTAFREQ=%d", uc_wiota_get_freq_info());

    return AT_RESULT_OK;
}


static at_result_t at_freq_setup(const char* args)
{
    int freq = 0;

    WIOTA_MUST_INIT(wiota_state)

    args = parse ((char*)(++args),"d", &freq);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    
    uc_wiota_set_freq_info(freq);

    return AT_RESULT_OK;
}

static at_result_t at_dcxo_setup(const char* args)
{
    int dcxo = 0;
    
    WIOTA_MUST_INIT(wiota_state)

    args = parse ((char*)(++args),"y", &dcxo);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    rt_kprintf("dcxo=0x%x\n", dcxo);
    uc_wiota_set_dcxo(dcxo);

    return AT_RESULT_OK;
}


static at_result_t at_userid_query(void)
{
    unsigned int id[2] = {0};
    unsigned char len = 0;
    
    uc_wiota_get_userid(&(id[0]), &len);
    at_server_printfln("+WIOTAUSERID=0x%x,0x%x", id[0], id[1]);

    return AT_RESULT_OK;
}


static at_result_t at_userid_setup(const char* args)
{
    unsigned int userid[2] = {0};

    WIOTA_MUST_INIT(wiota_state)

    args = parse ((char*)(++args),"y,y", &userid[0], &userid[1]);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    
    uc_wiota_set_userid(userid, 4);

    return AT_RESULT_OK;
}


static at_result_t at_radio_query(void)
{
    radio_info_t radio;

    WIOTA_MUST_INIT(wiota_state)

    uc_wiota_get_radio_info(&radio);
    
    at_server_printfln("+WIOTARADIO=%d,%d", radio.snr, radio.power);

    return AT_RESULT_OK;
}


static at_result_t at_system_config_query(void)
{
    sub_system_config_t config;
    uc_wiota_get_system_config(&config);

    at_server_printfln("+WIOTASYSTEMCONFIG=%d,%d,%d,%d,%d,%d,%d", \
        config.id_len, \
        config.symbol_length, config.dlul_ratio, config.btvalue, \
        config.group_number, config.systemid, config.subsystemid);

    return AT_RESULT_OK;
}

static at_result_t at_system_config_setup(const char* args)
{
    sub_system_config_t config;

   WIOTA_MUST_INIT(wiota_state)

    args = parse ((char*)(++args),"d,d,d,d,d,d,d", \
    &config.id_len,  &config.symbol_length, &config.dlul_ratio,\
        &config.btvalue, &config.group_number, &config.systemid, \
        &config.subsystemid);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    
    
    config.pn_num = 1;

    rt_kprintf("len=%d,symbol_length=%d,dlul_ratio=%d,\
        btvalue=%d, group_number=%d,systemid=%d,\
        subsystemid=%d", \
        config.id_len, config.symbol_length, config.dlul_ratio, \
        config.btvalue, config.group_number, config.systemid, \
        config.subsystemid);

    uc_wiota_set_system_config(&config);

    return AT_RESULT_OK;
}


static at_result_t at_wiota_init_exec(void)
{
    if ( wiota_state == AT_WIOTA_DEFAULT || wiota_state == AT_WIOTA_EXIT)
   {
        uc_wiota_init();
        wiota_state = AT_WIOTA_INIT;
        return AT_RESULT_OK;
   }
    
    return AT_RESULT_REPETITIVE_FAILE;
}

static void wiota_recv_callback(uc_recv_back_p data)
{
    rt_kprintf("wiota_recv_callback result %d\n",  data->result);
    
    if (0 == data->result)
    {
        at_server_printf("+WIOTARECV,%d,%d,", data->type, data->data_len);
        at_send_data(data->data, data->data_len);
        
        rt_free(data->data);
    }
}

static at_result_t at_wiota_cfun_setup(const char* args)
{
    int  state = 0;

    args = parse ((char*)(++args),"d", &state);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    rt_kprintf("state = %d\n", state);

    if (1 == state && wiota_state == AT_WIOTA_INIT)
    {
        uc_wiota_run();
        uc_wiota_register_recv_data(wiota_recv_callback);
        wiota_state = AT_WIOTA_RUN;
    }
    else if ( 0 == state && wiota_state == AT_WIOTA_RUN)
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
    at_server_printfln("+WIOTACONNECT=%d,%d", uc_wiota_get_state(), uc_wiota_get_activetime());

    return AT_RESULT_OK;
}

static at_result_t at_connect_setup(const char* args)
{
    int  state = 0, timeout = 0;

    args = parse ((char*)(++args),"d,d", &state, &timeout);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    rt_kprintf("state = %d, timeout=%d\n", state, timeout);

    if (wiota_state != AT_WIOTA_RUN)
        return AT_RESULT_REPETITIVE_FAILE;
    
    rt_kprintf("state = %d, timeout=%d", state, timeout);
    
    if (timeout)
        uc_wiota_set_activetime((unsigned int)timeout);
    
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


static rt_err_t get_char_timeout(rt_tick_t timeout, char * chr)
{
    char ch;
    rt_err_t result;
    at_server_t at_server = at_get_server();
    while (rt_device_read(at_server->device, 0, &ch, 1) == 0)
    {
        rt_sem_control(at_server->rx_notice, RT_IPC_CMD_RESET, RT_NULL);
        if((result = rt_sem_take(at_server->rx_notice, timeout)) != RT_EOK)
        {
            return result;
        }
    }
    
    if(at_server->echo_mode)
    {
        at_server_printf("%c", ch);
    }
    
    *chr = ch;
    return RT_EOK;
}

#if 0
static at_result_t at_wiotasend_exec(void)
{
    uint8_t * sendbuffer = NULL;
    uint8_t * psendbuffer;
    rt_err_t result = RT_EOK;
    int length = 0;
    int buff_size = 1024;
    
    if(0 == wiota_state)
    {
        return AT_RESULT_FAILE;
    }

    sendbuffer = (uint8_t *)malloc(buff_size);
    if (sendbuffer == RT_NULL)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    at_server_printf("\r\n>");
    while(1)
    {
        psendbuffer = sendbuffer;
        length = buff_size;
        while(length)
        {
            result = get_char_timeout(rt_tick_from_millisecond(20), (char*)psendbuffer);
            if(result != RT_EOK)
            {
                break;
            }
            length--;
            psendbuffer++;
        }
        if(rt_strncmp("+++", sendbuffer, 3) == 0)
        {
            free(sendbuffer);
            sendbuffer = RT_NULL;
            break;
        }
        if((psendbuffer - sendbuffer) > 0)
        {
            if(UC_OP_SUCC != uc_wiota_send_data(sendbuffer, psendbuffer - sendbuffer, 60000, RT_NULL))
            {
                free(sendbuffer);
                sendbuffer = RT_NULL;
                return AT_RESULT_FAILE;
            }
        }
    }
    return AT_RESULT_NULL;
}
#endif

static at_result_t at_wiotasend_setup(const char *args)
{
    int length = 0, timeout = 0;
    unsigned char * sendbuffer = NULL;
    unsigned char * psendbuffer;
    
    if(0 == wiota_state)
    {
        return AT_RESULT_FAILE;
    }

    args = parse ((char*)(++args),"d,d",  &timeout, &length);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    
    rt_kprintf("state = %d, timeout=%d\n", timeout, length);

    if (wiota_state != AT_WIOTA_RUN)
        return AT_RESULT_REPETITIVE_FAILE;
    
    if(length > 0)
    {        
        sendbuffer = (unsigned char *)rt_malloc(length);
        if(sendbuffer == NULL)
        {
            at_server_printfln("SEND FAIL");
            return AT_RESULT_NULL;
        }
        psendbuffer = sendbuffer;
        at_server_printfln("OK");
        at_server_printf(">");
        while(length)
        {
            if(get_char_timeout(rt_tick_from_millisecond(WIOTA_WAIT_DATA_TIMEOUT), (char*)psendbuffer) != RT_EOK)
            {
                at_server_printfln("SEND FAIL");
                rt_free(sendbuffer);
                return AT_RESULT_NULL;
            }
            length--;
            psendbuffer++;
        }

        if(UC_OP_SUCC == uc_wiota_send_data(sendbuffer, psendbuffer - sendbuffer, timeout > 0 ? timeout: WIOTA_SEND_TIMEOUT, RT_NULL))
        {
            rt_free(sendbuffer);
            sendbuffer = NULL;
            
            at_server_printfln("SEND OK");
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


static at_result_t at_wiotarecv_setup(const char *args)
{
    unsigned short timeout = 0;
    uc_recv_back_t result;
    
    if(AT_WIOTA_DEFAULT == wiota_state)
    {
        return AT_RESULT_FAILE;
    }

   args = parse ((char*)(++args),"d", &timeout);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    
    if (timeout < 1)
        timeout = WIOTA_WAIT_DATA_TIMEOUT;
    
    rt_kprintf("timeout = %d\n", timeout);
    
    uc_wiota_recv_data(&result, timeout, RT_NULL);
    if (!result.result)
    {
        at_server_printf("+WIOTARECV,%d,%d,", result.type, result.data_len);

        at_send_data(result.data, result.data_len);
        
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
    
   if(AT_WIOTA_DEFAULT == wiota_state)
    {
        return AT_RESULT_FAILE;
    }
   
    uc_wiota_recv_data(&result, WIOTA_WAIT_DATA_TIMEOUT, RT_NULL);
    if (!result.result)
    {
        at_server_printf("+WIOTARECV,%d,%d,", result.type, result.data_len);
        at_send_data(result.data, result.data_len);
        rt_free(result.data);
        return AT_RESULT_OK;
    }
    else
    {
        return AT_RESULT_FAILE;
     }
}


static at_result_t at_test_setup(const char* args)
{
    int  type = 0, length = 0, send_bytes = 0;
    unsigned char * sendbuffer = RT_NULL;
    unsigned char * psendbuffer ;

    args = parse ((char*)(++args),"d,d",  &type, &length);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    
    rt_kprintf("type = %d, len = %d\n", type, length);

    if ( length > 0)
    {
        send_bytes = length;
        sendbuffer = (unsigned char *)rt_malloc(length);
        if(sendbuffer == NULL)
        {
            at_server_printfln("SEND FAIL");
            return AT_RESULT_NULL;
        }
        psendbuffer = sendbuffer;
        at_server_printfln("\r\nOK");
        at_server_printf(">");
        while(length > 0)
        {
            if(get_char_timeout(rt_tick_from_millisecond(WIOTA_WAIT_DATA_TIMEOUT), (char*)psendbuffer) != RT_EOK)
            {
                at_server_printfln("SEND FAIL");
                rt_free(sendbuffer);
                return AT_RESULT_NULL;
            }
            length--;
            psendbuffer++;
        }
        //at_server_printfln("\r\nRecv %d bytes", send_bytes);
    }
    else
    {
        sendbuffer = (unsigned char *)rt_malloc(256);
        psendbuffer = sendbuffer;
        length = 256;
        send_bytes = 256;
        at_server_printfln("\r\nOK");
        at_server_printf(">");
        while(length > 0)
        {
            if(get_char_timeout(rt_tick_from_millisecond(WIOTA_WAIT_DATA_TIMEOUT/3), (char*)psendbuffer) != RT_EOK)
            {
                break;
            }
            length--;
            psendbuffer++;
        }
        send_bytes -= length;
        //at_server_printfln("\r\nRecv %d bytes", send_bytes);
    }
    
    switch(type)
    {
        case AT_TEST_PRINTF:
            set_rt_kprintf_switch(*sendbuffer & 0x1);            
            at_server_printfln("DO OK");
            break;
        
        case AT_TEST_CMD:
            if (send_bytes > 0)
           {
               extern void uart_tool_set_data(unsigned int * data, unsigned int len);
               extern void uart_tool_handle_msg(void);
                uart_tool_set_data((unsigned int *)sendbuffer, send_bytes);
                uart_tool_handle_msg();
                at_server_printfln("DO OK");
           }
           else
           {
                at_server_printfln("DO ERROR");
           }
           break;
           
        default:
            at_server_printfln("DO ERROR");
            break;
    }
    rt_free(sendbuffer);
    sendbuffer = NULL;

    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+WIOTAINIT", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_wiota_init_exec);
AT_CMD_EXPORT("AT+WIOTAFREQ", "=<freqpint>", RT_NULL, at_freq_query, at_freq_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTADCXO", "=<dcxo>", RT_NULL, RT_NULL, at_dcxo_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAUSERID", "=<id0>,<id1>", RT_NULL, at_userid_query, at_userid_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARADIO", "=<snr>,<power>", RT_NULL, at_radio_query, RT_NULL, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACONFIG", "=<id_len>,<symbol>,<dlul>,<bt>,<group_num>,<systemid>,<subsystemid>", RT_NULL, at_system_config_query, at_system_config_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARUN", "=<state>", RT_NULL, RT_NULL, at_wiota_cfun_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACONNECT", "=<state>,<activetime>", RT_NULL, at_connect_query, at_connect_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASEND", "=<timeout>,<len>", RT_NULL, RT_NULL, at_wiotasend_setup, /*at_wiotasend_exec*/ RT_NULL );
AT_CMD_EXPORT("AT+WIOTARECV", "=<timeout>", RT_NULL, RT_NULL, at_wiotarecv_setup, at_wiota_recv_exec );
AT_CMD_EXPORT("AT+TEST", "=<type>,<len>", RT_NULL, RT_NULL, at_test_setup, RT_NULL);

#endif

//#endif