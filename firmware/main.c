#include "hardware/sync.h"

int main(void)
{
    // Exercise SDK startup and leave the USB ports and GPIO idle.
    for (;;) {
        __wfi();
    }
}
