idf_component_register(SRCS "halow_router_main.cpp" "at_uart.c" "web_server.c"
                    INCLUDE_DIRS ".")

# The config page is committed as readable HTML and gzipped at build time; only
# the .gz is embedded in the image (served with Content-Encoding: gzip).
set(web_gz ${CMAKE_CURRENT_BINARY_DIR}/index.html.gz)
add_custom_command(OUTPUT ${web_gz}
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/www/index.html -DOUT=${web_gz}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gzip_file.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/www/index.html ${CMAKE_CURRENT_SOURCE_DIR}/gzip_file.cmake
    COMMENT "Compressing www/index.html")
add_custom_target(web_page DEPENDS ${web_gz})
target_add_binary_data(${COMPONENT_LIB} ${web_gz} BINARY DEPENDS web_page)
