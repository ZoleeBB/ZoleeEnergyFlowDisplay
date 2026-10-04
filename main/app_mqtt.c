#include "app_mqtt.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "freertos/event_groups.h"
#include "app_events.h"
#include "app_payload.h"
#include "app_state.h"
#include "app_stats.h"

#define MQTT_CONNECTED_BIT BIT0
#define MQTT_MAX_PAYLOAD_BYTES 4096
#define MQTT_CLIENT_ID "zolee-energy-flow-display"
static const char *TAG="mqtt";
static app_mqtt_config_t s_cfg;
static esp_mqtt_client_handle_t s_client;
static EventGroupHandle_t s_events;
static char s_rx[MQTT_MAX_PAYLOAD_BYTES+1],s_rx_topic[APP_TOPIC_MAX];
static int s_rx_total;
static int64_t s_last_invalid_event_us;
static int64_t s_last_transport_event_us;

static bool topic_matches(const char *t,int len){size_t n=strlen(s_cfg.topic);return t&&len==(int)n&&memcmp(t,s_cfg.topic,n)==0;}
static void reset_rx(void){s_rx_total=0;s_rx[0]=0;s_rx_topic[0]=0;}
static void complete(void){app_energy_sample_t s;char err[160];if(app_payload_parse(s_rx,(size_t)s_rx_total,&s,err,sizeof(err))==ESP_OK){app_state_set_sample(&s);app_stats_process_sample(&s);}else{ESP_LOGW(TAG,"Rejected MQTT payload: %s",err);int64_t now=esp_timer_get_time();if(now-s_last_invalid_event_us>60000000LL){app_events_add(APP_EVENT_WARNING,"Hibás MQTT adatcsomag elutasítva: %s",err);s_last_invalid_event_us=now;}}}
static void on_data(esp_mqtt_event_handle_t e){
    if(e->current_data_offset==0){
        reset_rx();
        if(!topic_matches(e->topic,e->topic_len))return;
        if(e->total_data_len<=0||e->total_data_len>MQTT_MAX_PAYLOAD_BYTES){
            int64_t now=esp_timer_get_time();
            if(now-s_last_invalid_event_us>60000000LL){
                app_events_add(APP_EVENT_WARNING,"MQTT payload mérete érvénytelen: %d byte",e->total_data_len);
                s_last_invalid_event_us=now;
            }
            return;
        }
        size_t n=e->topic_len;
        if(n>=sizeof(s_rx_topic))n=sizeof(s_rx_topic)-1;
        memcpy(s_rx_topic,e->topic,n);
        s_rx_topic[n]=0;
        s_rx_total=e->total_data_len;
    }
    if (s_rx_total <= 0 || !s_rx_topic[0]) {
        return;
    }
    if (e->current_data_offset < 0 || e->data_len < 0 ||
        e->current_data_offset + e->data_len > s_rx_total) {
        reset_rx();
        return;
    }
    memcpy(s_rx+e->current_data_offset,e->data,(size_t)e->data_len);if(e->current_data_offset+e->data_len==s_rx_total){s_rx[s_rx_total]=0;complete();reset_rx();}
}
static void handler(void *arg,esp_event_base_t base,int32_t id,void *data){(void)arg;(void)base;esp_mqtt_event_handle_t e=(esp_mqtt_event_handle_t)data;switch((esp_mqtt_event_id_t)id){
    case MQTT_EVENT_CONNECTED:{
        app_state_set_mqtt_connected(true);
        xEventGroupSetBits(s_events,MQTT_CONNECTED_BIT);
        int message_id=esp_mqtt_client_subscribe(s_client,s_cfg.topic,1);
        if(message_id<0){
            ESP_LOGE(TAG,"MQTT subscribe failed for topic %s",s_cfg.topic);
            app_events_add(APP_EVENT_ERROR,"MQTT feliratkozás sikertelen: %s",s_cfg.topic);
        }else{
            ESP_LOGI(TAG,"Subscribed to %s, msg_id=%d",s_cfg.topic,message_id);
        }
        app_events_add(APP_EVENT_INFO,"MQTT broker kapcsolat létrejött.");
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        app_state_set_mqtt_connected(false);
        xEventGroupClearBits(s_events,MQTT_CONNECTED_BIT);
        app_events_add(APP_EVENT_WARNING,"MQTT broker kapcsolat megszakadt.");
        reset_rx();
        break;
    case MQTT_EVENT_DATA:
        on_data(e);
        break;
    case MQTT_EVENT_ERROR:
        if(e->error_handle){
            ESP_LOGW(TAG,"MQTT error type=%d esp=%s sock=%d",
                     e->error_handle->error_type,
                     esp_err_to_name(e->error_handle->esp_tls_last_esp_err),
                     e->error_handle->esp_transport_sock_errno);
            int64_t now=esp_timer_get_time();
            if(now-s_last_transport_event_us>60000000LL){
                app_events_add(APP_EVENT_WARNING,
                               "MQTT transport hiba: %s, socket=%d",
                               esp_err_to_name(e->error_handle->esp_tls_last_esp_err),
                               e->error_handle->esp_transport_sock_errno);
                s_last_transport_event_us=now;
            }
        }
        break;
    default:break;}}
esp_err_t app_mqtt_start(const app_config_t *cfg){if(!cfg)return ESP_ERR_INVALID_ARG;if(s_client)return ESP_ERR_INVALID_STATE;s_cfg=cfg->mqtt;s_events=xEventGroupCreate();if(!s_events)return ESP_ERR_NO_MEM;
    esp_mqtt_client_config_t c={.broker.address.hostname=s_cfg.broker,.broker.address.port=s_cfg.port,.broker.address.transport=MQTT_TRANSPORT_OVER_TCP,.credentials.client_id=MQTT_CLIENT_ID,.network.reconnect_timeout_ms=(int)(s_cfg.reconnect_seconds*1000U),.network.timeout_ms=10000,.task.stack_size=6144,.buffer.size=2048};
    if (s_cfg.username[0]) {
        c.credentials.username = s_cfg.username;
    }
    if (s_cfg.password[0]) {
        c.credentials.authentication.password = s_cfg.password;
    }

    s_client = esp_mqtt_client_init(&c);
    if (!s_client) {
        return ESP_FAIL;
    }

    esp_err_t e = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, handler, NULL);
    if (e != ESP_OK) {
        return e;
    }
    return esp_mqtt_client_start(s_client);
}
bool app_mqtt_wait_connected(TickType_t t){if(!s_events)return false;return(xEventGroupWaitBits(s_events,MQTT_CONNECTED_BIT,pdFALSE,pdTRUE,t)&MQTT_CONNECTED_BIT)!=0;}
