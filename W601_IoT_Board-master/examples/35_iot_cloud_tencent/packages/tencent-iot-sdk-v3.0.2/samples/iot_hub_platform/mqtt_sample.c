/*
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright (C) 2016 THL A29 Limited, a Tencent company. All rights reserved.

 * Licensed under the MIT License (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>

#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"
#include "rtthread.h"



#define MAX_SIZE_OF_TOPIC_CONTENT 100
#define MQTT_BASIC_THREAD_STACK_SIZE 	6144


#ifdef AUTH_MODE_CERT
    static char sg_cert_file[PATH_MAX + 1];      //�ͻ���֤��ȫ·��
    static char sg_key_file[PATH_MAX + 1];       //�ͻ�����Կȫ·��
#endif

static DeviceInfo sg_devInfo;
static int sg_count = 0;
static int sg_sub_packet_id = -1;
static int running_state = 0;


static bool log_handler(const char* message) {
	//ʵ����־�ص���д����
	//ʵ�����ݺ��뷵��true
	return false;
}

void event_handler(void *pclient, void *handle_context, MQTTEventMsg *msg) {
	MQTTMessage* mqtt_messge = (MQTTMessage*)msg->msg;
	uintptr_t packet_id = (uintptr_t)msg->msg;

	switch(msg->event_type) {
		case MQTT_EVENT_UNDEF:
			Log_i("undefined event occur.");
			break;

		case MQTT_EVENT_DISCONNECT:
			Log_i("MQTT disconnect.");
			break;

		case MQTT_EVENT_RECONNECT:
			Log_i("MQTT reconnect.");
			break;

		case MQTT_EVENT_PUBLISH_RECVEIVED:
			Log_i("topic message arrived but without any related handle: topic=%.*s, topic_msg=%.*s",
					  mqtt_messge->topic_len,
					  mqtt_messge->ptopic,
					  mqtt_messge->payload_len,
					  mqtt_messge->payload);
			break;
		case MQTT_EVENT_SUBCRIBE_SUCCESS:
			Log_i("subscribe success, packet-id=%u", (unsigned int)packet_id);
			sg_sub_packet_id = packet_id;
			break;

		case MQTT_EVENT_SUBCRIBE_TIMEOUT:
			Log_i("subscribe wait ack timeout, packet-id=%u", (unsigned int)packet_id);
			sg_sub_packet_id = packet_id;
			break;

		case MQTT_EVENT_SUBCRIBE_NACK:
			Log_i("subscribe nack, packet-id=%u", (unsigned int)packet_id);
			sg_sub_packet_id = packet_id;
			break;

		case MQTT_EVENT_UNSUBCRIBE_SUCCESS:
			Log_i("unsubscribe success, packet-id=%u", (unsigned int)packet_id);
			break;

		case MQTT_EVENT_UNSUBCRIBE_TIMEOUT:
			Log_i("unsubscribe timeout, packet-id=%u", (unsigned int)packet_id);
			break;

		case MQTT_EVENT_UNSUBCRIBE_NACK:
			Log_i("unsubscribe nack, packet-id=%u", (unsigned int)packet_id);
			break;

		case MQTT_EVENT_PUBLISH_SUCCESS:
			Log_i("publish success, packet-id=%u", (unsigned int)packet_id);
			break;

		case MQTT_EVENT_PUBLISH_TIMEOUT:
			Log_i("publish timeout, packet-id=%u", (unsigned int)packet_id);
			break;

		case MQTT_EVENT_PUBLISH_NACK:
			Log_i("publish nack, packet-id=%u", (unsigned int)packet_id);
			break;
		default:
			Log_i("Should NOT arrive here.");
			break;
	}
}

/**
 * MQTT��Ϣ���մ�����
 *
 * @param topicName         topic����
 * @param topicNameLen      topic����
 * @param message           �Ѷ�����Ϣ�Ľṹ
 * @param userData         ��Ϣ����
 */
static void on_message_callback(void *pClient, MQTTMessage *message, void *userData) {
	if (message == NULL) {
		return;
	}

	Log_i("Receive Message With topicName:%.*s, payload:%.*s",
		  (int) message->topic_len, message->ptopic, (int) message->payload_len, (char *) message->payload);
}

/**
 * ����MQTT connet��ʼ������
 *
 * @param initParams MQTT connet��ʼ������
 *
 * @return 0: ������ʼ���ɹ�  ��0: ʧ��
 */
static int _setup_connect_init_params(MQTTInitParams* initParams)
{
	int ret;
	
	ret = HAL_GetDevInfo((void *)&sg_devInfo);	
	if(QCLOUD_ERR_SUCCESS != ret){
		return ret;
	}
	
	initParams->device_name = sg_devInfo.device_name;
	initParams->product_id = sg_devInfo.product_id;

#ifdef AUTH_MODE_CERT
	/* ʹ�÷ǶԳƼ���*/
	char certs_dir[PATH_MAX + 1] = "certs";
	char current_path[PATH_MAX + 1];
	char *cwd = getcwd(current_path, sizeof(current_path));
	if (cwd == NULL)
	{
		Log_e("getcwd return NULL");
		return QCLOUD_ERR_FAILURE;
	}
	sprintf(sg_cert_file, "%s/%s/%s", current_path, certs_dir, sg_devInfo.devCertFileName);
	sprintf(sg_key_file, "%s/%s/%s", current_path, certs_dir, sg_devInfo.devPrivateKeyFileName);

	initParams->cert_file = sg_cert_file;
	initParams->key_file = sg_key_file;
#else
	initParams->device_secret = sg_devInfo.devSerc;
#endif

	initParams->command_timeout = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
	initParams->keep_alive_interval_ms = QCLOUD_IOT_MQTT_KEEP_ALIVE_INTERNAL;

	initParams->auto_connect_enable = 1;
	initParams->event_handle.h_fp = event_handler;
	initParams->event_handle.context = NULL;

    return QCLOUD_ERR_SUCCESS;
}

/**
 * ����topic��Ϣ
 *
 */
static int _publish_msg(void *client)
{
    char topicName[128] = {0};
	sprintf(topicName,"%s/%s/%s", sg_devInfo.product_id, sg_devInfo.device_name, "data");
	

    PublishParams pub_params = DEFAULT_PUB_PARAMS;
    pub_params.qos = QOS1;

    char topic_content[MAX_SIZE_OF_TOPIC_CONTENT + 1] = {0};

	int size = HAL_Snprintf(topic_content, sizeof(topic_content), "{\"action\": \"publish_test\", \"count\": \"%d\"}", sg_count++);
	if (size < 0 || size > sizeof(topic_content) - 1)
	{
		Log_e("payload content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_content));
		return -3;
	}

	pub_params.payload = topic_content;
	pub_params.payload_len = strlen(topic_content);

    return IOT_MQTT_Publish(client, topicName, &pub_params);
}

/**
 * ���Ĺ�עtopic��ע����Ӧ�ص�����
 *
 */
static int _register_subscribe_topics(void *client)
{
    static char topic_name[128] = {0};    
    int size = HAL_Snprintf(topic_name, sizeof(topic_name), "%s/%s/%s", sg_devInfo.product_id, sg_devInfo.device_name, "data");
	
	if (size < 0 || size > sizeof(topic_name) - 1)
    {
        Log_e("topic content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_name));
        return QCLOUD_ERR_FAILURE;
    }
    SubscribeParams sub_params = DEFAULT_SUB_PARAMS;
    sub_params.on_message_handler = on_message_callback;
    return IOT_MQTT_Subscribe(client, topic_name, &sub_params);
}

#ifdef LOG_UPLOAD

#define LOG_SAVE_FILE_PATH "./upload-fail-save.log"

/**
 * ��־�ϱ������û��Զ���ص������������ϱ�ʧ��ʱ�Ļ����ͨѶ�ָ�����ش�
 * sampleʾ�����ö�д�ļ��ķ�ʽ��������Ҫ���ļ�ϵͳ֧��
 */
//����ָ��������־������ʧ�Դ洢(�����ļ�)������ֵΪ�ɹ�д��ĳ���
static size_t _log_save_callback(const char *msg, size_t wLen)
{
    FILE *fp;
    size_t len;    

    if( ( fp = fopen(LOG_SAVE_FILE_PATH, "a+" ) ) == NULL ) {
        Log_e("fail to open file %s", LOG_SAVE_FILE_PATH);
        return 0;
    }    
    
    len = fwrite((void *)msg, 1, wLen, fp);
    Log_d("write %d of %d to log file", len, wLen);
    
    fclose(fp);

    return len;
}

//�ӷ���ʧ�Դ洢(�����ļ�)��ȡָ��������־������ֵΪ�ɹ���ȡ�ĳ���
static size_t _log_read_callback(char *buff, size_t rLen)
{
    FILE *fp;
    size_t len;    

    if( ( fp = fopen(LOG_SAVE_FILE_PATH, "r" ) ) == NULL ) {
        Log_e("fail to open file %s", LOG_SAVE_FILE_PATH);
        return 0;
    }

    len = fread((void *)buff, 1, rLen, fp);
    Log_d("read %d of %d from log file", len, rLen);
    
    fclose(fp);

    return len;
}

//�ӷ���ʧ�Դ洢(�����ļ�)ɾ���������־������ֵΪ0ɾ���ɹ�����0ɾ��ʧ��
static int _log_del_callback(void)
{
    return remove(LOG_SAVE_FILE_PATH);
}

//��ȡ�洢�ڷ���ʧ�Դ洢(�����ļ�)�е�log���ȣ�����0Ϊû�л���
static size_t _log_get_size_callback()
{
    long length;
    FILE *fp;

    /* check if file exists */
    if (access(LOG_SAVE_FILE_PATH, 0))
        return 0;
    
    if( ( fp = fopen(LOG_SAVE_FILE_PATH, "r" ) ) == NULL ) {
        Log_e("fail to open file %s", LOG_SAVE_FILE_PATH);
        return 0;
    }    
    
    fseek(fp, 0L, SEEK_END);
    length = ftell(fp);
    fclose(fp);
    if (length > 0)
        return (size_t)length;
    else
        return 0;
}

#endif



static void mqtt_basic_thread(void) 
{
	int rc;
	
	Log_i("mqtt_sample start");
	
    //init connection
    MQTTInitParams init_params = DEFAULT_MQTTINIT_PARAMS;
    rc = _setup_connect_init_params(&init_params);
	if (rc != QCLOUD_ERR_SUCCESS) 
	{
		Log_e("init params err,rc=%d", rc);
		return;
	}
	
#ifdef LOG_UPLOAD
    // IOT_Log_Init_Uploader should be done after _setup_connect_init_params
    LogUploadInitParams log_init_params = {.product_id = sg_devInfo.product_id, 
                        .device_name = sg_devInfo.device_name, .sign_key = NULL,
                        .read_func = _log_read_callback, .save_func = _log_save_callback,
                        .del_func = _log_del_callback, .get_size_func = _log_get_size_callback};
#ifdef AUTH_MODE_CERT    
    log_init_params.sign_key = sg_cert_file;
#else
    log_init_params.sign_key = sg_devInfo.devSerc;
#endif
    IOT_Log_Init_Uploader(&log_init_params);
#endif	

    void *client = IOT_MQTT_Construct(&init_params);
    if (client != NULL) 
	{
        Log_i("Cloud Device Construct Success");
    } 
	else 
    {
        Log_e("Cloud Device Construct Failed");
        goto end;
    }


	
#ifdef SYSTEM_COMM
    long time = 0;

	rc = IOT_SYSTEM_GET_TIME(client, &time);
	if (QCLOUD_ERR_SUCCESS == rc){
		Log_i("the time is %ld", time);
	}
	else{
		Log_e("get system time failed!");
	}
#endif


	//register subscribe topics here
    rc = _register_subscribe_topics(client);
    if (rc < 0) 
	{
        Log_e("Client Subscribe Topic Failed: %d", rc);
        goto end;
    }
	else
	{
		Log_d("Subscribe Topic success");
	}

	Log_d("Start mqtt Loop");
	
	running_state = 1;
    do 
	{

    	rc = IOT_MQTT_Yield(client, 3000);
		if (rc == QCLOUD_ERR_MQTT_ATTEMPTING_RECONNECT) 
		{
			HAL_SleepMs(1000);
			continue;
		}
		else if (rc != QCLOUD_ERR_SUCCESS && rc != QCLOUD_ERR_MQTT_RECONNECTED)
		{
			Log_e("exit with error: %d", rc);
			break;
		}

		// �ȴ����Ľ��
		if (sg_sub_packet_id > 0) 
		{
			rc = _publish_msg(client);
			if (rc < 0) 
			{
				Log_e("client publish topic failed :%d.", rc);
				break;
			}
		}

		HAL_SleepMs(1000);

    } while (running_state);
	
end:	
	 //ע������쳣�˳����
	rc = IOT_MQTT_Destroy(&client);  
	Log_e("Something goes wrong or stoped");  
    IOT_Log_Upload(true);
	running_state = 0;
	
    return;
}

static int tc_mqtt_basic_example(int argc, char **argv)
{
    rt_thread_t tid;
    int stack_size = MQTT_BASIC_THREAD_STACK_SIZE;
	
    //init log level
    IOT_Log_Set_Level(DEBUG);
    IOT_Log_Set_MessageHandler(log_handler);

	if (2 == argc)
	{
		if (!strcmp("start", argv[1]))
		{
			if (1 == running_state)
			{
				Log_d("tc_mqtt_basic_example is already running\n");
				return 0;
			}			
		}
		else if (!strcmp("stop", argv[1]))
		{
			if (0 == running_state)
			{
				Log_d("tc_mqtt_basic_example is already stopped\n");
				return 0;
			}
			running_state = 0;
			return 0;
		}
		else
		{
			Log_d("Usage: tc_mqtt_basic_example start/stop");
			return 0;			  
		}
	}
	else
	{
		Log_e("Para err, usage: tc_mqtt_basic_example start/stop");
		return 0;
	}
	
	tid = rt_thread_create("mqtt_basic", (void (*)(void *))mqtt_basic_thread, 
							NULL, stack_size, RT_THREAD_PRIORITY_MAX / 2 - 1, 10);  

    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }

    return 0;
}


#ifdef RT_USING_FINSH
#include <finsh.h>
FINSH_FUNCTION_EXPORT(tc_mqtt_basic_example, startup mqtt basic example);
#endif

#ifdef FINSH_USING_MSH
MSH_CMD_EXPORT(tc_mqtt_basic_example, startup mqtt basic example);
#endif

