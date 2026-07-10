# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# The vendor HaLow driver, the bus driver and the board pin map are shared with
# examples/halow_spi_single rather than duplicated here.
set(EXTRA_COMPONENT_DIRS "../halow_spi_single/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(halow_netif)
