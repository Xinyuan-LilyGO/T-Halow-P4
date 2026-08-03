# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.18)

# The vendor HaLow driver, bus driver and pin map are shared with
# examples/halow_spi_single, and the hgic esp_netif driver with
# examples/halow_netif, rather than duplicated here.
set(EXTRA_COMPONENT_DIRS "../halow_spi_single/components" "../halow_netif/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(halow_node)
