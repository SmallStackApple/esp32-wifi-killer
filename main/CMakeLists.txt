idf_component_register(SRCS "wifi_deauther.c" "beacon_spammer.c" "main.c"
                    INCLUDE_DIRS "."
                    )

target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)