#include <3ds.h>
#include <stddef.h>
#include <stdint.h>

/* Provides the nx_getrandom symbol expected by core/src/crypto.rs console_entropy.
 * On 3DS, PS_GenerateRandomBytes uses the hardware RNG via the PS service.
 * psInit() MUST be called before any FFI crypto operations (vault init/unlock). */
void nx_getrandom(uint8_t *buf, size_t len) {
    PS_GenerateRandomBytes(buf, len);
}
