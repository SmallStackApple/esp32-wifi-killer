idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES 
                        esp_wifi 
                        esp_netif
                        driver
                        freertos
                        nvs_flash
                    )

target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)