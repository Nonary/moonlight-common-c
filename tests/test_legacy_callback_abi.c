#include "Limelight-legacy.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    CONNECTION_LISTENER_CALLBACKS callbacks;
    unsigned char guard[16];
} GuardedCallbacks;

int main(void) {
    GuardedCallbacks guarded;
    size_t i;

    memset(&guarded, 0xA5, sizeof(guarded));
    LiInitializeConnectionCallbacks(&guarded.callbacks);

    for (i = 0; i < sizeof(guarded.callbacks); i++) {
        if (((const unsigned char*)&guarded.callbacks)[i] != 0) {
            fprintf(stderr, "callback initializer left byte %zu nonzero\n", i);
            return 1;
        }
    }
    for (i = 0; i < sizeof(guarded.guard); i++) {
        if (guarded.guard[i] != 0xA5) {
            fprintf(stderr, "legacy callback initializer wrote into guard byte %zu\n", i);
            return 1;
        }
    }

    return 0;
}
