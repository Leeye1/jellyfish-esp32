idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver led_strip network hw_config actuator buzzer rgb_led rs485 sensors esp_timer esp_wifi esp_event nvs_flash mqtt)
