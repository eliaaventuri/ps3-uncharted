/*
 * ps3recomp - POSIX boot smoke test.
 *
 * Boots a synthetic PPC64 image (tools/make_test_elf.py) through the real boot
 * path -- ppu_load_elf, ppu_recomp_register, the HLE/syscall layers, ppu_run --
 * and checks the guest actually executed: the program leaves 42 in r3 and at
 * guest 0x00100000.
 *
 * The project's own boot harness needs a decrypted retail EBOOT, which not
 * every checkout has, so nothing exercised the boot path on a machine with no
 * game on it. This does, which is what makes the POSIX port testable.
 */
#include <cstdio>
#include <cstdlib>
#include <cstdint>

/* Only vm_base is the PORT's to define -- ppu_vm_size lives in ppu_loader.cpp
 * and ppu_vfs_root in ppu_fs.cpp. */
extern "C" {
    uint8_t* vm_base = nullptr;
    extern uint32_t ppu_vm_size;
    extern const char* ppu_vfs_root;

    uint32_t ppu_load_elf(const char* path);
    void     ppu_recomp_register(void);
    void     ppu_hle_init(void);
    void     ppu_sysprx_register(void);
    void     ppu_fs_register(void);
    void     lv2_init_syscalls(void);
    int      ppu_run(uint32_t entry_opd, uint32_t stack_top);
}

#define STACK_TOP  0x0FF00000u
#define VM_BYTES   0x20000000ull      /* 512 MB: the synthetic image is tiny */
#define SCRATCH    0x00100000u
#define EXPECTED   42u

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: %s <hello.elf>\n", argv[0]); return 2; }

    vm_base = static_cast<uint8_t*>(std::calloc(1, VM_BYTES));
    if (!vm_base) { std::printf("[smoke] out of memory\n"); return 1; }
    ppu_vm_size = static_cast<uint32_t>(VM_BYTES);

    const uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) { std::printf("[smoke] load failed\n"); return 1; }

    ppu_recomp_register();
    ppu_hle_init();
    ppu_sysprx_register();
    ppu_fs_register();
    lv2_init_syscalls();

    std::printf("[smoke] entry OPD 0x%08X, stack top 0x%08X\n", entry, STACK_TOP);
    const int rc = ppu_run(entry, STACK_TOP);

    /* Big-endian guest word. */
    const uint8_t* p = vm_base + SCRATCH;
    const uint32_t stored = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                            (uint32_t)p[2] << 8  | (uint32_t)p[3];

    std::printf("[smoke] ppu_run returned %d\n", rc);
    std::printf("[smoke] guest 0x%08X = %u (expected %u)\n",
                SCRATCH, stored, EXPECTED);

    const bool ok = (stored == EXPECTED);
    std::printf("\nPOSIX boot smoke: %s\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
