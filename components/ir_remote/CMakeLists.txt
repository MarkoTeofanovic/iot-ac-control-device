# TEST komponenta: IR prijemnik (GPIO4) + IR emiter (GPIO12)
idf_component_register(SRCS "ir_remote.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_driver_rmt esp_driver_gpio nvs_flash)
