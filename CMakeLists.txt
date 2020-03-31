cmake_minimum_required(VERSION 3.8)

set(SUIL_BASE_PATH "" CACHE STRING "Path to an install of suil package")

if (SUIL_BASE_PATH)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${SUIL_BASE_PATH}/share/cmake/Modules")
endif (SUIL_BASE_PATH)

set(CMAKE_CXX_STANDARD 17)
include(Suil)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(APP_VERSION        0.0.0 CACHE STRING "Current application version")
set(API_VERSION        v1    CACHE STRING "Current API version")

set(semausu_DEFINES -DAPI_VERSION=\"${APP_VERSION}\")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SUIL_BUILD_DEBUG ON)
    set(semausu_DEFINES "${semausu_DEFINES};-DSWEPT")
endif()

if (SUIL_BUILD_DEV_PACKAGE)
    set(_SCC_BIN     ${CMAKE_BINARY_DIR}/tools/scc)
    set(_IOD_BIN     ${CMAKE_BINARY_DIR}/deps/iod/iodsyms)
    set(_GENTPS_BIN  ${CMAKE_BINARY_DIR}/tools/suiltps)
endif()

SuilProject(semausu
        SCC_BIN     ${_SCC_BIN}
        IOD_BIN     ${_IOD_BIN}
        GENTPS_BIN  ${_GENTPS_BIN}
        BUILD_DBG   ${SUIL_BUILD_DEBUG})

set(GATEWAY_SCCS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway/gateway.scc)

set(GATEWAY_SOURCES
        src/gateway/gateway.cpp
        src/gateway/users.cpp
        src/gateway/gateway.scc.cpp)

SuilApp(gateway
        SOURCES      ${GATEWAY_SOURCES} src/gateway/main.cpp
        VERSION      ${APP_VERSION}
        DEFINES      ${semausu_DEFINES}
        INSTALL      ON
        SCC_SOURCES  ${GATEWAY_SCCS}
        INSTALL_DIRS res)

if (SUIL_BUILD_DEBUG)
    SuilApp(gtytest
            SOURCES      tests/swept.cpp
            VERSION      ${APP_VERSION}
            DEFINES      ${semausu_DEFINES}
            INSTALL      ON
            DEPENDS      gateway-scc)
endif()
install(PROGRAMS wait_for
        DESTINATION bin)

if (SUIL_BUILD_DEV_PACKAGE)
    file(COPY res DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endif()