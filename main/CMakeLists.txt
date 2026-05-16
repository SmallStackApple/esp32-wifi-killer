idf_component_register(SRCS "deauth.c" "twins.c" "beacon_spammer.c" "fake_ap.c" "main.c"
                    INCLUDE_DIRS "."
                    )

target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)