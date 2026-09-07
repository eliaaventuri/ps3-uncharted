/*
 * RSX fragment programs -> Metal Shading Language, checked by compiling them.
 *
 * The decompiler's HLSL output is validated by D3DCompile on Windows; nothing
 * validated the MSL path, so this hand-assembles a few programs, decompiles
 * them with RSX_FP_LANG_MSL and runs Apple's own `metal` compiler over the
 * result. A shader that merely looks plausible is not evidence -- one that
 * compiles is.
 *
 * Build:
 *   clang -std=c11 -I include -o test_fp_msl \
 *       libs/video/tests/test_fp_msl.c libs/video/rsx_fp_decompiler.c
 */
#include "../rsx_fp_decompiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

static void put_word(u8* p, u32 h)
{
    u32 be = (h << 16) | (h >> 16);
    p[0] = (u8)(be >> 24); p[1] = (u8)(be >> 16);
    p[2] = (u8)(be >> 8);  p[3] = (u8)(be);
}

#define OPC(x)      ((u32)(x) << 24)
#define OUTMASK_ALL (0xFu << 9)
#define END         (1u << 0)
#define INSRC(x)    ((u32)(x) << 13)
#define TEXU(x)     ((u32)(x) << 17)
#define SWZ_IDENT   ((0u<<9)|(1u<<11)|(2u<<13)|(3u<<15))
#define T_TEMP      0u
#define T_INPUT     1u
#define REG(i)      ((u32)(i) << 2)

/* Opcodes exercised here (see rsx_fp_decompiler.c). */
#define OP_MOV_  0x01u
#define OP_MUL_  0x02u
#define OP_FRC_  0x09u
#define OP_TEX_  0x11u
#define OP_LRP_  0x1Fu
#define OP_SLT_  0x0Cu

static void emit(u8* prog, u32 op, u32 src_type, u32 src_reg, u32 tex, int end)
{
    memset(prog, 0, 16);
    put_word(prog + 0, OPC(op) | OUTMASK_ALL | (end ? END : 0) | TEXU(tex));
    put_word(prog + 4, INSRC(src_type) | REG(src_reg) | SWZ_IDENT);
    put_word(prog + 8, INSRC(src_type) | REG(src_reg) | SWZ_IDENT);
    put_word(prog + 12, INSRC(src_type) | REG(src_reg) | SWZ_IDENT);
}

/* Write `msl` to a .metal file and run Apple's compiler over it. */
static int metal_compiles(const char* name, const char* msl)
{
    char src[64], cmd[512];
    snprintf(src, sizeof src, "/tmp/rsx_fp_%s.metal", name);
    FILE* f = fopen(src, "w");
    if (!f) { printf("[FAIL] %s -- cannot write %s\n", name, src); return 0; }
    fputs(msl, f);
    fclose(f);

    snprintf(cmd, sizeof cmd,
             "xcrun -sdk macosx metal -x metal -c %s -o /tmp/rsx_fp_%s.air 2>&1",
             src, name);
    FILE* pipe = popen(cmd, "r");
    if (!pipe) { printf("[FAIL] %s -- cannot run the metal compiler\n", name); return 0; }

    char line[512], errors[4096];
    errors[0] = '\0';
    while (fgets(line, sizeof line, pipe))
        if (strlen(errors) + strlen(line) < sizeof errors) strcat(errors, line);
    const int rc = pclose(pipe);

    if (rc == 0) { printf("[PASS] %s -- metal compiled it\n", name); return 1; }
    printf("[FAIL] %s -- metal rejected it:\n%s\nMSL:\n%s\n", name, errors, msl);
    return 0;
}

static void check(const char* name, u8* prog, u32 len, u32 ctrl, u32 cube,
                  int buffered)
{
    char msl[16384];
    const int n = rsx_fp_decompile_lang(prog, len, ctrl, cube, buffered,
                                        RSX_FP_LANG_MSL, msl, sizeof msl, NULL);
    if (n < 0) {
        printf("[FAIL] %s -- decompile returned %d\n", name, n);
        g_fail++;
        return;
    }
    if (metal_compiles(name, msl)) g_pass++; else g_fail++;
}

int main(void)
{
    u8 prog[64];

    emit(prog, OP_MOV_, T_INPUT, 0, 0, 1);
    check("mov", prog, sizeof prog, RSX_FP_CTRL_AUTO, 0, 0);

    emit(prog, OP_MUL_, T_TEMP, 1, 0, 1);
    check("mul", prog, sizeof prog, RSX_FP_CTRL_AUTO, 0, 0);

    emit(prog, OP_FRC_, T_TEMP, 2, 0, 1);          /* frac -> fract */
    check("frc", prog, sizeof prog, RSX_FP_CTRL_AUTO, 0, 0);

    emit(prog, OP_LRP_, T_TEMP, 0, 0, 1);          /* lerp -> mix   */
    check("lrp", prog, sizeof prog, RSX_FP_CTRL_AUTO, 0, 0);

    emit(prog, OP_SLT_, T_TEMP, 0, 0, 1);          /* (float4)cmp -> float4(cmp) */
    check("slt", prog, sizeof prog, RSX_FP_CTRL_AUTO, 0, 0);

    emit(prog, OP_TEX_, T_INPUT, 4, 3, 1);         /* .Sample -> .sample, 2D bank */
    check("tex2d", prog, sizeof prog, RSX_FP_CTRL_AUTO, 0, 0);

    emit(prog, OP_TEX_, T_INPUT, 4, 2, 1);         /* individual units, one cube */
    check("texcube", prog, sizeof prog, RSX_FP_CTRL_AUTO, 1u << 2, 0);

    emit(prog, OP_MOV_, T_INPUT, 0, 0, 1);         /* buffered constants path */
    check("buffered", prog, sizeof prog, RSX_FP_CTRL_AUTO, 0, 1);

    printf("\nRSX fragment MSL tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
