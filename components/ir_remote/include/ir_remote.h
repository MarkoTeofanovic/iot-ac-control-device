/* ============================ TEST KOMPONENTA ============================
 * IR daljinski - ucenje i ponovno slanje jednog IR koda.
 *
 *  - IR prijemnik (npr. TSOP/VS1838, 38 kHz) na GPIO4
 *  - IR emiter (dioda preko otpornika) na GPIO12
 *
 * Primljeni kod se cuva u RAM-u i u NVS (flash), pa ostaje sacuvan
 * i posle restarta. Slanje koristi 38 kHz noseci signal.
 *
 * Pre koriscenja mora biti pozvan initialize_nvs().
 * ========================================================================= */
#ifndef IR_REMOTE_H
#define IR_REMOTE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Inicijalizuje RMT prijem (GPIO4) i slanje (GPIO12).
esp_err_t ir_remote_init(void);

// Ucitava ranije sacuvan kod iz NVS u RAM.
// Vraca ESP_ERR_NVS_NOT_FOUND ako kod jos nije sacuvan.
esp_err_t ir_remote_load(void);

// Ceka IR signal najvise timeout_ms milisekundi.
// Kada ga primi, cuva ga u RAM i NVS i vraca ESP_OK.
// Ako signal ne stigne, vraca ESP_ERR_TIMEOUT.
esp_err_t ir_remote_learn(uint32_t timeout_ms);

// Da li u RAM-u postoji kod koji moze da se posalje.
bool ir_remote_has_code(void);

// Salje sacuvani kod preko IR emitera.
esp_err_t ir_remote_send(void);

#endif
