#include <rtthread.h>
// #ifdef RT_USING_AT
#ifndef WIOTA_APP_DEMO
// #ifdef UC8288_MODULE
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include "uc_wiota_api.h"
#include "uc_wiota_static.h"
// #include "at.h"
// #include "ati_prs.h"
#include "uc_string_lib.h"

#include "uc_cbor.h"
#include "uc_coding.h"
#include "uc_ota_flash.h"
#include "uc_wiota_gateway_api.h"

#ifdef AT_WIOTA_GATEWAY_API
#define GATEWAY_MAG_CODE_DL_RECV                (1L << 1)
#define GATEWAY_MAG_CODE_UL_HEART               (1L << 2)
#define GATEWAY_MAG_CODE_OTA_REQ                (1L << 3)
#define GATEWAY_MAG_CODE_UL_MISS_DATA_REQ       (1L << 4)
#define GATEWAY_MAG_CODE_EXIT       (1L << 5)

#define GATEWAY_OTA_VER_PERIOD (7200000) // 2 hours

//#define GATEWAY_OTA_FLASH_BIN_SIZE 328
//#define GATEWAY_OTA_FLASH_REVERSE_SIZE 136
//#define GATEWAY_OTA_FLASH_OTA_SIZE 40
#define GATEWAY_OTA_BLOCK_SIZE 512
//#define GATEWAY_MODE_OTA_START_ADDR ((GATEWAY_OTA_FLASH_BIN_SIZE + GATEWAY_OTA_FLASH_REVERSE_SIZE) * 1024)

#define SET_BIT(value, bit) (value |= (1 << bit))
#define CLEAR_BIT(value, bit) (value &= ~(1 << bit))
#define JUDGMENT_BIT(value, bit) (value >> bit & 1)

static uc_wiota_gateway_user_recv_cb uc_wiota_gateway_user_recv = RT_NULL;
static uc_wiota_gateway_exception_report_cb uc_wiota_gateway_exce_report = RT_NULL;
#ifdef RT_USING_AT
extern void wiota_recv_callback(uc_recv_back_p data);
#endif
static void uc_wiota_gateway_api_handle(void *para);
// before scantf freq.
static uc_gateway_state_t uc_gateway_state = GATEWAY_DEFAULT;
static boolean wiota_gateway_mode_flag = RT_FALSE;
static boolean wiota_gateway_run_flag = RT_FALSE;

static void uc_gateway_clean_data(void);

extern void at_wiota_get_avail_freq_list(unsigned char *output_list, unsigned char list_len);

typedef enum
{
    GATEWAY_OTA_DEFAULT = 0,
    GATEWAY_OTA_DOWNLOAD = 1,
    GATEWAY_OTA_PROGRAM = 2,
    GATEWAY_OTA_STOP = 3
} uc_gateway_ota_state_t;

typedef struct uc_wiota_gateway_msg
{
    void *data;
    unsigned int data_len;
    int msg_type;
    unsigned char recv_msg_type;
}uc_wiota_gateway_msg_t;

typedef struct uc_wiota_gateway_api_mode
{
    //handler
    rt_thread_t gateway_handler;
    rt_mq_t gateway_mq;
    // rt_timer_t heart_timer;

    boolean gateway_mode;
    boolean reboot_flag;
    e_auth_state auth_state;
    char auth_code[16];
    char device_type[16];

    //ota para
    rt_timer_t ota_timer;
    rt_timer_t ver_timer;
    char new_version[16];
    uc_gateway_ota_state_t ota_state;
    int upgrade_type;
    unsigned int block_count;
    unsigned int block_size;
    unsigned char *mask_map;
    int miss_data_num;
    boolean miss_data_req;

    unsigned int wiota_userid;
    unsigned int dev_address;

}uc_wiota_gateway_api_mode_t;

static uc_wiota_gateway_api_mode_t gateway_mode = {0};
unsigned char gw_freq_list[APP_CONNECT_FREQ_NUM] = {0};

void uc_wiota_gateway_set_run_flag(boolean run_flag);
unsigned int uc_wiota_gateway_get_wiota_id(void);
boolean uc_wiota_gateway_get_reboot_flag(void);
void uc_wiota_gateway_set_reboot_flag(boolean flag);
static void uc_wiota_gateway_send_miss_data_req_to_queue(void);


boolean uc_wiota_gateway_get_run_flag(void)
{
    return wiota_gateway_run_flag;
}

void uc_wiota_gateway_set_run_flag(boolean run_flag)
{
    wiota_gateway_run_flag = run_flag;
}

static void uc_wiota_gateway_set_auth_state(e_auth_state state)
{
    gateway_mode.auth_state = state;
}

// static e_auth_state uc_wiota_gateway_get_auth_state(void)
// {
//     return gateway_mode.auth_state;
// }

static void uc_wiota_gateway_set_wiota_id(unsigned int wiota_id)
{
    gateway_mode.wiota_userid = wiota_id;
}

unsigned int uc_wiota_gateway_get_wiota_id(void)
{
    return gateway_mode.wiota_userid;
}

static void uc_wiota_gateway_set_dev_address(unsigned int dev_address)
{
    gateway_mode.dev_address = dev_address;
}

static unsigned int uc_wiota_gateway_get_dev_address(void)
{
    return gateway_mode.dev_address;
}

boolean uc_wiota_gateway_get_reboot_flag(void)
{
    return gateway_mode.reboot_flag;
}

void uc_wiota_gateway_set_reboot_flag(boolean flag)
{
    gateway_mode.reboot_flag = flag;
}

static void *uc_wiota_gateway_create_queue(const char *name, unsigned int max_msgs, unsigned char flag)
{
    rt_mq_t mq;
    void *msgpool;

    mq = rt_malloc(sizeof(struct rt_messagequeue));
    if (RT_NULL == mq)
    {
        return RT_NULL;
    }

    msgpool = rt_malloc(4 * max_msgs);
    if ( RT_NULL == msgpool)
    {
        rt_free(mq);
        return RT_NULL;
    }

    if (RT_EOK != rt_mq_init(mq, name, msgpool, 4, 4 * max_msgs, flag))
    {
        rt_free(mq);
        rt_free(msgpool);
        return RT_NULL;
    }

    return (void *)mq;
}

static int uc_wiota_gateway_recv_queue(void *queue, void **buf, signed int timeout)
{
    unsigned int address = 0;
    int result = 0;

    if(queue == RT_NULL)
    {
        rt_kprintf("uc_wiota_gateway_recv_queue null.\n");
        return RT_ERROR;
    }

    result = rt_mq_recv(queue, &address, 4, timeout);
    *buf = (void *)address;

    return result;
}

static int uc_wiota_gateway_send_queue(void *queue, void *buf, signed int timeout)
{
    unsigned int address = (unsigned int)buf;

    if(queue == RT_NULL)
    {
        rt_kprintf("uc_wiota_gateway_send_queue send null point.\n");
        return 0;
    }

    return rt_mq_send_wait(queue, &address, 4, timeout);
}

static int uc_wiota_gateway_dele_queue(void *queue)
{
    rt_err_t ret = rt_mq_detach(queue);
    rt_free(((rt_mq_t)queue)->msg_pool);
    rt_free(queue);
    return ret;
}

static void uc_wiota_gateway_send_msg_to_queue(int msg_code)
{
    static uc_wiota_gateway_msg_t recv_data = {0};
    recv_data.msg_type = msg_code;

    if(RT_EOK != uc_wiota_gateway_send_queue(gateway_mode.gateway_mq, &recv_data, 0))
    {
        rt_kprintf("uc_wiota_gateway_send_msg_to_queue error.\n");
    }
}

static void uc_wiota_gateway_recv_data_callback(uc_recv_back_p recv_data)
{
    //unsigned char *msg_data = RT_NULL;
    uc_wiota_gateway_msg_t *recv_data_buf = RT_NULL;

    recv_data_buf = rt_malloc(sizeof(uc_wiota_gateway_msg_t));
    if(recv_data_buf == NULL)
    {
        rt_kprintf("uc_wiota_gateway_recv_data_callback req recv_data_buf memory failed.\n");
        return;
    }

    recv_data_buf->data = recv_data->data;
    recv_data_buf->data_len = recv_data->data_len;
    recv_data_buf->msg_type = GATEWAY_MAG_CODE_DL_RECV;
    recv_data_buf->recv_msg_type = recv_data->type;

    if(gateway_mode.gateway_mq == RT_NULL)
    {
        rt_kprintf("uc_wiota_gateway_recv_data_callback send null point.\n");
        return;
    }

    if(RT_EOK != uc_wiota_gateway_send_queue(gateway_mode.gateway_mq, recv_data_buf, 10))
    {
        rt_free(recv_data->data);
        rt_free(recv_data_buf);
        rt_kprintf("uc_wiota_gateway_recv_data_callback send queue failed.\n");
    }

    //rt_free(recv_data->data);
}

static int uc_wiota_gateway_send_ps_cmd_data(unsigned char *data,
    int len,
    app_ps_header_t *ps_header)
{
    int ret = 0;
    unsigned char *cmd_coding = data;
    unsigned int cmd_coding_len = len;
    unsigned char *data_coding = RT_NULL;
    unsigned int data_coding_len = 0;
    UC_OP_RESULT send_result = 0;

    if (0 != app_data_coding(ps_header, cmd_coding, cmd_coding_len, &data_coding, &data_coding_len))
    {
        rt_kprintf("uc_wiota_gateway_send_ps_cmd_data coding head failed.\n");
        rt_free(cmd_coding);
        return ret;
    }
    send_result = uc_wiota_send_data(data_coding, data_coding_len, 10000, RT_NULL);
    if(send_result == UC_OP_SUCC)
    {
        ret = 1;
    }
    else
    {
        ret = 0;
    }

    rt_kprintf("uc_wiota_gateway_send_ps_cmd_data send data res %d.\n", send_result);

    ///rt_kprintf
    // rt_kprintf("send data_len %d:", data_coding_len);
    // for (int i = 0; i < data_coding_len; i++)
    //     rt_kprintf("%x ", data_coding[i]);
    // rt_kprintf("\n");


    //rt_free(cmd_coding);
    rt_free(data_coding);

    return ret;
}

static void uc_wiota_gateway_handle_ota_recv_timer_msg(void *para)
{
    if(wiota_gateway_mode_flag)
    {
        uc_wiota_gateway_send_miss_data_req_to_queue();
    }
    //else
    //{
    //    rt_kprintf("uc_wiota_gateway_handle_ota_recv_timer_msg error.\n");
    //}
    // uc_wiota_gateway_send_msg_to_queue(GATEWAY_MAG_CODE_OTA_REQ);
}

static int uc_wiota_gateway_send_auth_req(void)
{
    int ret = 0;
    app_ps_auth_req_t auth_req_data = {0};
    app_ps_header_t ps_header = {0};
    // unsigned int src_addr = 0;
    // unsigned char src_addr_len = 0;
    unsigned char serial[16] = {0};

    auth_req_data.auth_type = 0;
    rt_strncpy(auth_req_data.aut_code, gateway_mode.auth_code, rt_strlen(gateway_mode.auth_code));
    rt_strncpy((char *)auth_req_data.freq, (const char*)gw_freq_list, APP_CONNECT_FREQ_NUM);

    app_set_header_property(PRO_SRC_ADDR, 1, &ps_header.property);
    //uc_wiota_get_userid(&src_addr, &src_addr_len);
    uc_wiota_get_dev_serial(serial);

    unsigned int userid = ((serial[4] & 0x7f)<< 24)\
                        | (serial[5] << 16)\
                        | (serial[6] << 8)\
                        |  serial[7];

    uc_wiota_gateway_set_dev_address(userid);

    ps_header.addr.src_addr = uc_wiota_gateway_get_dev_address();
    ps_header.cmd_type = AUTHENTICATION_REQ;
    ps_header.packet_num = app_packet_num();

    // wiota send data
    ret = uc_wiota_gateway_send_ps_cmd_data((unsigned char *)&auth_req_data, sizeof(app_ps_auth_req_t), &ps_header);
    return ret;
}

static int uc_gateway_check_connect_state(void)
{
    short count = 300;
     unsigned char flag = 0;
     while(count --)
     {
         if (UC_STATUS_SYNC == uc_wiota_get_state())
         {
             flag ++;
             if (flag > 2)
                 return 0; // success
         }
         rt_thread_delay(20);
     }
    return 1;// fail
}

static void uc_wiota_gateway_auth_res_msg(unsigned char *data, unsigned int data_len)
{
    app_ps_auth_res_t *auth_res_data = RT_NULL;

    auth_res_data = (app_ps_auth_res_t *)data;

    rt_kprintf("gateway_auth result %d allocation id 0x%x.\n", auth_res_data->connect_index.state, auth_res_data->wiota_id);
    switch (auth_res_data->connect_index.state)
    {
    case AUTHENTICATION_SUC:
        uc_wiota_gateway_set_wiota_id(auth_res_data->wiota_id);
        uc_wiota_set_freq_list(auth_res_data->freq_list, APP_MAX_FREQ_LIST_NUM);
        uc_wiota_save_static_info();
        // wait send ack.
        if (0 != auth_res_data->wiota_id)
        {
            rt_thread_delay(150);
            uc_wiota_disconnect();
            uc_wiota_set_wiotaid(auth_res_data->wiota_id);
            uc_wiota_connect();
            rt_thread_delay(200);
            if ( 0 == uc_gateway_check_connect_state())
            {
                // report success 
                rt_timer_start(gateway_mode.ver_timer);
                uc_gateway_state = GATEWAY_NORMAL;
            }
            else
            {
                uc_gateway_state = GATEWAY_FAILED;
                 if (uc_wiota_get_auto_connect_flag())
                {
                    //rt_kprintf("uc_wiota_gateway_auth_res_msg set reboot flag.\n");
                    uc_wiota_gateway_set_reboot_flag(RT_TRUE);
                }
            }
        }
        else
        {
            uc_gateway_state = GATEWAY_NORMAL;
        }
        
        if (RT_NULL != uc_wiota_gateway_exce_report)
            uc_wiota_gateway_exce_report(uc_gateway_state);

        break;

    case AUTHENTICATION_NO_DATA:
    case AUTHENTICATION_FAIL:
        uc_gateway_state = GATEWAY_FAILED;
        uc_wiota_gateway_exce_report(uc_gateway_state);
        break;

    case AUTHENTICATION_INFO_CHANGE:
        uc_wiota_set_freq_list(auth_res_data->freq_list, APP_MAX_FREQ_LIST_NUM);
        uc_wiota_save_static_info();
        break;

    case AUTHENTICATION_RECONNECT:

        if (RT_NULL != uc_wiota_gateway_exce_report)
            uc_wiota_gateway_exce_report(uc_gateway_state);

        rt_thread_delay(150);
        uc_wiota_disconnect();
         uc_wiota_set_freq_info(auth_res_data->connect_index.freq);
        uc_wiota_connect();
        if ( 0 == uc_gateway_check_connect_state())
        {
           uc_wiota_gateway_send_auth_req();
        }
        else
        {
        if (RT_NULL != uc_wiota_gateway_exce_report)
            uc_wiota_gateway_exce_report(GATEWAY_FAILED);
        }

        break;
    default:
        break;
    }
    uc_wiota_gateway_set_auth_state(auth_res_data->connect_index.state);

    //rt_free(cmd_decoding);
}

static boolean uc_wiota_gateway_check_if_upgrade_required(app_ps_ota_upgrade_req_t *ota_upgrade_req)
{
    boolean is_upgrade_range = FALSE;
    boolean is_required = FALSE;
    unsigned int dev_address = uc_wiota_gateway_get_dev_address();
    u8_t version[15] = {0};

    uc_wiota_get_version(version, RT_NULL, RT_NULL,RT_NULL);

    if (ota_upgrade_req->upgrade_range == 1)
    {
        for (int i = 0; i < APP_MAX_IOTE_UPGRADE_NUM; i++)
        {
            if (dev_address == ota_upgrade_req->iote_list[i])
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
        if (0 == rt_strncmp((char*)version, ota_upgrade_req->old_version, rt_strlen(ota_upgrade_req->old_version)) &&
            0 == rt_strncmp(gateway_mode.device_type, ota_upgrade_req->device_type, rt_strlen(ota_upgrade_req->device_type)))
        {
            is_required = TRUE;
        }
    }

    return is_required;
}

static boolean uc_wiota_gateway_whether_the_ota_upgrade_data_is_recved(void)
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

    rt_kprintf("gateway_ota_download %d/%d", block_count, gateway_mode.block_count);

    if (block_count >= gateway_mode.block_count)
    {
        rt_kprintf("ota data recv end\n");
        return TRUE;
    }

    return FALSE;
}

static void uc_wiota_gateway_send_miss_data_req_to_queue(void)
{
    if(!gateway_mode.miss_data_req)
    {
        uc_wiota_gateway_send_msg_to_queue(GATEWAY_MAG_CODE_UL_MISS_DATA_REQ);
        gateway_mode.miss_data_req = RT_TRUE;
    }
}

static void uc_wiota_gateway_ota_upgrade_res_msg(unsigned char *data, unsigned int data_len)
{
    //unsigned char *cmd_decoding = RT_NULL;
    app_ps_ota_upgrade_req_t *ota_upgrade_req = RT_NULL;
    u8_t version[15] = {0};
    int bin_size = 0;
    int reserved_size = 0;
     int ota_size = 0;

    uc_wiota_get_version(version, RT_NULL, RT_NULL,RT_NULL);

     get_partition_size(&bin_size , &reserved_size , &ota_size);
     rt_kprintf("bin size %d reserved_size %d\n", bin_size, reserved_size);
    #if 0
    if (app_cmd_decoding(OTA_UPGRADE_REQ, data, data_len, &cmd_decoding) < 0)
    {
        rt_kprintf("%s line %d app_cmd_decoding error\n", __FUNCTION__, __LINE__);
        return;
    }
    #endif
    ota_upgrade_req = (app_ps_ota_upgrade_req_t *)data;

    if (uc_wiota_gateway_check_if_upgrade_required(ota_upgrade_req))
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

            uc_wiota_ota_flash_erase(bin_size + reserved_size, file_size);
            gateway_mode.ota_state = GATEWAY_OTA_DOWNLOAD;
            gateway_mode.upgrade_type = ota_upgrade_req->upgrade_type;
            rt_strncpy(gateway_mode.new_version, ota_upgrade_req->new_version, rt_strlen(ota_upgrade_req->new_version));

            rt_timer_control(gateway_mode.ota_timer, RT_TIMER_CTRL_SET_TIME, (void *)&ota_upgrade_req->upgrade_time);
            rt_timer_start(gateway_mode.ota_timer);

            rt_kprintf("GATEWAY_OTA_DEFAULT file_size %d, mask_map_size %d, block_size %d, block_count %d, ota_state %d, upgrade_type %d, upgrade_time %d\n",
                       file_size, mask_map_size, gateway_mode.block_size, gateway_mode.block_count, gateway_mode.ota_state, gateway_mode.upgrade_type, ota_upgrade_req->upgrade_time);
            rt_kprintf("new_version %s, old_version %s, device_type %s\n", gateway_mode.new_version, version, gateway_mode.device_type);
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
                uc_wiota_ota_flash_write(ota_upgrade_req->data, bin_size + reserved_size + ota_upgrade_req->data_offset, ota_upgrade_req->data_length);
                SET_BIT(gateway_mode.mask_map[offset / 8], offset % 8);
                rt_kprintf("GATEWAY_OTA_DOWNLOAD offset %d mask_map[%d] = 0x%x\n", offset, offset / 8, gateway_mode.mask_map[offset / 8]);
            }

            if (uc_wiota_gateway_whether_the_ota_upgrade_data_is_recved())
            {
                if (0 == uc_wiota_ota_check_flash_data(bin_size + reserved_size, file_size, ota_upgrade_req->md5))
                {
                    rt_kprintf("ota data checkout ok, jump to program\n");

                    gateway_mode.ota_state = GATEWAY_OTA_PROGRAM;
                    rt_free(gateway_mode.mask_map);
                    gateway_mode.mask_map = RT_NULL;
                    rt_timer_stop(gateway_mode.ota_timer);

                    wiota_gateway_mode_flag = RT_FALSE;
                    //set_partition_size(GATEWAY_OTA_FLASH_BIN_SIZE, GATEWAY_OTA_FLASH_REVERSE_SIZE, GATEWAY_OTA_FLASH_OTA_SIZE);
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

    //rt_free(cmd_decoding);
}

static void uc_wiota_gateway_ota_upgrade_stop_msg(unsigned char *data, unsigned int data_len)
{
    //unsigned char *cmd_decoding = RT_NULL;
    app_ps_ota_upgrade_stop_t *ota_upgrade_stop = RT_NULL;
    unsigned int dev_address = uc_wiota_gateway_get_dev_address();
#if 0
    if (app_cmd_decoding(OTA_UPGRADE_STOP, data, data_len, &cmd_decoding) < 0)
    {
        rt_kprintf("%s line %d app_cmd_decoding error\n", __FUNCTION__, __LINE__);
        return;
    }
#endif
    ota_upgrade_stop = (app_ps_ota_upgrade_stop_t *)data;

    for (int i = 0; i < APP_MAX_IOTE_UPGRADE_STOP_NUM; i++)
    {
        rt_kprintf("iote_list 0x%x\n", ota_upgrade_stop->iote_list[i]);
        if (dev_address == ota_upgrade_stop->iote_list[i])
        {
            if (gateway_mode.ota_state == GATEWAY_OTA_DOWNLOAD)
            {
                rt_free(gateway_mode.mask_map);
                gateway_mode.mask_map = RT_NULL;
                rt_timer_stop(gateway_mode.ota_timer);
            }
            gateway_mode.ota_state = GATEWAY_OTA_STOP;
            rt_kprintf("0x%x stop ota upgrade\n", dev_address);
            break;
        }
    }

    //rt_free(cmd_decoding);
}

static void uc_wiota_gateway_ota_upgrade_state_msg(unsigned char *data, unsigned int data_len)
{
    //unsigned char *cmd_decoding = RT_NULL;
    app_ps_ota_upgrade_state_t *ota_upgrade_state = (app_ps_ota_upgrade_state_t *)data;
    u8_t version[15] = {0};

    uc_wiota_get_version(version, RT_NULL, RT_NULL,RT_NULL);
#if 0
    if (app_cmd_decoding(OTA_UPGRADE_STATE, data, data_len, &cmd_decoding) < 0)
    {
        rt_kprintf("%s line %d app_cmd_decoding error\n", __FUNCTION__, __LINE__);
        return;
    }
    ota_upgrade_state = (app_ps_ota_upgrade_state_t *)data;
#endif
    if (0 == rt_strncmp(ota_upgrade_state->old_version, (const char*)version, rt_strlen((const char *)version)) &&
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
                uc_wiota_gateway_send_miss_data_req_to_queue();
            }
        }
    }

    //rt_free(cmd_decoding);
}

static void uc_wiota_gateway_recv_dl_msg(unsigned char *data, unsigned int data_len, int response_flag, unsigned char data_type)
{
    if(response_flag)
    {
        //no use
        //rt_kprintf("uc_wiota_gateway_recv_dl_msg recv response.\n");
    }
    else
    {
        if(uc_wiota_gateway_user_recv != RT_NULL)
        {
            uc_wiota_gateway_user_recv(data, data_len, data_type);
        }
    }
}

static void uc_wiota_gateway_analisys_dl_msg(unsigned char *data, unsigned int data_len, unsigned char data_type)
{
    app_ps_header_t ps_header = {0};
    unsigned char *data_decoding = RT_NULL;
    unsigned int data_decoding_len = 0;

    if (0 != app_data_decoding(data, data_len, &data_decoding, &data_decoding_len, &ps_header))
    {
        rt_kprintf("uc_wiota_gateway_analisys_dl_msg decode failed.\n");
        return;
    }

   rt_kprintf("recv gateway cmd %d.\n", ps_header.cmd_type);

    switch (ps_header.cmd_type)
    {
    case AUTHENTICATION_RES:
        uc_wiota_gateway_auth_res_msg(data_decoding, data_decoding_len);
        break;

    case OTA_UPGRADE_REQ:
        uc_wiota_gateway_ota_upgrade_res_msg(data_decoding, data_decoding_len);
        break;

    case OTA_UPGRADE_STOP:
        uc_wiota_gateway_ota_upgrade_stop_msg(data_decoding, data_decoding_len);
        break;

    case OTA_UPGRADE_STATE:
        uc_wiota_gateway_ota_upgrade_state_msg(data_decoding, data_decoding_len);
        break;

    case IOTE_USER_DATA:
        uc_wiota_gateway_recv_dl_msg(data_decoding, data_decoding_len, ps_header.property.response_flag, data_type);
        break;
    }

    rt_free(data_decoding);
}

static int uc_wiota_gateway_send_ota_req_msg(void)
{
    app_ps_version_verify_t version_verify = {0};
    app_ps_header_t ps_header = {0};
    u8_t version[15] = {0};

    if(wiota_gateway_mode_flag)
    {
        uc_wiota_get_version(version, RT_NULL, RT_NULL,RT_NULL);

        rt_strncpy(version_verify.software_version, (const char*)version, rt_strlen((const char*)version));
        uc_wiota_get_hardware_ver((unsigned char *)version_verify.hardware_version);
        rt_strncpy(version_verify.device_type, gateway_mode.device_type, rt_strlen(gateway_mode.device_type));

        app_set_header_property(PRO_SRC_ADDR, 1, &ps_header.property);
        ps_header.addr.src_addr = uc_wiota_gateway_get_dev_address();
        ps_header.cmd_type = VERSION_VERIFY;
        ps_header.packet_num = app_packet_num();

        return uc_wiota_gateway_send_ps_cmd_data((unsigned char *)&version_verify,
            sizeof(app_ps_version_verify_t),
            &ps_header);
    }

    return 0;
}

static void uc_wiota_gateway_handle_ota_req_timer()
{
    if(wiota_gateway_mode_flag)
    {
        uc_wiota_gateway_send_msg_to_queue(GATEWAY_MAG_CODE_OTA_REQ);
    }
    else
    {
        rt_kprintf("uc_wiota_gateway_handle_ota_req_timer error.\r\n");
    }

    // if(wiota_gateway_mode_flag)
    // {
    //     uc_wiota_gateway_send_ota_req_msg();
    // }
    // else
    // {
    //     rt_kprintf("uc_wiota_gateway_handle_ota_req_timer error.\r\n");
    // }
}

static void uc_wiota_gateway_ota_miss_data_req_msg(void)
{
    app_ps_header_t ps_header = {0};
    app_ps_iote_missing_data_req_t miss_data_req = {0};
    u8_t version[15] = {0};

    uc_wiota_get_version(version, RT_NULL, RT_NULL,RT_NULL);

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
    rt_strncpy(miss_data_req.old_version, (const char*)version, rt_strlen((const char*)version));

    app_set_header_property(PRO_SRC_ADDR, 1, &ps_header.property);
    ps_header.addr.src_addr = uc_wiota_gateway_get_dev_address();
    ps_header.cmd_type = IOTE_MISSING_DATA_REQ;
    ps_header.packet_num = app_packet_num();

    uc_wiota_gateway_send_ps_cmd_data((unsigned char *)&miss_data_req,
        sizeof(app_ps_iote_missing_data_req_t),
        &ps_header);
}

int uc_gateway_get_random(void)
{
    int random_num = 0;
    static unsigned int *ptr = (unsigned int *)(0x1a107080);

    REG(0x1a10a02c) &= ~(1<<4);
    random_num = *ptr;
    if(random_num < 0)
    {
        random_num = (~random_num)+1;
    }
    return random_num;
}

static int uc_wiota_gateway_set_id()
{
    //if (!uc_wiota_get_auto_connect_flag())
    unsigned char serial[16] ={0};
    unsigned int dev_id = 0;
    unsigned int userid = 0;
    unsigned char len = 4;

    uc_wiota_get_dev_serial(serial);
    dev_id = ((serial[4] & 0x7f)<< 24)\
    | (serial[5] << 16)\
    | (serial[6] << 8)\
    |  serial[7] ;

    if (dev_id == 0 || dev_id == (~0))
    {
        rt_kprintf("invalid dev_id. regenauto userid.\n");
        uc_gateway_get_random();
        uc_gateway_get_random();
        userid = uc_gateway_get_random() & 0x7FFFFFFF;
    }
    
    rt_kprintf("user id 0x%x\n", dev_id);
    uc_wiota_get_userid(&userid, &len);

    if (dev_id != userid)
    {
        int count = 0;
        UC_WIOTA_STATUS state;
        rt_kprintf("userid error,reset id\n");
        //dis connect
        uc_wiota_disconnect();
        uc_wiota_set_userid( &dev_id , 4);
        // connect
        uc_wiota_connect();

        while(1)
        {
            state = uc_wiota_get_state();
            switch(state)
            {
                case UC_STATUS_SYNC:
                    return 0;
                default:
                    rt_thread_mdelay(50);
            }
            
            if ( count < 100)
                count ++;
           else
                return 1; // timeout
        }
    }

    return 0;
}

int uc_gatewaytest(void)
{
    if (RT_NULL != gateway_mode.gateway_handler)
    {
        if (gateway_mode.gateway_handler->type == 1)
            return 0;
        return 1;
    }

    return 0;
}

int uc_wiota_gateway_start(uc_gatway_mode_e mode, char *auth_key, unsigned char freq_list[APP_CONNECT_FREQ_NUM])
{
    int ret ;
    unsigned int delay_count = 0;


    if(!wiota_gateway_mode_flag)
    {
        if((uc_wiota_get_state() != UC_STATUS_SYNC))
        {
            rt_kprintf("uc_wiota_gateway_start no connect\n");
            return UC_GATEWAY_NO_CONNECT;
        }

        if(auth_key == NULL)
        {
            rt_kprintf("no authencation code.\n");
            return UC_GATEWAY_NO_KEY;
        }
        
        if (uc_wiota_gateway_set_id())
        {
			rt_kprintf("uc_wiota_gateway_set_id error\n");
             return UC_GATEWAY_NO_CONNECT;
        }

        // init para
        rt_memset(&gateway_mode, 0, sizeof(uc_wiota_gateway_api_mode_t));
        rt_strncpy(gateway_mode.auth_code, auth_key, rt_strlen(auth_key));
        rt_strncpy(gateway_mode.device_type, "iote", 4);
        gateway_mode.gateway_mode = RT_TRUE;
        gateway_mode.auth_state = AUTHENTICATION_FAIL;

        // init queue
        gateway_mode.gateway_mq = uc_wiota_gateway_create_queue("gw_mq", 4, RT_IPC_FLAG_PRIO);
        if(gateway_mode.gateway_mq == RT_NULL)
        {
            rt_kprintf("uc_wiota_gateway_start req queue memory failed.\n");
            return UC_CREATE_QUEUE_FAIL;
        }

        gateway_mode.ota_timer = rt_timer_create("t_ota",
                                                uc_wiota_gateway_handle_ota_recv_timer_msg,
                                                RT_NULL,
                                                10000,      //tick
                                                RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
        if(gateway_mode.ota_timer == RT_NULL)
        {
            uc_wiota_gateway_dele_queue(gateway_mode.gateway_mq);
            //rt_kprintf("uc_wiota_gateway_start req ota timer memory failed.\n");
            return UC_CREATE_MISSTIMER_FAIL;
        }

        gateway_mode.ver_timer = rt_timer_create("t_ver",
                                                uc_wiota_gateway_handle_ota_req_timer,
                                                RT_NULL,
                                                GATEWAY_OTA_VER_PERIOD,
                                                RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
        if (RT_NULL == gateway_mode.ver_timer)
        {
            uc_wiota_gateway_dele_queue(gateway_mode.gateway_mq);
            rt_timer_delete(gateway_mode.ota_timer);
            rt_kprintf("%s line %d create timer fail\n", __FUNCTION__, __LINE__);
            return UC_CREATE_OTATIMER_FAIL;
        }
        uc_wiota_set_crc(1);

        // register callback
        uc_wiota_register_recv_data_callback(uc_wiota_gateway_recv_data_callback, UC_CALLBACK_NORAMAL_MSG);

        gateway_mode.gateway_handler = rt_thread_create("gateway",
                                                        uc_wiota_gateway_api_handle,
                                                        RT_NULL,
                                                        1024,
                                                        5,
                                                        3);
        if(gateway_mode.gateway_handler == RT_NULL)
        {
            uc_wiota_gateway_dele_queue(gateway_mode.gateway_mq);
            rt_timer_delete(gateway_mode.ota_timer);
            rt_timer_delete(gateway_mode.ver_timer);
            rt_kprintf("uc_wiota_gateway_start creat task failed.\n");
            return UC_CREATE_TASK_FAIL;
        }

        // start task
        rt_thread_startup(gateway_mode.gateway_handler);

        if (RT_NULL != freq_list)
            rt_memcpy(gw_freq_list, freq_list, APP_CONNECT_FREQ_NUM);

       if (mode == UC_GATEWAY_MODE)
        {
            // send auth req
            ret = uc_wiota_gateway_send_auth_req();
            if (ret)
            {// success
                rt_thread_mdelay(500);

                delay_count = 0;

                while((uc_wiota_get_state() != UC_STATUS_SYNC) || (uc_wiota_gateway_get_reboot_flag()) || (uc_gateway_state != GATEWAY_NORMAL))
                {
                    rt_thread_mdelay(10);
                    delay_count++;
                    if(delay_count >= 2000)
                    {
                        rt_kprintf("uc_wiota_gateway_start timeout type %d.\n", gateway_mode.gateway_handler->type);
                        uc_gateway_clean_data();
                        //ret = 0;
                        return UC_GATEWAY_AUTO_FAIL;
                    }
                }
                //rt_thread_mdelay(1000);

                if(ret)
                {
                    uc_wiota_register_recv_data_callback(uc_wiota_gateway_recv_data_callback, UC_CALLBACK_NORAMAL_MSG);
                    wiota_gateway_mode_flag = RT_TRUE;
                }
                else
                {
                    uc_wiota_gateway_end();
                }
            }
            else 
            { // fail
                rt_kprintf("uc_wiota_gateway_send_auth_req fail\n");
                uc_gateway_clean_data();
                uc_gateway_state = GATEWAY_FAILED;
                return UC_GATEWAY_SEDN_FAIL;
            }
      }
       else if (mode == UC_TRANSMISSION_MODE)
      {
            wiota_gateway_mode_flag = RT_TRUE;
            uc_gateway_state = GATEWAY_NORMAL;
            rt_timer_start(gateway_mode.ver_timer);
      }
    }
    else
    {
        rt_kprintf("wiota_gateway_mode_flag  error\n");
        return UC_GATEWAY_OTHER_FAIL;
    }

    return UC_GATEWAY_OK;
}

int uc_wiota_gateway_send_data(void *data, unsigned int data_len, unsigned int timeout)
{
    //int ret = 0;
    unsigned char *data_coding = RT_NULL;
    unsigned int data_coding_len = 0;
    app_ps_header_t ps_header = {0};
    UC_OP_RESULT send_result = UC_OP_FAIL;

    if (data_len > GATEWAY_SEND_MAX_LEN)
    {
        rt_kprintf("uc_wiota_gateway_send_data len error.len %d\n", data_len);
        return 0;
    }

    if(wiota_gateway_mode_flag)
    {
        app_set_header_property(PRO_SRC_ADDR, 1, &ps_header.property);
        //app_set_header_property(PRO_NEED_RES, 1, &ps_header.property);  //need response
        ps_header.cmd_type = IOTE_USER_DATA;
        ps_header.addr.src_addr = uc_wiota_gateway_get_dev_address();
        app_set_header_property(PRO_PACKET_NUM, 1, &ps_header.property);
        ps_header.packet_num = app_packet_num();

        if (0 != app_data_coding(&ps_header, data, data_len, &data_coding, &data_coding_len))
        {
            rt_kprintf("uc_wiota_gateway_send_data code failed.\n");
            return 0;
        }

        send_result = uc_wiota_send_data(data_coding, data_coding_len, timeout, RT_NULL);

        rt_free(data_coding);

        if(send_result == UC_OP_SUCC)
        {
            return 1;
        }
        else if(send_result == UC_OP_TIMEOUT)
        {
            rt_kprintf("uc_wiota_gateway_send_data send data timeout.\n");
        }
        else
        {
            rt_kprintf("uc_wiota_gateway_send_data send data failed.\n");
        }
    }

    return 0;
}

int uc_wiota_gateway_state_update_info_msg(void)
{
    app_ps_iote_state_update_t state_update_data = {0};
    app_ps_header_t ps_header = {0};
    radio_info_t radio = {0};
    char device_type[] = {"iote"};

    if(wiota_gateway_mode_flag)
    {
        uc_wiota_get_radio_info(&radio);

        rt_strncpy(state_update_data.device_type, device_type, rt_strlen(device_type));
        state_update_data.rssi = radio.rssi;
        state_update_data.snr = radio.snr;

        app_set_header_property(PRO_SRC_ADDR, 1, &ps_header.property);
        ps_header.cmd_type = IOTE_STATE_UPDATE;
        ps_header.addr.src_addr = uc_wiota_gateway_get_dev_address();
        ps_header.packet_num = app_packet_num();

        return uc_wiota_gateway_send_ps_cmd_data((unsigned char *)&state_update_data,
            sizeof(app_ps_iote_state_update_t),
            &ps_header);
    }

    return 0;
}

int uc_wiota_gateway_register_user_recv_cb(uc_wiota_gateway_user_recv_cb user_recv_cb, uc_wiota_gateway_exception_report_cb user_get_exc_report_cb)
{
    uc_wiota_gateway_user_recv = user_recv_cb;

    uc_wiota_gateway_exce_report = user_get_exc_report_cb;

    return 1;
}

int uc_wiota_gateway_ota_req(void)
{
    if(gateway_mode.gateway_mode)
    {
        return uc_wiota_gateway_send_ota_req_msg();
    }

    return 0;
}

int uc_wiota_gateway_set_ota_period(unsigned int p_tick)
{
    if((gateway_mode.ver_timer != RT_NULL)\
        && (p_tick > GATEWAY_OTA_VER_PERIOD)\
        && (wiota_gateway_mode_flag))
    {
        rt_timer_control(gateway_mode.ver_timer, RT_TIMER_CTRL_SET_TIME, &p_tick);
        rt_timer_start(gateway_mode.ver_timer);
        return 1;
    }
    return 0;
}

static void uc_gateway_clean_data(void)
{
#ifdef RT_USING_AT
        uc_wiota_register_recv_data_callback(wiota_recv_callback, UC_CALLBACK_NORAMAL_MSG);
        // uc_wiota_register_recv_data_callback(wiota_recv_callback, UC_CALLBACK_STATE_INFO);
#endif

        if(gateway_mode.ota_timer != RT_NULL)
        {
            rt_kprintf("%s line %d\n", __FUNCTION__, __LINE__);
            rt_timer_delete(gateway_mode.ota_timer);
            gateway_mode.ota_timer = RT_NULL;
        }

        if(gateway_mode.ver_timer != RT_NULL)
        {
            rt_timer_delete(gateway_mode.ver_timer);
            gateway_mode.ver_timer = RT_NULL;
        }

        if(gateway_mode.gateway_handler != RT_NULL)
        {
            rt_thread_delete(gateway_mode.gateway_handler);
            gateway_mode.gateway_handler = RT_NULL;
        }

        wiota_gateway_mode_flag = RT_FALSE;
        gateway_mode.gateway_mode = RT_FALSE;

        while (1)
        {
            uc_wiota_gateway_msg_t *recv_data;
            int  ret = uc_wiota_gateway_recv_queue(gateway_mode.gateway_mq, (void **)&recv_data, 0);
            if (RT_EOK != ret)
                break;

            if (recv_data->msg_type == GATEWAY_MAG_CODE_DL_RECV)
            {
                rt_free(recv_data->data);
                rt_free(recv_data);
            }
        }

        if(gateway_mode.gateway_mq != RT_NULL)
        {
            uc_wiota_gateway_dele_queue(gateway_mode.gateway_mq);
            gateway_mode.gateway_mq = RT_NULL;
        }



        uc_gateway_state = GATEWAY_END;

 }

int uc_wiota_gateway_end(void)
{
    if(wiota_gateway_mode_flag)
    {
        uc_gateway_clean_data();
    }
    else
        return 1;
    
    return 0;
}

uc_gateway_state_t uc_wiota_gateway_get_state(void)
{
    return uc_gateway_state;
}

static void uc_wiota_gateway_api_handle(void *para)
{
    uc_wiota_gateway_msg_t *recv_data = RT_NULL;

    while(1)
    {
        if(gateway_mode.gateway_mode)   //if not set 1, task out
        {
            if(gateway_mode.gateway_mq == RT_NULL)
            {
                rt_kprintf("gateway_mode.gateway_mq is null point.\n");
                break;
            }

            if (RT_EOK != uc_wiota_gateway_recv_queue(gateway_mode.gateway_mq, (void **)&recv_data, RT_WAITING_FOREVER))
            {
                continue;
            }
            rt_kprintf("recv_data->msg_type %d\n", recv_data->msg_type);
            switch(recv_data->msg_type)
            {
            case GATEWAY_MAG_CODE_DL_RECV:
                uc_wiota_gateway_analisys_dl_msg(recv_data->data, recv_data->data_len, recv_data->recv_msg_type);
                rt_free(recv_data->data);
                rt_free(recv_data);
                break;
            case GATEWAY_MAG_CODE_UL_HEART:
               uc_wiota_gateway_state_update_info_msg();
                break;
            case GATEWAY_MAG_CODE_OTA_REQ:
                uc_wiota_gateway_send_ota_req_msg();
                break;
            case GATEWAY_MAG_CODE_UL_MISS_DATA_REQ:
                uc_wiota_gateway_ota_miss_data_req_msg();
                break;
             case GATEWAY_MAG_CODE_EXIT:
                return ;
            default:
                rt_kprintf("uc_wiota_gateway_api_handle default.\n");
                break;
            }
        }
        else
        {
            rt_kprintf("uc_wiota_gateway_api_handle mode out.\n");
            break;
        }
    }
}

#endif  // AT_WIOTA_GATEWAY_API
// #endif  // RT_USING_AT
#endif  // WIOTA_APP_DEMO
// #endif  // UC8288_MODULE

