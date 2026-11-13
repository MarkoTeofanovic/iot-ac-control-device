/* 
    IR daljinski: prima IR kod na GPIO4, cuva ga u RAM i NVS
     i salje ga na GPIO12 (38 kHz noseci signal).
*/
#include "ir_remote.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h" // NVS API iz ESP-IDF (nvs_open, nvs_set_blob...)

#define IR_RX_GPIO          GPIO_NUM_4
#define IR_TX_GPIO          GPIO_NUM_12
#define IR_RESOLUTION_HZ    1000000   // 1 tick 
#define IR_CARRIER_HZ       38000     // standardna IR frekvencija
#define IR_MAX_SYMBOLS      512       // dovoljno i za duze kodove (klima!)
#define IR_MIN_SYMBOLS      8         // kraci signali su smetnje

#define IR_NVS_NAMESPACE    "ir_remote"
#define IR_NVS_KEY          "code"

static const char *TAG = "IR_REMOTE";

static rmt_channel_handle_t s_rx_chan;
static rmt_channel_handle_t s_tx_chan;
static rmt_encoder_handle_t s_copy_encoder;
static QueueHandle_t s_rx_queue;

// bafer u koji RMT upisuje primljeni signal
static rmt_symbol_word_t s_rx_buffer[IR_MAX_SYMBOLS];

// kod u RAM-u (pripremljen za slanje)
static rmt_symbol_word_t s_code[IR_MAX_SYMBOLS];
static size_t s_code_len;

static bool IRAM_ATTR rx_done_cb(rmt_channel_handle_t chan,
                                 const rmt_rx_done_event_data_t *edata,
                                 void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)user_ctx, edata, &woken);
    return woken == pdTRUE;
}

esp_err_t ir_remote_init(void)
{
    // input (GPIO4)
    rmt_rx_channel_config_t rx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .gpio_num = IR_RX_GPIO,
    };
    ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&rx_cfg, &s_rx_chan), TAG, "rx kanal");

    // prijemnik ima izlaz "open collector" kod nekih modula
    gpio_pullup_en(IR_RX_GPIO);

    s_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (s_rx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    rmt_rx_event_callbacks_t cbs = { .on_recv_done = rx_done_cb };
    ESP_RETURN_ON_ERROR(rmt_rx_register_event_callbacks(s_rx_chan, &cbs, s_rx_queue),
                        TAG, "rx callback");
    ESP_RETURN_ON_ERROR(rmt_enable(s_rx_chan), TAG, "rx enable");

    // slanje (GPIO12)
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = IR_TX_GPIO,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_tx_chan), TAG, "tx kanal");

    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = IR_CARRIER_HZ,
        .duty_cycle = 0.33,
    };
    ESP_RETURN_ON_ERROR(rmt_apply_carrier(s_tx_chan, &carrier_cfg), TAG, "carrier");

    rmt_copy_encoder_config_t enc_cfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&enc_cfg, &s_copy_encoder), TAG, "encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_tx_chan), TAG, "tx enable");

    ESP_LOGI(TAG, "IR inicijalizovan (prijem GPIO%d, slanje GPIO%d)",
             IR_RX_GPIO, IR_TX_GPIO);
    return ESP_OK;
}

esp_err_t ir_remote_load(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(IR_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err; // ESP_ERR_NVS_NOT_FOUND ako namespace ne postoji
    }

    size_t size = sizeof(s_code);
    err = nvs_get_blob(nvs, IR_NVS_KEY, s_code, &size);
    nvs_close(nvs);

    if (err != ESP_OK) {
        s_code_len = 0;
        return err;
    }

    s_code_len = size / sizeof(rmt_symbol_word_t);
    ESP_LOGI(TAG, "Ucitan IR kod iz NVS (%u simbola)", (unsigned)s_code_len);
    return ESP_OK;
}

static esp_err_t save_to_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(IR_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(nvs, IR_NVS_KEY, s_code, s_code_len * sizeof(rmt_symbol_word_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t ir_remote_learn(uint32_t timeout_ms)
{
    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = 1250,      // kraci signal je smetnja
        .signal_range_max_ns = 15000000,  // 15 ms nicega sledi kraj signala
    };

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    xQueueReset(s_rx_queue);

    ESP_LOGI(TAG, "Cekam IR signal %u s... uperi daljinski u prijemnik i pritisni dugme",
             (unsigned)(timeout_ms / 1000));

    while (1) {
        ESP_RETURN_ON_ERROR(rmt_receive(s_rx_chan, s_rx_buffer, sizeof(s_rx_buffer), &rx_cfg),
                            TAG, "rmt_receive");

        TickType_t now = xTaskGetTickCount();
        TickType_t wait = (deadline > now) ? (deadline - now) : 0;

        rmt_rx_done_event_data_t rx_data;
        if (xQueueReceive(s_rx_queue, &rx_data, wait) != pdTRUE) {
            // isteklo vreme zaustavlja prijem 
            rmt_disable(s_rx_chan);
            rmt_enable(s_rx_chan);
            ESP_LOGW(TAG, "IR signal NIJE primljen u roku od %u s",
                     (unsigned)(timeout_ms / 1000));
            return ESP_ERR_TIMEOUT;
        }

        if (rx_data.num_symbols < IR_MIN_SYMBOLS) {
            ESP_LOGD(TAG, "Ignorisana smetnja (%u simbola)", (unsigned)rx_data.num_symbols);
            continue; // pokusaj ponovo dok ne istekne vreme
        }

        /* 
            prijemnik ima obrnut izlaz: LOW kada vidi IR svetlo!
            za slanje je potrebno obrnuto: HIGH = dioda svetli logicno(sa 38 kHz)
            zato ovde obrcemo nivoe pre cuvanja
        */
        s_code_len = rx_data.num_symbols;
        for (size_t i = 0; i < s_code_len; i++) {
            s_code[i] = rx_data.received_symbols[i];
            s_code[i].level0 = !s_code[i].level0;
            s_code[i].level1 = !s_code[i].level1;
        }

        printf("\n*** IR SIGNAL PRIMLJEN! (%u simbola) ***\n\n", (unsigned)s_code_len);

        esp_err_t err = save_to_nvs();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "IR kod sacuvan u RAM i NVS");
        } else {
            ESP_LOGE(TAG, "IR kod je u RAM-u, ali cuvanje u NVS nije uspelo: %s",
                     esp_err_to_name(err));
        }
        return ESP_OK;
    }
}

bool ir_remote_has_code(void)
{
    return s_code_len > 0;
}

esp_err_t ir_remote_send(void)
{
    if (s_code_len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0, // posalji jednom
    };
    ESP_RETURN_ON_ERROR(rmt_transmit(s_tx_chan, s_copy_encoder, s_code,
                                     s_code_len * sizeof(rmt_symbol_word_t), &tx_cfg),
                        TAG, "rmt_transmit");
    return rmt_tx_wait_all_done(s_tx_chan, 1000);
}
