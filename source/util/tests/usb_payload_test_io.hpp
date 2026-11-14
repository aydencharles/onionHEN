#pragma once

#include <unistd.h>

extern "C" int onion_test_usb_link(const char *source, const char *destination);
extern "C" int onion_test_usb_fsync(int fd);
extern "C" ssize_t onion_test_usb_write(int fd, const void *buffer, size_t size);

// Redirect only the importer object; all other host code uses the real I/O.
#ifdef ONION_USB_PAYLOAD_TEST_IO
#define link onion_test_usb_link
#define fsync onion_test_usb_fsync
#define write onion_test_usb_write
#endif
