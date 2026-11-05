/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Include the production allocator to exercise its reservation/trim seam. */
#include "../../src/translation-layer-next/husk-tl-bionic-io.c"
#include <assert.h>
void tl_log_line(const char *fmt, ...) { (void)fmt; }
void tl_set_guest_errno(int e) { (void)e; }
int tl_errno_to_guest(int e) { return e; }
int main(int argc, char **argv) {
    bool late = argc > 1 && !strcmp(argv[1], "late");
    if (argc > 1 && !strcmp(argv[1], "giveback")) {
        /* An engine that is not V8 or Dart (Unity) in a process short of address space: the kept 4 GiB is given back when its
         * heaps no longer fit beside it. */
        husk_prepare_compressed_heap(); assert(g_compressed_heap);
        vm_address_t jit = 0;                                    /* the JIT region's two views and the libraries */
        assert(vm_map(mach_task_self(), &jit, (vm_size_t)1536 << 20, 0, VM_FLAGS_ANYWHERE, MACH_PORT_NULL, 0, FALSE,
                      VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL, VM_INHERIT_COPY) == KERN_SUCCESS);
        for (int i = 0; i < 3; i++) assert(b_mmap(NULL, 0x1fffc000, 0, 0x22, -1, 0) != MAP_FAILED);   /* Unity's 512 MiB heaps */
        assert(!g_compressed_heap);
        assert(b_mmap(NULL, (size_t)7 << 29, 0, 0x22, -1, 0) == MAP_FAILED);                         /* 3.5 GiB of the 3 left: nothing more to give */
        puts("PASS: an ordinary mapping refused beside the kept heap gets its space");
        return 0;
    }
    size_t len = 2 * PHANTOM_HALF - getpagesize();
    assert(compressed_heap_request(len));
    assert(compressed_heap_request(2 * PHANTOM_HALF));
    assert(!compressed_heap_request(len - getpagesize()));
    if (!late) {
        husk_prepare_compressed_heap(); assert(g_compressed_heap);
        vm_address_t first = g_compressed_heap;
        husk_prepare_compressed_heap(); assert(g_compressed_heap == first);
        husk_release_unused_compressed_heap(); assert(!g_compressed_heap);
        husk_prepare_compressed_heap(); assert(g_compressed_heap);
    }
    vm_address_t alias = 0;
    assert(vm_map(mach_task_self(), &alias, (vm_size_t)512 << 20, 0,
        VM_FLAGS_ANYWHERE, MACH_PORT_NULL, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_ALL, VM_INHERIT_COPY) == KERN_SUCCESS);
    void *p = phantom_reserve(len);
    if (late) {
        assert(p == MAP_FAILED);
        puts("PASS: late allocation fails in the constrained window");
    } else {
        assert(p != MAP_FAILED && !((uintptr_t)p % PHANTOM_HALF));
        assert(!g_compressed_heap);
        husk_release_unused_compressed_heap(); /* consumed cage must survive */
        assert(phantom_clamp((uintptr_t)p + PHANTOM_HALF, len - PHANTOM_HALF) == 0);
        assert(phantom_clamp((uintptr_t)p, len) == PHANTOM_HALF);
        assert(!mprotect(p, getpagesize(), PROT_READ | PROT_WRITE));
        assert(!mprotect((char *)p + PHANTOM_HALF - getpagesize(), getpagesize(), PROT_READ | PROT_WRITE));
        *(volatile char *)p = 42; *((volatile char *)p + PHANTOM_HALF - 1) = 43;
        assert(!munmap(p, PHANTOM_HALF));
        puts("PASS: early reservation, retry, single transfer, trim, commit at both ends");
    }
    assert(vm_deallocate(mach_task_self(), alias, (vm_size_t)512 << 20) == KERN_SUCCESS);
}
