#include "usb_names.h"

// Edit these lines to create your own name.  The length must
// match the number of characters in your custom name.

#define MIDI_NAME    {'T', 'O', 'T', 'A', 'L', ' ', 'C', 'O', 'N', 'T', 'R', 'O', 'L'}
#define MIDI_NAME_LEN  13

#define VENDOR_NAME   {'T', 'O', 'T', 'A', 'L', ' ', 'T', 'O', 'T', 'A', 'L'}
#define VENDOR_NAME_LEN  11

// Do not change this part.  This exact format is required by USB.

struct usb_string_descriptor_struct usb_string_product_name = {
        2 + MIDI_NAME_LEN * 2,
        3,
        MIDI_NAME
};

struct usb_string_descriptor_struct usb_string_manufacturer_name = {
        2 + VENDOR_NAME_LEN * 2,
        3,
        VENDOR_NAME
};