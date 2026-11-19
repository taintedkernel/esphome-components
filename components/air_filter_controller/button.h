#pragma once

#include <AceButton.h>
#include <TM1638plus.h>

using namespace ace_button;

class CustomButtonConfig : public ButtonConfig
{
private:
    TM1638plus *tm;

public:
    void setTM(TM1638plus* newTM) {
        tm = newTM;
    }

    int readButton(uint8_t pin)
    {
        if (tm == NULL) {
            ESP_LOGE("acebutton", "Error reading button, TM1638 not defined!");
            return 0;
        }

        // Pin numbering starts at 0
        int result = ((tm->readButtons()) & (0x01 << pin));
        return (result > 0) ? 1 : 0;
    }
};