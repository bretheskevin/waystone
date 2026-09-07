#include <3ds.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>  /* ssize_t */

/* Shared RNG fill backed by the 3DS PS hardware service.
 * psInit() MUST be called before any FFI crypto operations (vault init/unlock).
 * Returns 0 on success, -1 if PS fails. */
static int ctr_fill_random(void *buf, size_t len) {
    return R_SUCCEEDED(PS_GenerateRandomBytes(buf, len)) ? 0 : -1;
}

/* nx_getrandom — custom-getrandom symbol used by core/src/crypto.rs on Switch.
 * Kept linkable on 3DS even though the custom path is not selected there. */
void nx_getrandom(uint8_t *buf, size_t len) {
    ctr_fill_random(buf, len);
}

/* getrandom — POSIX shim required by getrandom-0.2 on horizon/arm (3DS).
 * Signature matches libc::getrandom as declared in
 * libc/src/unix/newlib/horizon/mod.rs:
 *   pub fn getrandom(buf: *mut c_void, buflen: size_t, flags: c_uint) -> ssize_t
 * flags are ignored (no /dev/random pool distinction on 3DS).
 * On PS failure sets errno = EIO (deterministic, non-EINTR) so getrandom()
 * reports a clean error without triggering sys_fill_exact's EINTR retry loop. */
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    if (ctr_fill_random(buf, buflen) != 0) {
        errno = EIO;            /* deterministic, non-EINTR: getrandom() returns Err, no retry loop */
        return (ssize_t)-1;
    }
    return (ssize_t)buflen;
}
