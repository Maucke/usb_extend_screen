set(srcs "app_cdc_log.c" "app_hid.c" "app_lcd.c" "app_usb.c" "app_vendor.c"
         "lcd_amoled.c" "usb_extend_screen.c" "usb_frame.c")

if(CONFIG_HID_TOUCH_ENABLE)
    list (APPEND srcs "app_touch.c")
endif()

if(CONFIG_UAC_AUDIO_ENABLE)
    list (APPEND srcs "app_uac.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include" "tusb")

# Components pulled from the registry are registered as "<namespace>__<name>";
# components living in the project are registered under their plain name.
idf_build_get_property(build_components BUILD_COMPONENTS)

if(tinyusb IN_LIST build_components)
    set(tinyusb_name tinyusb) # Local component
else()
    set(tinyusb_name espressif__tinyusb) # Managed component
endif()

if(usb_device_uac IN_LIST build_components)
    set(uac_name usb_device_uac) # Local component
else()
    set(uac_name espressif__usb_device_uac) # Managed component
endif()

idf_component_get_property(UAC_PATH ${uac_name} COMPONENT_DIR)

idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${PROJECT_DIR}/main/tusb" "${UAC_PATH}/tusb_uac")
target_sources(${tusb_lib} PUBLIC "${PROJECT_DIR}/main/tusb/usb_descriptors.c")

cmake_policy(SET CMP0079 NEW)
target_link_libraries(${tusb_lib} PRIVATE ${COMPONENT_LIB})
