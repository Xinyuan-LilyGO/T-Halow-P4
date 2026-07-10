set(src_dirs ./)
idf_component_register(
        SRC_DIRS 
        ${src_dirs} 
        INCLUDE_DIRS 
        ${src_dirs}
    )

target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE -Wno-error=unused-const-variable -Wno-error=delete-non-virtual-dtor
            -Wno-error=implicit-function-declaration -fpermissive -Werror=stringop-truncation)

# The driver asks the application to supply a global raw_send(). That collides
# with lwIP's own raw_send() (lwip/src/core/raw.c) as soon as any netif is
# linked in, so rename the hook. PUBLIC: the application defines it, so it must
# see the same name.
target_compile_definitions(${COMPONENT_LIB} PUBLIC raw_send=hgic_spi_raw_send)