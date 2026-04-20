#include "vizkey_hid.h"

#include <string.h>

void vizkey_hid_build_keyboard_report(uint8_t modifiers, uint8_t keycode, bool pressed, uint8_t report[8])
{
    if (report == NULL) {
        return;
    }

    memset(report, 0, 8);
    report[0] = modifiers;
    report[2] = pressed ? keycode : 0;
}
