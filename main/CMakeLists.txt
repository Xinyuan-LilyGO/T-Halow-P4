#这里选择你需要编译的项目
if(CONFIG_EXAMPLE_BUILD_TX_AH_R900PNR)
    set(PROJECT_PATH "examples/tx_ah_r900pnr/")
elseif(CONFIG_EXAMPLE_BUILD_TX_AH_R900PNR_SPEED_TEST)
    set(PROJECT_PATH "examples/tx_ah_r900pnr_speed_test/")
endif()
        
idf_component_register(SRC_DIRS ${PROJECT_PATH} 
                        INCLUDE_DIRS ${PROJECT_PATH})

target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE -Wno-error=unused-const-variable -Wno-error=delete-non-virtual-dtor
            -Wno-error=implicit-function-declaration -fpermissive)