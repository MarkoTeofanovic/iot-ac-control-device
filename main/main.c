#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pir.h"
#include "buzzer.h"
#include "dht11.h" //senzor toplote
#include "esp_log.h"
#include "wifi_manager.h"
#include "http_client.h"
#include "esp_log.h"
#include "services.h"
#include "nvs.h"

#include "driver/gpio.h" // TEST IR emiter
#include "ir_remote.h"   // TEST IR prijemnik + emiter

// TEST IR daljinski podesavanja
#define IR_TEST_LEARN_TIMEOUT_MS  30000   // cekanje prijema
#define IR_TEST_SEND_PERIOD_MS    10000   // delay testa slanja
#define IR_TEST_TEMP_LIMIT_C      30.0f   // temp treshold kao parametar

static const char *TAG = "MAIN";

/*  
    IR emiter test, spam 500 ms upaljen / 500 ms ugasen
    GPIO12
*/
#define IR_TEST_GPIO GPIO_NUM_12

static void ir_test_task(void *arg)
{
    int level = 0;

    while (1)
    {
        level = !level;
        gpio_set_level(IR_TEST_GPIO, level);
        ESP_LOGI("IR_TEST", "IR LED %s", level ? "UPALJEN" : "UGASEN");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void ir_test_start(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << IR_TEST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    // ovako da bi radio i dht i emiter test
    xTaskCreate(ir_test_task, "ir_test", 2048, NULL, 5, NULL);
    ESP_LOGW(TAG, "TEST: IR emiter trepce na GPIO12");
}



void app_main(void)
{
    //ir_test_start(); // TEST IR emiter 

    // esp_err_t err = initialize_nvs();

    // if (err == ESP_OK)
    // {
    //     ESP_LOGI(TAG, "NVS je inicijalizovan.");
    // }
    // else
    // {
    //     ESP_LOGW(TAG, "Ne mogu da inicijalizujem NVS.");
    //     return;
    // }

    // ESP_ERROR_CHECK(wifi_manager_init());

    // ESP_ERROR_CHECK(wifi_manager_start());

    // ESP_LOGI(TAG, "Cekam Wi-Fi konekciju...");

    // err = wifi_manager_wait_connected(15000);

    // if (err == ESP_OK)
    // {
    //     ESP_LOGI(TAG, "Wi-Fi povezan; uredjaj ima IP adresu");
    // }
    // else
    // {
    //     ESP_LOGW(TAG, "Wi-Fi jos nije povezan; aplikacija nastavlja rad");
    // }
//}

    /* 
        TEST KOD IR DALJINSKI 
        1. NVS mora biti inicijalizovan da bi se IR kod sacuvao
        2. ucitava se ranije sacuvan kod (ako postoji)
        3. 30 s se ceka novi IR signal, ako stigne, ispisuje se poruka i
        kod se cuva u RAM i NVS (zamenjuje stari).
    */
    ESP_ERROR_CHECK(initialize_nvs());               
    ESP_ERROR_CHECK(ir_remote_init());               

    if (ir_remote_load() != ESP_OK)                  
    {
        ESP_LOGI(TAG, "TEST: nema sacuvanog IR koda u NVS");
    }

    if (ir_remote_learn(IR_TEST_LEARN_TIMEOUT_MS) == ESP_OK)  
    {
        ESP_LOGI(TAG, "TEST: IR kod primljen i sacuvan");
    }
    else if (ir_remote_has_code())
    {
        ESP_LOGW(TAG, "TEST: novi kod nije primljen, koristi se kod iz NVS");
    }
    else
    {
        ESP_LOGW(TAG, "TEST: nema IR koda - slanje nece raditi");
    }

    TickType_t ir_last_send = 0;                     
    bool ir_sent_once = false;                       
    



    dht11_init();
    ESP_LOGI(TAG, "DHT11 senzor je inicijalizovan");

    while (1)
    {

        float temperature;
        float humidity;

        esp_err_t err_dht = dht11_read(&temperature, &humidity);

        if (err_dht == ESP_OK)
        {
            ESP_LOGI(TAG,
                     "Temperatura: %.1f C | Vlaznost: %.1f %%",
                     (double)temperature,
                     (double)humidity);
        }
        else
        {
            ESP_LOGE(TAG, "Greska citanja dht11: %s",
                     esp_err_to_name(err_dht));
            continue;
        }

        /* 
            TEST KOD IR DALJINSKI slanje ako je ispunjen uslov,
            ako je temperatura ispod 30 C, salje se sacuvani IR kod
            na svakih 10 s 
        */
        if (temperature < IR_TEST_TEMP_LIMIT_C && ir_remote_has_code())
        {
            TickType_t now = xTaskGetTickCount();

            if (!ir_sent_once ||
                (now - ir_last_send) >= pdMS_TO_TICKS(IR_TEST_SEND_PERIOD_MS))
            {
                esp_err_t ir_err = ir_remote_send();

                if (ir_err == ESP_OK)
                {
                    ESP_LOGI(TAG, "TEST: IR kod poslat (temperatura %.1f C < %.0f C)",
                             (double)temperature, (double)IR_TEST_TEMP_LIMIT_C);
                }
                else
                {
                    ESP_LOGE(TAG, "TEST: slanje IR koda nije uspelo: %s",
                             esp_err_to_name(ir_err));
                }

                ir_last_send = now;
                ir_sent_once = true;
            }
        }

        
        if (wifi_manager_is_connected())
        {
            ESP_LOGI(TAG, "Wi-Fi status: povezan");

            esp_err_t https_err =
                send_temperature_and_humidity(
                    temperature,
                    humidity);

            if (https_err == ESP_OK)
            {
                ESP_LOGI(TAG, "Slanje podataka uspesno");
            }
            else
            {
                ESP_LOGE(
                    TAG,
                    "Slanje podataka nije uspelo: %s",
                    esp_err_to_name(https_err));
            }
        }
        else
        {
            ESP_LOGW(TAG, "Wi-Fi status: nije povezan");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
