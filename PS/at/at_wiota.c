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
#include "uc_string_lib.h"
#include "uc_adda.h"

enum at_wiota_state
{
    AT_WIOTA_DEFAULT = 0,
    AT_WIOTA_INIT ,
    AT_WIOTA_RUN,
    AT_WIOTA_EXIT,
};

enum at_wiota_lpm
{
    AT_WIOTA_SLEEP = 0,
    AT_WIOTA_GATING,
};


#define ADC_DEV_NAME                      "adc"  

#define WIOTA_SCAN_FREQ_TIMEOUT 120000
#define WIOTA_SEND_TIMEOUT 60000
#define WIOTA_WAIT_DATA_TIMEOUT 10000
#define WIOTA_SEND_DATA_MUX_LEN 1024
#define WIOTA_DATA_END  0x1A

#define WIOTA_MUST_INIT(state) \
if(state != AT_WIOTA_INIT) \
{ \
     return AT_RESULT_REPETITIVE_FAILE; \
}

extern at_server_t at_get_server(void);
extern char *parse (char *b, char *f, ...);

static int wiota_state = AT_WIOTA_DEFAULT;

static rt_err_t get_char_timeout(rt_tick_t timeout, char * chr)
{
    at_server_t at_server = at_get_server();
    return at_server->get_char(at_server, chr, timeout);
}


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


static at_result_t at_scan_freq_setup(const char *args)
{
     unsigned int length = 0, len = 0, timeout = 0;
     unsigned char * data = RT_NULL;
     unsigned char * pdata = RT_NULL;
     uc_recv_back_t result;
     at_result_t at_re = AT_RESULT_OK;

     if (wiota_state != AT_WIOTA_RUN)
        return AT_RESULT_REPETITIVE_FAILE;

    args = parse ((char*)(++args),"dd", &timeout, &length);
     if (!args)
     {
         return AT_RESULT_PARSE_FAILE;
     }
     len = length;
    rt_kprintf("timeout=%d,len=%d\n", timeout, length);
    if(length > 0)
    {
        data = (unsigned char *)rt_malloc(length);
        if(data == RT_NULL)
        {
            at_server_printfln("SEND FAIL");
            return AT_RESULT_NULL;
        }
        pdata = data;
        //at_server_printfln("SUCC");
        at_server_printf(">");
        while(length)
        {
            if(get_char_timeout(rt_tick_from_millisecond(WIOTA_WAIT_DATA_TIMEOUT), (char*)pdata) != RT_EOK)
            {
                at_server_printfln("SEND FAIL");
                rt_free(pdata);
                return AT_RESULT_NULL;
            }
            //rt_kprintf("%x\n", *pdata);
            length--;
            pdata++;
        }
    }

    uc_wiota_scan_freq( data, len, timeout, RT_NULL, &result);
    rt_kprintf("%s result %d\n",__FUNCTION__, result.result);
    if (UC_OP_SUCC == result.result)
    {
        uc_freq_scan_result_p freqlinst = (uc_freq_scan_result_p)result.data;
        int freq_num = result.data_len/sizeof(uc_freq_scan_result_t);

        at_server_printf("+WIOTASCANF:");

        while(freq_num--)
        {
            if (freq_num)
                at_server_printfln("%d,%d,%d,%d", freqlinst->freq_idx, freqlinst->rssi, freqlinst->snr, freqlinst->is_synced);
            else
                at_server_printf("%d,%d,%d,%d", freqlinst->freq_idx, freqlinst->rssi, freqlinst->snr, freqlinst->is_synced);
            freqlinst ++;
        }

        rt_free(result.data);
    }
    else
        at_re = AT_RESULT_NULL;

     rt_free(data);
     return at_re;
}


static at_result_t at_scan_freq_exec(void)
{
    at_result_t at_re = AT_RESULT_OK;
    uc_recv_back_t result;

   if (wiota_state != AT_WIOTA_RUN)
        return AT_RESULT_REPETITIVE_FAILE;

    uc_wiota_scan_freq( RT_NULL, 0, WIOTA_SCAN_FREQ_TIMEOUT, RT_NULL, &result);
    rt_kprintf("%s result %d\n",__FUNCTION__, result.result);
    if (UC_OP_SUCC == result.result)
    {
        uc_freq_scan_result_p freqlinst = (uc_freq_scan_result_p)result.data;
        int freq_num = result.data_len/sizeof(uc_freq_scan_result_t);

        at_server_printf("+WIOTASCANF:");

        while(freq_num--)
        {
            if (freq_num)
                at_server_printfln("%d,%d,%d,%d", freqlinst->freq_idx, freqlinst->rssi, freqlinst->snr, freqlinst->is_synced);
            else
                at_server_printf("%d,%d,%d,%d", freqlinst->freq_idx, freqlinst->rssi, freqlinst->snr, freqlinst->is_synced);
            freqlinst ++;
        }

        rt_free(result.data);
    }
    else
        at_re = AT_RESULT_NULL;

     return at_re;


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
    rt_kprintf("userid:%x,%x\n", userid[0], userid[1]);

    uc_wiota_set_userid(userid, 4);

    return AT_RESULT_OK;
}


static at_result_t at_radio_query(void)
{
    rt_uint32_t temp = 0;
    radio_info_t radio;
    rt_device_t adc_dev;

    if(AT_WIOTA_RUN != wiota_state)
    {
        rt_kprintf("%s line %d wiota state error %d\n", __FUNCTION__, __LINE__, wiota_state);
        return AT_RESULT_FAILE;
    }
        
    adc_dev = rt_device_find(ADC_DEV_NAME);
    if (RT_NULL == adc_dev)
    {
        rt_kprintf("ad find %s fail\n", ADC_DEV_NAME);
    }

    temp = rt_adc_read((rt_adc_device_t)adc_dev, ADC_CONFIG_CHANNEL_CHIP_TEMP);

    uc_wiota_get_radio_info(&radio);
    //temp,rssi,ber,snr,power
    at_server_printfln("+WIOTARADIO=%d,%d,%d,%d,%d", temp, radio.rssi, radio.ber, radio.snr, radio.power);

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


static at_result_t at_wiotasend_exec(void)
{
    uint8_t * sendbuffer = NULL;
    uint8_t * psendbuffer;
    rt_err_t result = RT_EOK;
    int length = 0;


    if(AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }

    sendbuffer = (uint8_t *)rt_malloc(WIOTA_SEND_DATA_MUX_LEN+CRC32_LEN);    // reserve CRC32_LEN for low mac
    if (sendbuffer == RT_NULL)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    at_server_printf("\r\n>");
    //while(1)
    {
        psendbuffer = sendbuffer;
        length = WIOTA_SEND_DATA_MUX_LEN;

        while(length)
        {
            result = get_char_timeout(rt_tick_from_millisecond(WIOTA_WAIT_DATA_TIMEOUT), (char*)psendbuffer);
            if(result != RT_EOK )
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
        if((psendbuffer - sendbuffer) > 0)
        {
            //rt_kprintf("len=%d, sendbuffer=%s\n", psendbuffer - sendbuffer, sendbuffer);
            if(UC_OP_SUCC != uc_wiota_send_data(sendbuffer, psendbuffer - sendbuffer, WIOTA_SEND_TIMEOUT, RT_NULL))
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
    unsigned char * sendbuffer = NULL;
    unsigned char * psendbuffer;

    if(AT_WIOTA_RUN != wiota_state)
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
        sendbuffer = (unsigned char *)rt_malloc(length+CRC32_LEN);    // reserve CRC32_LEN for low mac
        if(sendbuffer == NULL)
        {
            at_server_printfln("SEND FAIL");
            return AT_RESULT_NULL;
        }
        psendbuffer = sendbuffer;
        //at_server_printfln("SUCC");
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

static at_result_t at_wiotalpm_setup(const char *args)
{
    int mode = 0, state = 0;
    args = parse ((char*)(++args),"dd", &mode, &state);

    switch(mode)
    {
        case AT_WIOTA_SLEEP:
        {
            at_server_printfln("OK");

            
            while(1);
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


AT_CMD_EXPORT("AT+WIOTAINIT", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_wiota_init_exec);
AT_CMD_EXPORT("AT+WIOTASCANF", "=<timeout>,<len>", RT_NULL, RT_NULL, at_scan_freq_setup, at_scan_freq_exec);
AT_CMD_EXPORT("AT+WIOTAFREQ", "=<freqpint>", RT_NULL, at_freq_query, at_freq_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTADCXO", "=<dcxo>", RT_NULL, RT_NULL, at_dcxo_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAUSERID", "=<id0>,<id1>", RT_NULL, at_userid_query, at_userid_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARADIO", "=<temp>,<rssi>,<ber>,<snr><power>", RT_NULL, at_radio_query, RT_NULL, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACONFIG", "=<id_len>,<symbol>,<dlul>,<bt>,<group_num>,<systemid>,<subsystemid>", RT_NULL, at_system_config_query, at_system_config_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARUN", "=<state>", RT_NULL, RT_NULL, at_wiota_cfun_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACONNECT", "=<state>,<activetime>", RT_NULL, at_connect_query, at_connect_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASEND", "=<timeout>,<len>", RT_NULL, RT_NULL, at_wiotasend_setup, at_wiotasend_exec);
AT_CMD_EXPORT("AT+WIOTARECV", "=<timeout>", RT_NULL, RT_NULL, at_wiotarecv_setup, at_wiota_recv_exec );
AT_CMD_EXPORT("AT+WIOTALPM", "=<mode>,<state>", RT_NULL, RT_NULL, at_wiotalpm_setup, RT_NULL );

#endif

//#endif
