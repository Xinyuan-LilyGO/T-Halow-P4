# The AT console bridge, config web server and its page are shared with
# examples/halow_router rather than duplicated.
set(router_main ${CMAKE_CURRENT_SOURCE_DIR}/../../halow_router/main)

idf_component_register(SRCS "halow_node_main.cpp"
                            "${router_main}/at_uart.c"
                            "${router_main}/web_server.c"
                    INCLUDE_DIRS "." "${router_main}")

# The config page is committed as readable HTML and gzipped at build time; only
# the .gz is embedded in the image (served with Content-Encoding: gzip).
set(web_gz ${CMAKE_CURRENT_BINARY_DIR}/index.html.gz)
add_custom_command(OUTPUT ${web_gz}
    COMMAND ${CMAKE_COMMAND} -DIN=${router_main}/www/index.html -DOUT=${web_gz}
            -P ${router_main}/gzip_file.cmake
    DEPENDS ${router_main}/www/index.html ${router_main}/gzip_file.cmake
    COMMENT "Compressing www/index.html")
add_custom_target(web_page DEPENDS ${web_gz})
target_add_binary_data(${COMPONENT_LIB} ${web_gz} BINARY DEPENDS web_page)
