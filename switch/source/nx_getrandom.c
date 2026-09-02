#include <switch.h>
#include <stddef.h>
#include <stdint.h>

void nx_getrandom(uint8_t *buf, size_t len) {
    randomGet(buf, len);
}
