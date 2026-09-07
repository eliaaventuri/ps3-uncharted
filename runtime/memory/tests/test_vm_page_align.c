/*
 * ps3recomp - guest VM vs HOST page granularity.
 *
 * mprotect() rejects any range that is not a multiple of the HOST page size.
 * The PS3 (and Windows) use 4 KB pages and VM_PAGE_SIZE is 4 KB to match, but
 * Apple Silicon uses 16 KB: every vm_commit/vm_protect of a 4 KB-aligned guest
 * range fails there with EINVAL, and the stack allocator's 4 KB guard page is
 * un-protectable. This exercises exactly those cases.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../vm.h"

/* The runtime proper defines these; a self-contained test carries its own. */
uint8_t* vm_base = NULL;
uint32_t ppu_vm_size = 0;
uint32_t ppu_hle_inject_base = 0;

#define GUEST32(ea) (*(volatile uint32_t*)(vm_base + (ea)))

static int failures = 0;

static void check(int ok, const char* what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    long hp = sysconf(_SC_PAGESIZE);
    printf("host page size: %ld bytes, VM_PAGE_SIZE: %u bytes\n\n",
           hp, (unsigned)VM_PAGE_SIZE);

    check(vm_init() == CELL_OK, "vm_init()");

    /* A guest allocation that is 4 KB-aligned but not host-page-aligned: the
     * case sys_memory and the PT_LOAD commit in ppu_loader.cpp both produce. */
    check(vm_commit(0x20001000u, 0x1000u) == CELL_OK,
          "vm_commit(0x20001000, 4 KB) -- unaligned to a 16 KB page");
    GUEST32(0x20001000u) = 0xDEADBEEFu;
    check(GUEST32(0x20001000u) == 0xDEADBEEFu, "  ...and the page is writable");

    check(vm_protect(0x20005000u, 0x1000u, 1, 1, 0) == CELL_OK,
          "vm_protect(0x20005000, 4 KB) -- unaligned to a 16 KB page");

    /* The stack allocator: the guard page must be un-writable, and the usable
     * stack immediately above it must be writable -- a guard rounded up to the
     * host page silently eats the bottom of the stack instead. */
    vm_stack_alloc sa;
    vm_stack_alloc_init(&sa);
    uint32_t stack = vm_stack_allocate(&sa, 0x10000u);
    check(stack != 0, "vm_stack_allocate(64 KB)");
    if (stack) {
        GUEST32(stack) = 0x12345678u;
        check(GUEST32(stack) == 0x12345678u,
              "  ...the first usable stack word is writable");
    }

    printf("\nvm page-alignment tests: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
