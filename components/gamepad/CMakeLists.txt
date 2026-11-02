# With the USB CDC console enabled the OTG port is a USB device, so the USB
# host gamepad driver is replaced by a stub (see gamepad_stub.c).
if(CONFIG_TINYUSB_CDC_ENABLED)
    set(gamepad_srcs "gamepad_stub.c")
    set(gamepad_priv_req "")
else()
    set(gamepad_srcs "gamepad.c")
    set(gamepad_priv_req usb)
endif()

idf_component_register(
    SRCS ${gamepad_srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${gamepad_priv_req}
)
