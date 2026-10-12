# Q2 wrapper; documented panel patch propagates tx_color errors.
idf_component_register(SRCS "esp_lcd_qspi_amoled.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_gpio REQUIRES esp_lcd)
