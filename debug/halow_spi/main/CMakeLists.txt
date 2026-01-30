idf_component_register(
SRCS 
    "main.c"
    "main_port_higc.c"
    "hgic_bootdl.c"
    "hgic_cmd.c"
    "hgic_ota.c"
    "hgic_raw.c"
    "hgic_sdspi_v2.c"
    # "hgic_sdspi.c"
    "hgic_spi_slave.c"

INCLUDE_DIRS ".")
