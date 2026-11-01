#ifndef NUSB_USB_MSC_NATIVE_H
#define NUSB_USB_MSC_NATIVE_H

#include <libndls.h>

typedef struct {
    __typeof__(&TCT_Local_Control_Interrupts) TCT_Local_Control_Interrupts;
    __typeof__(&device_get_ivars) device_get_ivars;
    __typeof__(&device_get_softc) device_get_softc;
    __typeof__(&usbd_open_pipe) usbd_open_pipe;
    __typeof__(&usbd_close_pipe) usbd_close_pipe;
    __typeof__(&usbd_transfer) usbd_transfer;
    __typeof__(&usbd_alloc_xfer) usbd_alloc_xfer;
    __typeof__(&usbd_free_xfer) usbd_free_xfer;
    __typeof__(&usbd_setup_xfer) usbd_setup_xfer;
    __typeof__(&usbd_get_xfer_status) usbd_get_xfer_status;
    __typeof__(&usbd_interface2endpoint_descriptor) usbd_interface2endpoint_descriptor;
    __typeof__(&usbd_abort_pipe) usbd_abort_pipe;
    __typeof__(&usbd_clear_endpoint_stall) usbd_clear_endpoint_stall;
    __typeof__(&usbd_do_request_flags) usbd_do_request_flags;
    __typeof__(&usbd_get_interface_descriptor) usbd_get_interface_descriptor;
    __typeof__(&usb_register_driver) usb_register_driver;
} NativeUsb;

/* Diagnostic access only; not part of ResidentApi. */
const NativeUsb **msc_backend_native(void);

#endif
