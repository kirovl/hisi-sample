# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(camera_ai_lcd LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
if(NOT SDK_ROOT OR NOT DRM_PREFIX)
    message(FATAL_ERROR "Set SDK_ROOT and DRM_PREFIX (ARM libdrm installation prefix)")
endif()
get_filename_component(SDK_ROOT "${SDK_ROOT}" ABSOLUTE)
get_filename_component(DRM_PREFIX "${DRM_PREFIX}" ABSOLUTE)
set(COMM "${CMAKE_CURRENT_SOURCE_DIR}/vendor/hisilicon/comm")
set(SDK_LIB "${SDK_ROOT}/soc/libs/musl")
add_executable(camera_ai_lcd src/main.c src/menu.c src/device_info.c src/overlay.c src/camera_mpp.c src/detector.c src/display_drm.c src/touch.c src/snapshot.c
    "${COMM}/sample_comm_sys.c" "${COMM}/sample_comm_vi.c"
    "${COMM}/sample_comm_vpss.c" "${COMM}/sample_comm_isp.c")
target_include_directories(camera_ai_lcd PRIVATE include "${COMM}"
    "${SDK_ROOT}/soc/include" "${SDK_ROOT}/soc/include/module"
    "${SDK_ROOT}/soc/config/gcc"
    "${DRM_PREFIX}/include" "${DRM_PREFIX}/include/libdrm")
target_compile_definitions(camera_ai_lcd PRIVATE _GNU_SOURCE
    SENSOR0_TYPE=OS04D10_MIPI_4M_30FPS_10BIT OS04D10_MIPI_4M_30FPS_10BIT_SELECT=y)
target_compile_options(camera_ai_lcd PRIVATE -O2 -Wall -Wextra -Werror=implicit-function-declaration
    -Werror=return-type -ffunction-sections -fdata-sections)
# Vendor sample helpers retain their original warning policy.
set_source_files_properties("${COMM}/sample_comm_sys.c" "${COMM}/sample_comm_vi.c"
    "${COMM}/sample_comm_vpss.c" "${COMM}/sample_comm_isp.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-sign-compare;-Wno-unused-function")
target_link_directories(camera_ai_lcd PRIVATE "${SDK_LIB}")
set_target_properties(camera_ai_lcd PROPERTIES LINKER_LANGUAGE CXX BUILD_RPATH "\$ORIGIN/lib")
target_link_options(camera_ai_lcd PRIVATE "-Wl,--gc-sections" "-Wl,-rpath-link,${SDK_LIB}")
target_link_libraries(camera_ai_lcd PRIVATE "${DRM_PREFIX}/lib/libdrm.a"
    ss_mpi ss_mpi_sysbind ss_mpi_sysmem ss_mpi_aidetect ss_ivs_md svp_acl protobuf-c
    aiservice_comm ss_mpi_cipher ss_mpi_ive
    ss_mpi_ae ss_mpi_isp ot_mpi_isp ss_mpi_awb dehaze extend_stats drc ldci bnr
    calcflicker ir_auto acs sns_os04d10 ot_osal ot_base ot_sys ot_vb ot_pm ot_vca
    ot_irq ot_sensor_i2c ot_vi ot_vpss ot_vpp ot_vgs ot_chnl ot_rgn ot_isp ot_aiisp
    securec pthread dl m)

add_library(wifi_ui OBJECT src/wifi_setup.c)
target_compile_definitions(wifi_ui PRIVATE _GNU_SOURCE CAM_WIFI_EMBEDDED)
target_include_directories(wifi_ui PRIVATE include)
target_sources(camera_ai_lcd PRIVATE $<TARGET_OBJECTS:wifi_ui>)
add_executable(wifi_setup src/wifi_setup.c src/device_info.c src/overlay.c src/display_drm.c src/touch.c)
target_include_directories(wifi_setup PRIVATE include "${DRM_PREFIX}/include" "${DRM_PREFIX}/include/libdrm")
target_compile_definitions(wifi_setup PRIVATE _GNU_SOURCE)
target_compile_options(wifi_setup PRIVATE -O2 -Wall -Wextra -Werror=implicit-function-declaration -Werror=return-type)
target_link_directories(wifi_setup PRIVATE "${SDK_LIB}")
target_link_libraries(wifi_setup PRIVATE "${DRM_PREFIX}/lib/libdrm.a" m)
