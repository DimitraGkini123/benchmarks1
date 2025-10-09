idf_component_register(
    SRCS "blink_example_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_timer freertos
)

# --- Manual include of perfmon headers (since it's not registered automatically) ---
set(EXTRA_COMPONENT_DIRS "C:/Users/Dimitra/esp/v5.5.1/esp-idf/components/perfmon")
include_directories("C:/Users/Dimitra/esp/v5.5.1/esp-idf/components/perfmon/include")
