/*
OSCAR OVALLES VELOZ 2019-8238
THAYS NIVAR 2022-0459
DEVIDSON MATOS 2019-8286
*/
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_ota_ops.h"
#include <sys/param.h>
#include <freertos/FreeRTOS.h>
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"

#define STACK_SIZE 4096
#define FALSE 0
#define TRUE 1
#define PIN_LED 2
#define STATE_1 1
#define STATE_2 2
#define STATE_3 3
#define STATE_4 4
#define STATE_5 5
#define STATE_GOBACK 6

struct GLOBAL_CONTROL
{
    int PICHARDO_BLINK;
    unsigned int BPP : 1;
    unsigned int flanco : 1;

} gen_val;

uint8_t level;
int Current_state = 0;
int Next_state = 0;
// timers
int interval = 0;
int timerId = 1;
TimerHandle_t xTimers;
// declaracion de las funciones
esp_err_t create_tasks(void);
esp_err_t config_pins(void);
esp_err_t set_timer(void);
void BOTON_FUNC();
void vTaskOUTPUT_MQTT();
void vTaskOUTPUT_PHYSICAL();
void vTaskSTATE();

// Variables utilizadas para la creación de ñas colas que intercambiarán datos entre las diferentes tareas
QueueHandle_t xQueue1 = 0; // Declaración de la cola de mensajes que se utilizará para enviar datos desde la tarea de entrada hacia la tarea del led
QueueHandle_t xQueue2 = 0; // Declaración de la cola de mensajes que se utilizará para enviar datos desde la tarea de entrada hacia la tarea de salida
char tag[5];
int msg_mqtt = 0;
static const char *TAG = "mqtts_example";
static void mqtt_event_handler();
esp_mqtt_client_handle_t client;

void vTimerCallback(TimerHandle_t pxtimer)
{

    if (Current_state == STATE_1)
    {
        gen_val.PICHARDO_BLINK = FALSE;
        xQueueSend(xQueue2, &gen_val.PICHARDO_BLINK, pdMS_TO_TICKS(100));
        gpio_set_level(PIN_LED, FALSE);
    }
    if (Current_state != STATE_1)
    {
        gen_val.PICHARDO_BLINK = !gen_val.PICHARDO_BLINK;
        xQueueSend(xQueue2, &gen_val.PICHARDO_BLINK, pdMS_TO_TICKS(100));
        gpio_set_level(PIN_LED, gen_val.PICHARDO_BLINK);
    }
    printf("El estado del led es: %i\n", gen_val.PICHARDO_BLINK);
    ESP_LOGI(tag, "El valor de intervalos es %i", interval);
}

//
// Note: this function is for testing purposes only publishing part of the active partition
//       (to be checked against the original binary)
//

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/topic/BPP", 0);
        ESP_LOGI(TAG, "Enviado subscribe exitoso, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/STATE_ACT", 0);
        ESP_LOGI(TAG, "Enviado subscribe exitoso, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/ledblink", 0);
        ESP_LOGI(TAG, "Enviado subscribe exitoso, msg_id=%d", msg_id);

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:

        char topico_MQTT[100];
        strncpy(topico_MQTT, event->topic, event->topic_len);
        topico_MQTT[event->topic_len] = '\0';

        char dato_MQTT[100];
        strncpy(dato_MQTT, event->data, event->data_len);
        dato_MQTT[event->data_len] = '\0';

        if ((strcmp(topico_MQTT, "/topic/BPP")) == (strcmp(dato_MQTT, "1")))
        {
            ESP_LOGI(TAG, "Sending the binary");
            gen_val.BPP = TRUE;
            strcpy(dato_MQTT, "0");
        }

        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGI(TAG, "Last captured errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        }
        else
        {
            ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = CONFIG_BROKER_URI},
    };

    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

int msg_mqtt;

void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    xQueue1 = xQueueCreate(10, sizeof(uint32_t));
    xQueue2 = xQueueCreate(10, sizeof(uint32_t));
    create_tasks();
    ESP_LOGI(tag, "Task creada");
    gpio_reset_pin(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    mqtt_app_start();

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000 / portTICK_PERIOD_MS));
        ESP_LOGI(tag, "sigo vivo");
    }
}

esp_err_t create_tasks(void)
{
    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;
    ESP_LOGI(tag, "comienza tareas");
    xTaskCreate(vTaskOUTPUT_MQTT,
                "vTask_MQTT",
                STACK_SIZE,
                &ucParameterToPass,
                1,
                &xHandle);

    xTaskCreate(vTaskSTATE,
                "vTask_PHYSICAL",
                STACK_SIZE,
                &ucParameterToPass,
                1,
                &xHandle);

    xTaskCreate(vTaskOUTPUT_PHYSICAL,
                "vTask_STATES",
                STACK_SIZE,
                &ucParameterToPass,
                1,
                &xHandle);

    ESP_LOGI(tag, "TASKS READY");

    return ESP_OK;
}

void vTaskOUTPUT_MQTT(void *pvParameters)
{

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        if (Current_state == STATE_1)
        {
           ESP_LOGI(tag,"ESTADO 1\n");
            msg_mqtt = esp_mqtt_client_publish(client, "/topic/STATE_ACT", "OFF", 0, 0, 0);
        }

        if (Current_state == STATE_2)
        {
            ESP_LOGI(tag,"ESTADO 2\n");
            msg_mqtt = esp_mqtt_client_publish(client, "/topic/STATE_ACT", "ESTADO 2", 0, 0, 0);
        }

        if (Current_state == STATE_3)
        {
            ESP_LOGI(tag,"ESTADO 3\n");
            msg_mqtt = esp_mqtt_client_publish(client, "/topic/STATE_ACT", "ESTADO 3", 0, 0, 0);
        }

        if (Current_state == STATE_4)
        {
            ESP_LOGI(tag,"ESTADO 4\n");
            msg_mqtt = esp_mqtt_client_publish(client, "/topic/STATE_ACT", "ESTADO 4", 0, 0, 0);
        }

        if (Current_state == STATE_5)
        {
            ESP_LOGI(tag,"ESTADO 5\n");
            msg_mqtt = esp_mqtt_client_publish(client, "/topic/STATE_ACT", "ESTADO 5", 0, 0, 0);
        }
    }
}

void vTaskOUTPUT_PHYSICAL(void *pvParameters)
{

    unsigned int value_led = 0;

    for (;;)
    {
        xQueueReceive(xQueue2, &value_led, pdMS_TO_TICKS(10));
        if (value_led == TRUE)
        {
            msg_mqtt = esp_mqtt_client_publish(client, "/topic/ledblink", "1", 0, 0, 0);
        }

        if (value_led == FALSE)
        {
            msg_mqtt = esp_mqtt_client_publish(client, "/topic/ledblink", "0", 0, 0, 0);
        }
    }
}

void vTaskSTATE(void *pvParameters)
{
    Current_state = STATE_1;
    Next_state = STATE_2;
    gen_val.BPP = FALSE;
    gen_val.flanco = FALSE;

    ESP_LOGI(tag, "ENTRANDO A LA MAQUINA DE ESTADOS");
    ESP_LOGI(tag, "READING DATA PIN");

    msg_mqtt = esp_mqtt_client_publish(client, "/topic/BPP", "", 0, 0, 0);
    for (;;)
    {

        if (gen_val.BPP == TRUE)
        {
            ESP_LOGI(tag, "DENTRO DE LA MAQUINA DE ESTADOS");
            gen_val.BPP = FALSE;
            Current_state++;
            printf("el valor del conteo de estado es: %i\n", Current_state);
            Next_state++;
            printf("el valor del conteo de estado siguiente es: %i\n", Next_state);

            if (Current_state == STATE_2 && Next_state == STATE_3)
            {
                // TIMERS SET LED 0.5S
                interval = 500;
                vTaskDelay(pdMS_TO_TICKS(100));
                set_timer();
                vTaskDelay(pdMS_TO_TICKS(100));
                xQueueSend(xQueue1, &Current_state, pdMS_TO_TICKS(100));
                ESP_LOGI(tag, "STATE 2");
            }
            if (Current_state == STATE_3 && Next_state == STATE_4)
            {
                ////// TIMERS SET LED 0.1S
                interval = 100;
                vTaskDelay(pdMS_TO_TICKS(100));
                set_timer();
                vTaskDelay(pdMS_TO_TICKS(100));
                xQueueSend(xQueue1, &Current_state, pdMS_TO_TICKS(100));
                ESP_LOGI(tag, "STATE 3");
            }

            if (Current_state == STATE_4 && Next_state == STATE_5)
            {
                ////// TIMERS SET LED 1S
                interval = 1000;
                vTaskDelay(pdMS_TO_TICKS(100));
                set_timer();
                vTaskDelay(pdMS_TO_TICKS(100));
                xQueueSend(xQueue1, &Current_state, pdMS_TO_TICKS(100));
                ESP_LOGI(tag, "STATE 4");
            }

            if (Current_state == STATE_5 && Next_state == STATE_1)
            {
                xQueueSend(xQueue1, &Current_state, pdMS_TO_TICKS(100));
                vTaskDelay(pdMS_TO_TICKS(250));
                ESP_LOGI(tag, "STATE 5");
            }

            if (Current_state == 6 && Next_state == 7)
            {
                Current_state = STATE_1;
                Next_state = STATE_2;
                vTaskDelay(pdMS_TO_TICKS(250));
                interval=0;
                vTaskDelay(pdMS_TO_TICKS(250));
                set_timer();
                ESP_LOGI(tag, "STATE 6");
                xQueueSend(xQueue1, &Current_state, pdMS_TO_TICKS(100));
            }
        }

        if (Current_state == STATE_5 && gen_val.BPP == FALSE)
        {
            vTaskDelay(pdMS_TO_TICKS(200));

            if (interval == 1000)
            {
                vTaskDelay(pdMS_TO_TICKS(250));
                interval = 100;
                set_timer();
            }
            else if (interval != 1000)
            {
                vTaskDelay(pdMS_TO_TICKS(250));
                interval = interval + 100;
                set_timer();
            }
        }
    }
}

esp_err_t set_timer()
{
    ESP_LOGI(tag, "config timer");
    xTimers = xTimerCreate("Timer",                   // Just a text name, not used by the kernel.
                           (pdMS_TO_TICKS(interval)), // The timer period in ticks.
                           pdTRUE,                    // The timers will auto-reload themselves when they expire.
                           (void *)timerId,           // Assign each timer a unique id equal to its array index.
                           vTimerCallback             // Each timer calls the same callback when it expires.
    );

    if (xTimers == NULL)
    {
        // The timer was not created.
        ESP_LOGI(tag, "The timer was not created.");
    }
    else
    {
        // Start the timer.  No block time is specified, and even if one was
        // it would be ignored because the scheduler has not yet been
        // started.
        if (xTimerStart(xTimers, 0) != pdPASS)
        {
            // The timer could not be set into the Active state.
            ESP_LOGI(tag, "The timer could not be set into the Active state.");
        }
    }

    return ESP_OK;
}
