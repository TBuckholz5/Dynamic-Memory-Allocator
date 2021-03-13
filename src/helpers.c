#include "helpers.h"

size_t roundPayloadSize16(size_t size) {
    if (size % 16 == 0) {
        return size;
    } else {
        int i;
        for (i = 0; i < 16; ++i) {
            size++;
            if (size % 16 == 0) {
                return size;
            }
        }
        return -1;
    }
}
