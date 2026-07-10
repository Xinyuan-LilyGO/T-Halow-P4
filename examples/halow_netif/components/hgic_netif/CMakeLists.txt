idf_component_register(
        SRCS "hgic_netif.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_netif esp_event "huge-ic-nonos-driver-v2.0.0-31919"
    )
