set(PROJ_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB_RECURSE PROJ_SRCS ${PROJ_SRC_DIR}/*.c)

idf_component_register(
    SRCS ${PROJ_SRCS}
    INCLUDE_DIRS ${PROJ_SRC_DIR})

target_compile_definitions(${COMPONENT_LIB} PRIVATE "-DLV_LVGL_H_INCLUDE_SIMPLE")
target_compile_options(${COMPONENT_LIB} PUBLIC "-Wno-missing-field-initializers")