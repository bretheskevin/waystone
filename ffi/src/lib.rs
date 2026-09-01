#![cfg_attr(feature = "switch", no_std)]
// All pub unsafe extern "C" ffi boundary functions share the same contract:
// every pointer argument must be valid or null (null is always checked and returns an error).
// Annotating each individually adds noise without clarity.
#![allow(clippy::missing_safety_doc)]

extern crate alloc;

#[cfg(all(feature = "switch", test))]
extern crate std;

#[cfg(feature = "switch")]
mod switch_runtime {
    use core::alloc::{GlobalAlloc, Layout};

    struct NewlibAllocator;

    // SAFETY: newlib's memalign returns correctly aligned pointers and free
    // releases them. This is the only allocator in the process; the Switch
    // homebrew is single-threaded so there are no data races on the heap.
    unsafe impl GlobalAlloc for NewlibAllocator {
        unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
            // SAFETY: memalign is provided by newlib at link time; layout
            // guarantees align is a power of two and size is non-zero.
            unsafe {
                unsafe extern "C" {
                    fn memalign(align: usize, size: usize) -> *mut u8;
                }
                memalign(layout.align(), layout.size())
            }
        }

        unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
            // SAFETY: ptr was allocated by memalign above; free is the
            // matching newlib deallocator.
            unsafe {
                unsafe extern "C" {
                    fn free(ptr: *mut u8);
                }
                free(ptr)
            }
        }
    }

    #[global_allocator]
    static ALLOCATOR: NewlibAllocator = NewlibAllocator;

    #[panic_handler]
    fn panic(_info: &core::panic::PanicInfo) -> ! {
        loop {}
    }
}

pub mod adapter_abi;
pub mod buffer;
pub mod conflict_abi;
pub mod crypto_abi;
pub mod dto;
pub mod error;
pub mod packaging_abi;
