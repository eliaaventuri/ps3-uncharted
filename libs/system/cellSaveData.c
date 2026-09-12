/*
 * ps3recomp - cellSaveData HLE implementation
 *
 * Game save data management with callback-driven flow.
 * Save data is stored under: {root}/gamedata/dev_hdd0/home/00000001/savedata/{dirName}/
 */

#include "cellSaveData.h"
#include "ps3emu/guest_call.h"
#include "../../runtime/ppu/ppu_memory.h"
#include "../../runtime/memory/vm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

/* glibc's <sys/stat.h> exposes st_atime/st_mtime/st_ctime as macros (st_atim.tv_sec
 * etc.) that collide with the identically named members of CellSaveData*Stat; undef
 * them so the struct fields resolve, and read host times via the explicit timespec
 * fields in the POSIX branches below. */
#ifdef st_atime
#  undef st_atime
#endif
#ifdef st_mtime
#  undef st_mtime
#endif
#ifdef st_ctime
#  undef st_ctime
#endif

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  include <direct.h>
#  define HOST_MKDIR(p) _mkdir(p)
#  define HOST_STAT     _stat64
#  define HOST_STAT_T   struct __stat64
#else
#  include <unistd.h>
#  include <dirent.h>
#  include <sys/types.h>
#  define HOST_MKDIR(p) mkdir(p, 0755)
#  define HOST_STAT     stat
#  define HOST_STAT_T   struct stat
#endif

/* ---------------------------------------------------------------------------
 * Internal helpers
 * -----------------------------------------------------------------------*/

static char s_save_root[1024] = "./gamedata/dev_hdd0/home/00000001/savedata";

/* Ensure directory (and parents) exist */
static void ensure_dirs(const char* path)
{
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            HOST_MKDIR(tmp);
            *p = saved;
        }
    }
    HOST_MKDIR(tmp);
}

static void build_save_path(char* buf, size_t buf_size, const char* dirName)
{
    snprintf(buf, buf_size, "%s/%s", s_save_root, dirName);
#ifdef _WIN32
    for (char* p = buf; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#endif
}

/* The generic HLE adapter passes PPC register values raw, so string args
 * arrive as GUEST addresses (< 4GB). Translate to a host pointer once at
 * the entry point; the internal helpers all expect host char*. */
static const char* savedata_host_str(const char* p)
{
    uintptr_t v = (uintptr_t)p;
    if (v && v < 0x100000000ull)
        return (const char*)(vm_base + (uint32_t)v);
    return p;
}

static int dir_exists(const char* path)
{
    HOST_STAT_T st;
    if (HOST_STAT(path, &st) != 0)
        return 0;
#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

/* ---------------------------------------------------------------------------
 * Guest-callback marshalling
 *
 * The funcStat / funcFile callbacks the game registers are GUEST function
 * pointers (OPD addresses), not host-callable. We have to:
 *   1. Allocate guest memory for the StatGet/StatSet/CBResult structures
 *   2. Write the structs in big-endian format
 *   3. Dispatch the OPD via g_ps3_guest_caller with the guest pointers
 *   4. Read the structures back (game may have updated CBResult.result)
 *
 * Layout (sizes in PS3 ABI, all big-endian, 32-bit pointers):
 *   CellSaveDataCBResult:    4 + 4 + 4 + 4 + 4                = 20 bytes
 *   CellSaveDataStatGet:     4 + 4 + 56 + 1552 + 4*5 + 4      = 1640 bytes
 *   CellSaveDataStatSet:     4 + 4 + 4                        = 12 bytes (pointers + reCreateMode)
 *
 * We use a fixed scratch region at 0x024E0000 (128 KB, just before the
 * cmdbuf region at 0x02500000). Only one savedata operation is in flight
 * at a time, so a bump allocator is sufficient.
 * -----------------------------------------------------------------------*/

/* Sessione 27: la base fissa 0x024E0000 stava DENTRO la memoria di Uncharted.
 * Ora e' la coda della finestra HLE (VM_HLE_INJECT_BASE + 0x8000, 32 KB: i
 * primi 0x8000 li usano import, controllo GCM e tabelle IO), che per
 * costruzione il titolo non tocca mai. */
#define SAVEDATA_SCRATCH_BASE  (VM_HLE_INJECT_BASE + 0x8000u)
#define SAVEDATA_SCRATCH_SIZE  0x00008000u  /* 32 KB */

static uint32_t s_scratch_next = 0;

static void scratch_reset(void) { s_scratch_next = SAVEDATA_SCRATCH_BASE; }

static uint32_t scratch_alloc(uint32_t size)
{
    if (!s_scratch_next) s_scratch_next = SAVEDATA_SCRATCH_BASE;
    /* 16-byte align */
    size = (size + 0xF) & ~0xFu;
    if (s_scratch_next + size > SAVEDATA_SCRATCH_BASE + SAVEDATA_SCRATCH_SIZE)
        return 0;
    uint32_t addr = s_scratch_next;
    s_scratch_next += size;
    /* Zero-init */
    memset(vm_base + addr, 0, size);
    return addr;
}

/* Big-endian struct field writers. Offsets are in PS3 ABI layout.
 *
 * CellSaveDataCBResult (sysutil_savedata.h) is 20 bytes, and invalidMsg is a
 * POINTER, not an inline buffer:
 *   int   result;                +0
 *   unsigned int progressBarInc; +4
 *   int   errNeedSizeKB;         +8
 *   char *invalidMsg;            +12   (32-bit guest pointer, NULL when unused)
 *   void *userdata;              +16   (32-bit guest pointer)
 *
 * userdata is the pointer the title handed to cellSaveData*(..., userdata) and
 * it MUST be echoed back here: a callback typically recovers its own object
 * from it. LBP's funcStat is literally
 *     v = cbResult->userdata; r = (*(v+104))(cbResult, get, set); ...
 * so leaving it zero made it dispatch through guest address 104, read the VM's
 * zero-fill, and report CELL_SAVEDATA_CBRESULT_ERR_NODATA (-4) -- it never
 * reached its own handler at all. */
static void marshal_cbresult_init(uint32_t addr, s32 result, uint32_t userdata_ea)
{
    vm_write32(addr + 0,  (uint32_t)result);     /* result */
    vm_write32(addr + 4,  0);                    /* progressBarInc */
    vm_write32(addr + 8,  0);                    /* errNeedSizeKB */
    vm_write32(addr + 12, 0);                    /* invalidMsg = NULL */
    vm_write32(addr + 16, userdata_ea);          /* userdata (echoed back) */
}

static s32 marshal_cbresult_read_result(uint32_t addr)
{
    return (s32)vm_read32(addr + 0);
}

/* PS3 ABI layout of CellSaveDataStatGet (all big-endian on PPC, 32-bit pointers):
 *   s32 hddFreeSizeKB                     +0
 *   u32 isNewData                         +4
 *   CellSaveDataDirStat dir               +8   (s64*3 + char[32] = 56 bytes)
 *   CellSaveDataSystemFileParam getParam  +64  (128+128+1024+4+4+8+256 = 1552 bytes)
 *   u32 bind                              +1616
 *   s32 sizeKB                            +1620
 *   s32 sysSizeKB                         +1624
 *   u32 fileNum                           +1628
 *   u32 fileListNum                       +1632
 *   ptr fileList                          +1636 (32-bit guest ptr)
 *   total                                 1640 bytes
 */
#define SAVEDATA_STATGET_SIZE   1640u
#define SAVEDATA_STATSET_SIZE   16u    /* setParam ptr + reCreateMode + indicator ptr */
#define SAVEDATA_CBRESULT_SIZE  20u    /* s32 + u32 + s32 + char* + void* */

/* CellSaveDataFileGet is 0x44 (excSize + reserved[64]); CellSaveDataFileSet is
 * 0x30 (fileOperation, reserved, fileType, secureFileId[16], fileName ptr,
 * fileOffset, fileSize, fileBufSize, fileBuf ptr). */
#define SAVEDATA_FILEGET_SIZE   0x44u
#define SAVEDATA_FILESET_SIZE   0x30u

/* Dispatch the game's funcFile the same way funcStat is dispatched.
 *
 * It used to be called as a HOST function pointer with HOST stack addresses:
 *
 *     funcFile(&cbResult, &fileGet, &fileSet);
 *
 * funcFile is a GUEST OPD, so that jumped straight into guest memory as if it
 * were host code -- "ACCESS VIOLATION: execute at host 0x34C250" the moment
 * Tokyo Jungle autosaved. Marshal the three structs into guest memory and go
 * through g_ps3_guest_caller, exactly as dispatch_func_stat does.
 *
 * ponytail: the callback's fileSet is not read back yet, so the game's chosen
 * file is not written -- the callback runs and the title advances instead of
 * crashing. Read fileName/fileBuf/fileSize out of set_ea and feed
 * process_file_op when a title actually needs its save contents. */
static s32 dispatch_func_file(uint32_t func_opd, uint32_t userdata_ea)
{
    if (!g_ps3_guest_caller) return CELL_SAVEDATA_CBRESULT_ERR_FAILURE;

    scratch_reset();
    uint32_t cb_ea  = scratch_alloc(SAVEDATA_CBRESULT_SIZE);
    uint32_t get_ea = scratch_alloc(SAVEDATA_FILEGET_SIZE);
    uint32_t set_ea = scratch_alloc(SAVEDATA_FILESET_SIZE);
    if (!cb_ea || !get_ea || !set_ea) {
        printf("[cellSaveData] funcFile scratch alloc failed\n");
        return CELL_SAVEDATA_CBRESULT_ERR_FAILURE;
    }

    marshal_cbresult_init(cb_ea, CELL_SAVEDATA_CBRESULT_OK_NEXT, userdata_ea);

    printf("[cellSaveData] dispatching funcFile OPD=0x%08X (cb=0x%X get=0x%X set=0x%X)\n",
           func_opd, cb_ea, get_ea, set_ea);
    g_ps3_guest_caller(func_opd, cb_ea, get_ea, set_ea, 0, 0, 0, 0, 0);

    s32 result = marshal_cbresult_read_result(cb_ea);
    printf("[cellSaveData] funcFile returned cbResult.result=%d\n", result);
    return result;
}

static void marshal_statget_init(uint32_t addr, int is_new, const char* dirName,
                                  s32 sizeKB, u32 fileNum)
{
    vm_write32(addr + 0,    1024 * 1024);    /* hddFreeSizeKB = 1 GB */
    vm_write32(addr + 4,    is_new ? 1 : 0); /* isNewData */
    vm_write64(addr + 8,    0);              /* dir.st_atime */
    vm_write64(addr + 16,   0);              /* dir.st_mtime */
    vm_write64(addr + 24,   0);              /* dir.st_ctime */
    if (dirName) {
        size_t len = strnlen(dirName, CELL_SAVEDATA_DIRNAME_SIZE - 1);
        memcpy(vm_base + addr + 32, dirName, len);
        ((char*)vm_base)[addr + 32 + len] = 0;
    }
    /* getParam (+64..+1615) left zero — no PARAM.SFO data */
    vm_write32(addr + 1616, 0);              /* bind */
    vm_write32(addr + 1620, (uint32_t)sizeKB); /* sizeKB */
    vm_write32(addr + 1624, 0);              /* sysSizeKB */
    vm_write32(addr + 1628, fileNum);        /* fileNum */
    vm_write32(addr + 1632, 0);              /* fileListNum */
    vm_write32(addr + 1636, 0);              /* fileList ptr (NULL — no files) */
}

/* Dispatch funcStat callback via the guest-caller hook.
 * Returns the cbResult.result value the callback wrote, or
 * CELL_SAVEDATA_CBRESULT_ERR_FAILURE if no dispatcher is installed. */
static s32 dispatch_func_stat(uint32_t func_opd, int is_new, const char* dirName,
                              uint32_t userdata_ea)
{
    if (!g_ps3_guest_caller) return CELL_SAVEDATA_CBRESULT_ERR_FAILURE;

    scratch_reset();
    uint32_t cb_ea  = scratch_alloc(SAVEDATA_CBRESULT_SIZE);
    uint32_t get_ea = scratch_alloc(SAVEDATA_STATGET_SIZE);
    uint32_t set_ea = scratch_alloc(SAVEDATA_STATSET_SIZE);
    if (!cb_ea || !get_ea || !set_ea) {
        printf("[cellSaveData] scratch alloc failed\n");
        return CELL_SAVEDATA_CBRESULT_ERR_FAILURE;
    }

    marshal_cbresult_init(cb_ea, CELL_SAVEDATA_CBRESULT_OK_NEXT, userdata_ea);
    marshal_statget_init(get_ea, is_new, dirName, 0, 0);
    /* StatSet zero-init by scratch_alloc */

    printf("[cellSaveData] dispatching funcStat OPD=0x%08X (cb=0x%X get=0x%X set=0x%X, isNew=%d, userdata=0x%08X)\n",
           func_opd, cb_ea, get_ea, set_ea, is_new, userdata_ea);
    g_ps3_guest_caller(func_opd, cb_ea, get_ea, set_ea, 0, 0, 0, 0, 0);

    s32 result = marshal_cbresult_read_result(cb_ea);
    printf("[cellSaveData] funcStat returned cbResult.result=%d\n", result);
    return result;
}

/* Enumerate save directories matching a prefix. Returns count, fills dirList up to max. */
static u32 enumerate_save_dirs(const char* prefix, CellSaveDataDirList* dirList, u32 max)
{
    u32 count = 0;

    ensure_dirs(s_save_root);

#ifdef _WIN32
    {
        char search[1024];
        snprintf(search, sizeof(search), "%s\\*", s_save_root);
        for (char* p = search; *p; p++) {
            if (*p == '/') *p = '\\';
        }

        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search, &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            return 0;

        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            if (fd.cFileName[0] == '.')
                continue;
            if (prefix && prefix[0] && strncmp(fd.cFileName, prefix, strlen(prefix)) != 0)
                continue;
            if (count < max && dirList) {
                memset(&dirList[count], 0, sizeof(CellSaveDataDirList));
                strncpy(dirList[count].dirName, fd.cFileName,
                        CELL_SAVEDATA_DIRNAME_SIZE - 1);
            }
            count++;
        } while (FindNextFileA(hFind, &fd));

        FindClose(hFind);
    }
#else
    {
        DIR* dp = opendir(s_save_root);
        if (!dp)
            return 0;

        struct dirent* de;
        while ((de = readdir(dp)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            /* Check it's a directory */
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", s_save_root, de->d_name);
            if (!dir_exists(full))
                continue;
            if (prefix && prefix[0] && strncmp(de->d_name, prefix, strlen(prefix)) != 0)
                continue;
            if (count < max && dirList) {
                memset(&dirList[count], 0, sizeof(CellSaveDataDirList));
                strncpy(dirList[count].dirName, de->d_name,
                        CELL_SAVEDATA_DIRNAME_SIZE - 1);
            }
            count++;
        }
        closedir(dp);
    }
#endif

    return count;
}

/* Enumerate files in a save directory. Returns count, fills fileList up to max. */
static u32 enumerate_save_files(const char* save_path,
                                 CellSaveDataFileStat* fileList, u32 max)
{
    u32 count = 0;

#ifdef _WIN32
    {
        char search[1024];
        snprintf(search, sizeof(search), "%s\\*", save_path);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search, &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            return 0;

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (count < max && fileList) {
                memset(&fileList[count], 0, sizeof(CellSaveDataFileStat));
                fileList[count].fileType = CELL_SAVEDATA_FILETYPE_NORMALFILE;
                strncpy(fileList[count].fileName, fd.cFileName,
                        CELL_SAVEDATA_FILENAME_SIZE - 1);
                ULARGE_INTEGER sz;
                sz.HighPart = fd.nFileSizeHigh;
                sz.LowPart  = fd.nFileSizeLow;
                fileList[count].st_size = (u64)sz.QuadPart;
            }
            count++;
        } while (FindNextFileA(hFind, &fd));

        FindClose(hFind);
    }
#else
    {
        DIR* dp = opendir(save_path);
        if (!dp)
            return 0;

        struct dirent* de;
        while ((de = readdir(dp)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", save_path, de->d_name);
            HOST_STAT_T st;
            if (HOST_STAT(full, &st) != 0)
                continue;
#ifdef _WIN32
            if (st.st_mode & _S_IFDIR)
                continue;
#else
            if (S_ISDIR(st.st_mode))
                continue;
#endif
            if (count < max && fileList) {
                memset(&fileList[count], 0, sizeof(CellSaveDataFileStat));
                fileList[count].fileType = CELL_SAVEDATA_FILETYPE_NORMALFILE;
                strncpy(fileList[count].fileName, de->d_name,
                        CELL_SAVEDATA_FILENAME_SIZE - 1);
                fileList[count].st_size = (u64)st.st_size;
#ifdef _WIN32
                fileList[count].st_atime = (s64)st.st_atime;
                fileList[count].st_mtime = (s64)st.st_mtime;
                fileList[count].st_ctime = (s64)st.st_ctime;
#elif defined(__APPLE__)
/* Darwin names the sub-second stat fields st_*timespec, not glibc's st_*tim. */
                fileList[count].st_atime = (s64)st.st_atimespec.tv_sec;
                fileList[count].st_mtime = (s64)st.st_mtimespec.tv_sec;
                fileList[count].st_ctime = (s64)st.st_ctimespec.tv_sec;
#else
                fileList[count].st_atime = (s64)st.st_atim.tv_sec;
                fileList[count].st_mtime = (s64)st.st_mtim.tv_sec;
                fileList[count].st_ctime = (s64)st.st_ctim.tv_sec;
#endif
            }
            count++;
        }
        closedir(dp);
    }
#endif

    return count;
}

/* Execute file callback: read/write/delete files in the save directory */
static s32 process_file_op(const char* save_path, CellSaveDataFileSet* set)
{
    if (!set || !set->fileName)
        return CELL_OK;

    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/%s", save_path, set->fileName);
#ifdef _WIN32
    for (char* p = file_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#endif

    switch (set->fileOperation) {
    case CELL_SAVEDATA_FILEOP_READ: {
        FILE* fp = fopen(file_path, "rb");
        if (!fp) {
            printf("[cellSaveData] file read: cannot open '%s'\n", file_path);
            return 0; /* excSize = 0 */
        }
        if (set->fileOffset > 0) {
#ifdef _MSC_VER
            _fseeki64(fp, (long long)set->fileOffset, SEEK_SET);
#else
            fseeko(fp, (off_t)set->fileOffset, SEEK_SET);
#endif
        }
        size_t read_size = (size_t)set->fileSize;
        if (read_size > set->fileBufSize) read_size = set->fileBufSize;
        size_t got = fread(set->fileBuf, 1, read_size, fp);
        fclose(fp);
        return (s32)got;
    }

    case CELL_SAVEDATA_FILEOP_WRITE:
    case CELL_SAVEDATA_FILEOP_WRITE_NOTRUNC: {
        ensure_dirs(save_path);
        const char* mode;
        if (set->fileOperation == CELL_SAVEDATA_FILEOP_WRITE && set->fileOffset == 0) {
            mode = "wb";
        } else {
            mode = "r+b";
        }
        FILE* fp = fopen(file_path, mode);
        if (!fp) {
            fp = fopen(file_path, "wb");
        }
        if (!fp) {
            printf("[cellSaveData] file write: cannot open '%s'\n", file_path);
            return 0;
        }
        if (set->fileOffset > 0) {
#ifdef _MSC_VER
            _fseeki64(fp, (long long)set->fileOffset, SEEK_SET);
#else
            fseeko(fp, (off_t)set->fileOffset, SEEK_SET);
#endif
        }
        size_t write_size = (size_t)set->fileSize;
        if (write_size > set->fileBufSize) write_size = set->fileBufSize;
        size_t wrote = fwrite(set->fileBuf, 1, write_size, fp);
        fclose(fp);
        return (s32)wrote;
    }

    case CELL_SAVEDATA_FILEOP_DELETE:
        remove(file_path);
        return 0;

    default:
        return 0;
    }
}

/* Write a simplified PARAM.SFO with the save's title/subtitle/detail */
static void write_param_sfo(const char* save_path, const CellSaveDataSystemFileParam* param)
{
    if (!param) return;

    char sfo_path[1024];
    snprintf(sfo_path, sizeof(sfo_path), "%s/PARAM.SFO", save_path);
#ifdef _WIN32
    for (char* p = sfo_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#endif

    FILE* fp = fopen(sfo_path, "wb");
    if (!fp) return;

    /* Write a simplified text-based PARAM.SFO for easy debugging.
       Games don't read this directly - our stat callback fills it from here. */
    fprintf(fp, "TITLE=%s\n", param->title);
    fprintf(fp, "SUB_TITLE=%s\n", param->subTitle);
    fprintf(fp, "DETAIL=%s\n", param->detail);
    fprintf(fp, "ATTRIBUTE=%u\n", param->attribute);
    fprintf(fp, "LIST_PARAM=%s\n", param->listParam);
    fclose(fp);
}

/* Read simplified PARAM.SFO */
static void read_param_sfo(const char* save_path, CellSaveDataSystemFileParam* param)
{
    if (!param) return;
    memset(param, 0, sizeof(CellSaveDataSystemFileParam));

    char sfo_path[1024];
    snprintf(sfo_path, sizeof(sfo_path), "%s/PARAM.SFO", save_path);
#ifdef _WIN32
    for (char* p = sfo_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#endif

    FILE* fp = fopen(sfo_path, "rb");
    if (!fp) return;

    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (strncmp(line, "TITLE=", 6) == 0)
            strncpy(param->title, line + 6, CELL_SAVEDATA_SYSP_TITLE_SIZE - 1);
        else if (strncmp(line, "SUB_TITLE=", 10) == 0)
            strncpy(param->subTitle, line + 10, CELL_SAVEDATA_SYSP_SUBTITLE_SIZE - 1);
        else if (strncmp(line, "DETAIL=", 7) == 0)
            strncpy(param->detail, line + 7, CELL_SAVEDATA_SYSP_DETAIL_SIZE - 1);
        else if (strncmp(line, "ATTRIBUTE=", 10) == 0)
            param->attribute = (u32)atoi(line + 10);
        else if (strncmp(line, "LIST_PARAM=", 11) == 0)
            strncpy(param->listParam, line + 11, CELL_SAVEDATA_SYSP_LPARAM_SIZE - 1);
    }
    fclose(fp);
}

/* Core save/load implementation shared by List/Fixed/Auto variants */
static s32 savedata_execute(const char* dirName, int is_save,
                             CellSaveDataSetBuf* setBuf,
                             CellSaveDataStatCallback funcStat,
                             CellSaveDataFileCallback funcFile,
                             void* userdata)
{
    if (!dirName || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    char save_path[1024];
    build_save_path(save_path, sizeof(save_path), dirName);

    int is_new = !dir_exists(save_path);

    printf("[cellSaveData] %s dir='%s' (new=%d)\n",
           is_save ? "SAVE" : "LOAD", dirName, is_new);

    /* If loading and directory doesn't exist, it's not an error -
       the stat callback will see isNewData=1 and can handle it */

    /* Prepare stat get */
    /* setBuf is a GUEST address, not a host pointer. Dereferencing it faulted
     * the moment the title autosaved: cellSaveDataAutoSave2 -> savedata_execute
     * read setBuf->fileListMax off a guest stack address and took an access
     * violation (0xD00DCF84 = r1+0x90). Read it out of guest memory, the way
     * the dirListMax read below already does. fileListMax is at +4. */
    u32 file_list_max = setBuf ? vm_read32((u32)(uintptr_t)setBuf + 4) : 64;
    CellSaveDataFileStat* fileList = NULL;
    u32 fileNum = 0;

    if (file_list_max > 0) {
        fileList = (CellSaveDataFileStat*)calloc(file_list_max, sizeof(CellSaveDataFileStat));
        if (!is_new) {
            fileNum = enumerate_save_files(save_path, fileList, file_list_max);
        }
    }

    CellSaveDataStatGet statGet;
    memset(&statGet, 0, sizeof(statGet));
    statGet.hddFreeSizeKB = 1024 * 1024; /* report 1GB free */
    statGet.isNewData = is_new ? 1 : 0;
    strncpy(statGet.dir.dirName, dirName, CELL_SAVEDATA_DIRNAME_SIZE - 1);

    if (!is_new) {
        HOST_STAT_T hst;
        if (HOST_STAT(save_path, &hst) == 0) {
#ifdef _WIN32
            statGet.dir.st_atime = (s64)hst.st_atime;
            statGet.dir.st_mtime = (s64)hst.st_mtime;
            statGet.dir.st_ctime = (s64)hst.st_ctime;
#elif defined(__APPLE__)
/* Darwin names the sub-second stat fields st_*timespec, not glibc's st_*tim. */
            statGet.dir.st_atime = (s64)hst.st_atimespec.tv_sec;
            statGet.dir.st_mtime = (s64)hst.st_mtimespec.tv_sec;
            statGet.dir.st_ctime = (s64)hst.st_ctimespec.tv_sec;
#else
            statGet.dir.st_atime = (s64)hst.st_atim.tv_sec;
            statGet.dir.st_mtime = (s64)hst.st_mtim.tv_sec;
            statGet.dir.st_ctime = (s64)hst.st_ctim.tv_sec;
#endif
        }
        read_param_sfo(save_path, &statGet.getParam);
    }

    statGet.fileNum = fileNum;
    statGet.fileListNum = fileNum < file_list_max ? fileNum : file_list_max;
    statGet.fileList = fileList;
    statGet.bind = 0;
    statGet.sizeKB = 0;
    statGet.sysSizeKB = 0;

    /* Call stat callback */
    CellSaveDataCBResult cbResult;
    memset(&cbResult, 0, sizeof(cbResult));
    cbResult.result = CELL_SAVEDATA_CBRESULT_OK_NEXT;

    CellSaveDataStatSet statSet;
    memset(&statSet, 0, sizeof(statSet));

    /* Same guest-OPD-as-host-pointer bug as funcFile below: this jumped into
     * guest memory as host code -- ACCESS VIOLATION: execute at host 0x34C250
     * -- the moment Tokyo Jungle autosaved. Route it through the marshalling
     * dispatcher, which builds StatGet/StatSet in guest memory. */
    cbResult.result = dispatch_func_stat((uint32_t)(uintptr_t)funcStat, is_new,
                                         dirName, (uint32_t)(uintptr_t)userdata);

    if (cbResult.result < 0) {
        printf("[cellSaveData] stat callback returned error %d\n", cbResult.result);
        free(fileList);
        if (cbResult.result == CELL_SAVEDATA_CBRESULT_ERR_NODATA)
            return CELL_SAVEDATA_ERROR_NODATA;
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }

    /* Write PARAM.SFO if stat set provided params and we're saving */
    if (is_save && statSet.setParam) {
        ensure_dirs(save_path);
        write_param_sfo(save_path, statSet.setParam);
    }

    /* Call file callback repeatedly */
    if (funcFile && cbResult.result == CELL_SAVEDATA_CBRESULT_OK_NEXT) {
        while (1) {
            CellSaveDataFileGet fileGet;
            memset(&fileGet, 0, sizeof(fileGet));

            CellSaveDataFileSet fileSet;
            memset(&fileSet, 0, sizeof(fileSet));

            cbResult.result = CELL_SAVEDATA_CBRESULT_OK_NEXT;

            cbResult.result = dispatch_func_file((uint32_t)(uintptr_t)funcFile, (uint32_t)(uintptr_t)userdata);

            if (cbResult.result == CELL_SAVEDATA_CBRESULT_OK_LAST ||
                cbResult.result < 0) {
                /* Process last operation if set */
                if (fileSet.fileName && fileSet.fileBuf) {
                    s32 exc = process_file_op(save_path, &fileSet);
                    (void)exc;
                }
                break;
            }

            if (fileSet.fileName && fileSet.fileBuf) {
                s32 exc = process_file_op(save_path, &fileSet);
                (void)exc;
            } else {
                /* No file operation requested, done */
                break;
            }
        }
    }

    free(fileList);

    if (cbResult.result < 0) {
        if (cbResult.result == CELL_SAVEDATA_CBRESULT_ERR_NODATA)
            return CELL_SAVEDATA_ERROR_NODATA;
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }

    printf("[cellSaveData] %s complete for '%s'\n",
           is_save ? "SAVE" : "LOAD", dirName);
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * API implementations
 * -----------------------------------------------------------------------*/

/* cellSaveDataListSave2 / ListLoad2 (NID 0x2DE0D663 / 0x1DFBFDD6) -- con le
 * callback GUEST, come cellSaveDataListAutoLoad piu' sotto (le versioni
 * precedenti le chiamavano come puntatori host: "UNTRANSLATED GUEST POINTER"
 * quando Uncharted ha salvato la prima volta, "New Game" dal menu).
 *
 *   (version, setList*, setBuf*, funcList, funcStat, funcFile, container,
 *    userdata) -- 8 argomenti in r3..r10.
 *
 * Il dialogo di scelta del salvataggio non c'e': si prende newData->dirName se
 * il titolo lo propone, altrimenti fixedList[0], focusDirName, o la prima
 * directory esistente. Layout guest (RPCS3 cellSaveData.h, big-endian):
 *   CellSaveDataSetList    +0 sortType +4 sortOrder +8 dirNamePrefix
 *   CellSaveDataSetBuf     +0 dirListMax +4 fileListMax +32 bufSize +36 buf
 *   CellSaveDataListGet    +0 dirNum +4 dirListNum +8 dirList
 *   CellSaveDataDirList    dirName[32] listParam[8] reserved[8]  (48)
 *   CellSaveDataListSet    +0 focusPosition +4 focusDirName +8 fixedListNum
 *                          +12 fixedList +16 newData +20 reserved
 *   CellSaveDataListNewData +0 iconPosition +4 dirName +8 icon
 *   CellSaveDataStatGet    (1640, vedi marshal_statget_init)
 *   CellSaveDataStatSet    +0 setParam +4 reCreateMode +8 indicator
 *   CellSaveDataFileGet    +0 excSize
 *   CellSaveDataFileSet    +0 fileOperation +8 fileType +28 fileName
 *                          +32 fileOffset +36 fileSize +40 fileBufSize +44 fileBuf
 * Le strutture stanno in fondo al buffer del titolo (setBuf->buf, se lungo
 * almeno 4 KB) e la lista delle directory in testa. I file vanno in
 * <s_save_root>/<dirName>/; i parametri (titolo, sottotitolo, dettaglio) in
 * PARAM.TXT della stessa directory, per ripresentarli al caricamento. */
static void sd_write_str(u32 ea, const char* str, u32 max)
{
    u32 i = 0; for (; str && str[i] && i + 1 < max; i++) vm_write8(ea + i, (u8)str[i]);
    vm_write8(ea + i, 0);
}
static void sd_read_str(u32 ea, char* out, u32 max)
{
    u32 i = 0; if (ea) for (; i + 1 < max; i++) { out[i] = (char)vm_read8(ea + i); if (!out[i]) break; }
    out[i] = 0;
}

static s32 savedata_list_guest(int is_save, u32 setList_ea, u32 setBuf_ea, u32 funcList,
                               u32 funcStat, u32 funcFile, u32 userdata)
{
    printf("[cellSaveData] %s(setList=0x%08X setBuf=0x%08X funcList=0x%08X funcStat=0x%08X funcFile=0x%08X userdata=0x%08X)\n",
           is_save ? "ListSave2" : "ListLoad2", setList_ea, setBuf_ea, funcList, funcStat, funcFile, userdata);
    if (!setList_ea || !setBuf_ea || !funcList || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;
    u32 buf = vm_read32(setBuf_ea + 36), bufSize = vm_read32(setBuf_ea + 32);
    u32 dirMax = vm_read32(setBuf_ea + 0);
    printf("[cellSaveData]   setBuf: dirListMax=%u fileListMax=%u bufSize=%u buf=0x%08X\n", dirMax, vm_read32(setBuf_ea + 4), bufSize, buf);
    if (!buf || bufSize < 48)
        return CELL_SAVEDATA_ERROR_PARAM;

    /* le strutture delle callback nello scratch HLE (il buffer del titolo e'
     * dimensionato per la sola lista: Uncharted da' 480 byte = 10 directory) */
    scratch_reset();
    u32 top = scratch_alloc(0x800);
    if (!top) return CELL_SAVEDATA_ERROR_INTERNAL;
    u32 cbres = top, listGet = top + 0x20, listSet = top + 0x40, statSet = top + 0x60,
        fileGet = top + 0x80, fileSet = top + 0xD0, statGet = top + 0x100;   /* +1640 < 0x800 */

    /* la lista delle directory esistenti (prefisso) nel buffer del titolo */
    char prefix[64]; sd_read_str(vm_read32(setList_ea + 8), prefix, sizeof prefix);
    u32 maxEntries = bufSize / 48; if (dirMax && dirMax < maxEntries) maxEntries = dirMax;
    CellSaveDataDirList* tmp = (CellSaveDataDirList*)calloc(maxEntries ? maxEntries : 1, sizeof(CellSaveDataDirList));
    u32 count = enumerate_save_dirs(prefix, tmp, maxEntries);
    for (u32 i = 0; i < count; i++) {
        u32 e = buf + i * 48;
        for (u32 o = 0; o < 48; o += 4) vm_write32(e + o, 0);
        sd_write_str(e, tmp[i].dirName, 32);
    }
    free(tmp);
    vm_write32(listGet + 0, count); vm_write32(listGet + 4, count); vm_write32(listGet + 8, buf);

    /* funcList */
    marshal_cbresult_init(cbres, CELL_SAVEDATA_CBRESULT_OK_NEXT, userdata);
    printf("[cellSaveData] %s: %u salvataggi con prefisso '%s'; funcList=0x%08X\n",
           is_save ? "ListSave2" : "ListLoad2", count, prefix, funcList);
    ps3_invoke_guest(funcList, cbres, listGet, listSet, 0, 0, 0, 0, 0);
    s32 result = (s32)vm_read32(cbres);
    if (result < 0) { printf("[cellSaveData] funcList -> %d\n", result); return CELL_SAVEDATA_ERROR_CBRESULT; }

    /* la directory: newData, fixedList[0], focusDirName, o la prima */
    char dirName[33] = {0};
    u32 newData = vm_read32(listSet + 16), fixedNum = vm_read32(listSet + 8),
        fixedList = vm_read32(listSet + 12), focus = vm_read32(listSet + 4);
    if (newData && vm_read32(newData + 4)) sd_read_str(vm_read32(newData + 4), dirName, sizeof dirName);
    else if (fixedNum && fixedList) sd_read_str(fixedList, dirName, sizeof dirName);
    else if (focus) sd_read_str(focus, dirName, sizeof dirName);
    else if (count) sd_read_str(buf, dirName, sizeof dirName);
    if (!dirName[0]) { printf("[cellSaveData] funcList: nessuna directory scelta\n"); return CELL_SAVEDATA_ERROR_NODATA; }

    char save_path[1024]; build_save_path(save_path, sizeof save_path, dirName);
    int is_new = !dir_exists(save_path);
    if (!is_save && is_new) { printf("[cellSaveData] ListLoad2: '%s' non esiste\n", dirName); return CELL_SAVEDATA_ERROR_NODATA; }
    if (is_save) ensure_dirs(save_path);
    printf("[cellSaveData] %s dir='%s' (nuova=%d)\n", is_save ? "SAVE" : "LOAD", dirName, is_new);

    /* funcStat */
    marshal_cbresult_init(cbres, CELL_SAVEDATA_CBRESULT_OK_NEXT, userdata);
    marshal_statget_init(statGet, is_new, dirName, 0, 0);
    if (!is_new) {   /* i parametri salvati la volta prima */
        char pp[1100]; snprintf(pp, sizeof pp, "%s/PARAM.TXT", save_path);
        FILE* pf = fopen(pp, "rb");
        if (pf) { char line[1200];
            while (fgets(line, sizeof line, pf)) { size_t L = strlen(line); while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
                if (!strncmp(line, "title=", 6)) sd_write_str(statGet + 64, line + 6, 128);
                else if (!strncmp(line, "subtitle=", 9)) sd_write_str(statGet + 64 + 128, line + 9, 128);
                else if (!strncmp(line, "detail=", 7)) sd_write_str(statGet + 64 + 256, line + 7, 1024); }
            fclose(pf); }
    }
    ps3_invoke_guest(funcStat, cbres, statGet, statSet, 0, 0, 0, 0, 0);
    result = (s32)vm_read32(cbres);
    if (result < 0) { printf("[cellSaveData] funcStat -> %d\n", result); return CELL_SAVEDATA_ERROR_CBRESULT; }
    if (is_save) {
        u32 setParam = vm_read32(statSet + 0);
        if (setParam) {
            char title[129], sub[129], det[1025];
            sd_read_str(setParam, title, sizeof title); sd_read_str(setParam + 128, sub, sizeof sub); sd_read_str(setParam + 256, det, sizeof det);
            char pp[1100]; snprintf(pp, sizeof pp, "%s/PARAM.TXT", save_path);
            FILE* pf = fopen(pp, "wb");
            if (pf) { fprintf(pf, "title=%s\nsubtitle=%s\ndetail=%s\n", title, sub, det); fclose(pf); }
            printf("[cellSaveData] setParam: '%s' / '%s'\n", title, sub);
        }
    }
    if (result == CELL_SAVEDATA_CBRESULT_OK_LAST) return CELL_OK;

    /* funcFile, finche' il titolo risponde OK_NEXT */
    for (int iter = 0; iter < 256; iter++) {
        marshal_cbresult_init(cbres, CELL_SAVEDATA_CBRESULT_OK_NEXT, userdata);
        for (u32 o = 0; o < 0x30; o += 4) vm_write32(fileSet + o, 0);
        vm_write32(fileGet, 0);
        ps3_invoke_guest(funcFile, cbres, fileGet, fileSet, 0, 0, 0, 0, 0);
        result = (s32)vm_read32(cbres);
        if (result < 0) { printf("[cellSaveData] funcFile -> %d\n", result); return CELL_SAVEDATA_ERROR_CBRESULT; }
        u32 op = vm_read32(fileSet + 0), type = vm_read32(fileSet + 8), name_ea = vm_read32(fileSet + 28);
        u32 off = vm_read32(fileSet + 32), size = vm_read32(fileSet + 36), bufsz = vm_read32(fileSet + 40), fbuf = vm_read32(fileSet + 44);
        char fname[64];
        switch (type) {
        case CELL_SAVEDATA_FILETYPE_CONTENT_ICON0: strcpy(fname, "ICON0.PNG"); break;
        case CELL_SAVEDATA_FILETYPE_CONTENT_ICON1: strcpy(fname, "ICON1.PAM"); break;
        case CELL_SAVEDATA_FILETYPE_CONTENT_PIC1:  strcpy(fname, "PIC1.PNG");  break;
        case CELL_SAVEDATA_FILETYPE_CONTENT_SND0:  strcpy(fname, "SND0.AT3");  break;
        default: sd_read_str(name_ea, fname, sizeof fname); break;
        }
        char fpath[1100]; snprintf(fpath, sizeof fpath, "%s/%s", save_path, fname);
        u32 done = 0;
        if (fname[0]) {
            if (op == CELL_SAVEDATA_FILEOP_WRITE || op == CELL_SAVEDATA_FILEOP_WRITE_NOTRUNC) {
                FILE* f = fopen(fpath, (op == CELL_SAVEDATA_FILEOP_WRITE && off == 0) ? "wb" : "r+b");
                if (!f && op != CELL_SAVEDATA_FILEOP_WRITE) f = fopen(fpath, "wb");
                if (!f) f = fopen(fpath, "wb");
                if (f) { fseeko(f, (off_t)off, SEEK_SET); u32 n = size < bufsz ? size : bufsz;
                         if (fbuf && n) done = (u32)fwrite(vm_base + fbuf, 1, n, f); fclose(f); }
            } else if (op == CELL_SAVEDATA_FILEOP_READ) {
                FILE* f = fopen(fpath, "rb");
                if (f) { fseeko(f, (off_t)off, SEEK_SET); u32 n = size < bufsz ? size : bufsz;
                         if (fbuf && n) done = (u32)fread(vm_base + fbuf, 1, n, f); fclose(f); }
            } else if (op == CELL_SAVEDATA_FILEOP_DELETE) {
                remove(fpath); done = 0;
            }
        }
        vm_write32(fileGet, done);
        printf("[cellSaveData] funcFile: op=%u tipo=%u '%s' off=%u size=%u -> %u byte, result=%d\n",
               op, type, fname, off, size, done, result);
        if (result == CELL_SAVEDATA_CBRESULT_OK_LAST) break;
    }
    return CELL_OK;
}

s32 cellSaveDataListSave2(u32 version, u32 setList_ea, u32 setBuf_ea, u32 funcList,
                          u32 funcStat, u32 funcFile, u32 container, u32 userdata)
{
    (void)version; (void)container;
    return savedata_list_guest(1, setList_ea, setBuf_ea, funcList, funcStat, funcFile, userdata);
}

s32 cellSaveDataListLoad2(u32 version, u32 setList_ea, u32 setBuf_ea, u32 funcList,
                          u32 funcStat, u32 funcFile, u32 container, u32 userdata)
{
    (void)version; (void)container;
    return savedata_list_guest(0, setList_ea, setBuf_ea, funcList, funcStat, funcFile, userdata);
}

s32 cellSaveDataFixedSave2(u32 version, CellSaveDataSetList* setList,
                            CellSaveDataSetBuf* setBuf,
                            CellSaveDataFixedCallback funcFixed,
                            CellSaveDataStatCallback funcStat,
                            CellSaveDataFileCallback funcFile,
                            u32 container, void* userdata)
{
    printf("[cellSaveData] FixedSave2(version=%u)\n", version);

    if (!setList || !setBuf || !funcFixed || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    /* CellSaveDataSetList is { u32 sortType; u32 sortOrder; char* dirNamePrefix; }
     * and CellSaveDataSetBuf starts with u32 dirListMax -- guest layout, so the
     * prefix EA is the word at +8 and needs translating before it is used as a
     * host string. The host structs put dirNamePrefix at +8 as an 8-byte
     * pointer, so a cast would have read half of it plus padding. */
    u32 dir_max = vm_read32((u32)(uintptr_t)setBuf + 0);
    if (dir_max == 0) dir_max = CELL_SAVEDATA_DIRLIST_MAX;
    CellSaveDataDirList* dirList = (CellSaveDataDirList*)calloc(dir_max, sizeof(CellSaveDataDirList));

    u32 dirCount = enumerate_save_dirs(
        savedata_host_str((const char*)(uintptr_t)vm_read32((u32)(uintptr_t)setList + 8)) ?
            savedata_host_str((const char*)(uintptr_t)vm_read32((u32)(uintptr_t)setList + 8)) : "",
        dirList, dir_max);

    CellSaveDataCBResult cbResult;
    memset(&cbResult, 0, sizeof(cbResult));
    cbResult.result = CELL_SAVEDATA_CBRESULT_OK_NEXT;

    CellSaveDataListGet listGet;
    memset(&listGet, 0, sizeof(listGet));
    listGet.dirListNum = dirCount < dir_max ? dirCount : dir_max;
    listGet.dirList = dirList;

    CellSaveDataListSet listSet;
    memset(&listSet, 0, sizeof(listSet));

    funcFixed(&cbResult, &listGet, &listSet);

    if (cbResult.result < 0) {
        free(dirList);
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }

    const char* selectedDir = NULL;
    if (listSet.fixedList && listSet.fixedListNum > 0) {
        selectedDir = listSet.fixedList[0].dirName;
    } else if (listSet.focusDirName) {
        selectedDir = listSet.focusDirName;
    }

    if (!selectedDir || selectedDir[0] == '\0') {
        free(dirList);
        return CELL_SAVEDATA_ERROR_NODATA;
    }

    s32 result = savedata_execute(selectedDir, 1, setBuf, funcStat, funcFile, userdata);
    free(dirList);
    return result;
}

s32 cellSaveDataFixedLoad2(u32 version, CellSaveDataSetList* setList,
                            CellSaveDataSetBuf* setBuf,
                            CellSaveDataFixedCallback funcFixed,
                            CellSaveDataStatCallback funcStat,
                            CellSaveDataFileCallback funcFile,
                            u32 container, void* userdata)
{
    printf("[cellSaveData] FixedLoad2(version=%u)\n", version);

    if (!setList || !setBuf || !funcFixed || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    /* CellSaveDataSetList is { u32 sortType; u32 sortOrder; char* dirNamePrefix; }
     * and CellSaveDataSetBuf starts with u32 dirListMax -- guest layout, so the
     * prefix EA is the word at +8 and needs translating before it is used as a
     * host string. The host structs put dirNamePrefix at +8 as an 8-byte
     * pointer, so a cast would have read half of it plus padding. */
    u32 dir_max = vm_read32((u32)(uintptr_t)setBuf + 0);
    if (dir_max == 0) dir_max = CELL_SAVEDATA_DIRLIST_MAX;
    CellSaveDataDirList* dirList = (CellSaveDataDirList*)calloc(dir_max, sizeof(CellSaveDataDirList));

    u32 dirCount = enumerate_save_dirs(
        savedata_host_str((const char*)(uintptr_t)vm_read32((u32)(uintptr_t)setList + 8)) ?
            savedata_host_str((const char*)(uintptr_t)vm_read32((u32)(uintptr_t)setList + 8)) : "",
        dirList, dir_max);

    CellSaveDataCBResult cbResult;
    memset(&cbResult, 0, sizeof(cbResult));
    cbResult.result = CELL_SAVEDATA_CBRESULT_OK_NEXT;

    CellSaveDataListGet listGet;
    memset(&listGet, 0, sizeof(listGet));
    listGet.dirListNum = dirCount < dir_max ? dirCount : dir_max;
    listGet.dirList = dirList;

    CellSaveDataListSet listSet;
    memset(&listSet, 0, sizeof(listSet));

    funcFixed(&cbResult, &listGet, &listSet);

    if (cbResult.result < 0) {
        free(dirList);
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }

    const char* selectedDir = NULL;
    if (listSet.fixedList && listSet.fixedListNum > 0) {
        selectedDir = listSet.fixedList[0].dirName;
    } else if (listSet.focusDirName) {
        selectedDir = listSet.focusDirName;
    }

    if (!selectedDir || selectedDir[0] == '\0') {
        free(dirList);
        return CELL_SAVEDATA_ERROR_NODATA;
    }

    s32 result = savedata_execute(selectedDir, 0, setBuf, funcStat, funcFile, userdata);
    free(dirList);
    return result;
}

s32 cellSaveDataAutoSave2(u32 version, const char* dirName,
                           u32 errDialog,
                           CellSaveDataSetBuf* setBuf,
                           CellSaveDataStatCallback funcStat,
                           CellSaveDataFileCallback funcFile,
                           u32 container, void* userdata)
{
    dirName = savedata_host_str(dirName);
    printf("[cellSaveData] AutoSave2(version=%u, dir='%s')\n",
           version, dirName ? dirName : "<null>");

    if (!dirName || !setBuf || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    return savedata_execute(dirName, 1, setBuf, funcStat, funcFile, userdata);
}

s32 cellSaveDataAutoLoad2(u32 version, const char* dirName,
                           u32 errDialog,
                           CellSaveDataSetBuf* setBuf,
                           CellSaveDataStatCallback funcStat,
                           CellSaveDataFileCallback funcFile,
                           u32 container, void* userdata)
{
    dirName = savedata_host_str(dirName);
    (void)version; (void)errDialog; (void)setBuf; (void)funcFile;
    (void)container;
    printf("[cellSaveData] AutoLoad2(version=%u, dir='%s')\n",
           version, dirName ? dirName : "<null>");

    if (!dirName || !setBuf || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    /* funcStat is the GAME's guest-OPD address. Marshal StatGet/Set/CB
     * into guest BE memory and dispatch through g_ps3_guest_caller.
     * Even on first-run-no-data, the game observes funcStat fire with
     * isNewData=1 so its title state machine can advance. */
    char save_path[1024];
    build_save_path(save_path, sizeof(save_path), dirName);
    int is_new = !dir_exists(save_path);

    uint32_t func_opd = (uint32_t)(uintptr_t)funcStat;
    /* userdata arrives as a GUEST address in a pointer type (same convention as
     * funcStat/dirName). The callback recovers its own object from it. */
    uint32_t userdata_ea = (uint32_t)(uintptr_t)userdata;
    s32 cb = dispatch_func_stat(func_opd, is_new, dirName, userdata_ea);

    if (cb < 0) {
        /* flОw first-boot: its funcStat returns ERR_NODATA on a new profile
         * (isNewData=1). The callback already ran and told the game "no save",
         * so report AutoLoad as CELL_OK -- an ERROR return leaves the title
         * parked in MODE_AUTO_LOAD (no app loop, no flips).
         * Per the SDK the correct return here is CELL_SAVEDATA_ERROR_NODATA,
         * and a real title handles it; that flОw does not is a bug somewhere in
         * its MODE_AUTO_LOAD state machine we have not tracked down. Keep the
         * compat return until that is understood -- it was dropped once already
         * in the fold merge and cost a boot regression. */
        if (cb == CELL_SAVEDATA_CBRESULT_ERR_NODATA)
            return CELL_OK;
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }
    /* OK_LAST or OK_NEXT — with no actual file load infrastructure for
     * now, succeed without invoking funcFile. */
    return CELL_OK;
}

s32 cellSaveDataDelete2(u32 container)
{
    printf("[cellSaveData] Delete2(container=%u)\n", container);

    /* Without a directory name we can't delete anything meaningful.
       This variant typically shows a UI for deletion - just succeed. */
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Old / non-_2 variants — same wrapper pattern as the _2 versions, with
 * the same guest-callback caveat.
 *
 * Older PS3 SDK builds (e.g. our flOw NPUA80001 dump) link the original
 * cellSaveDataAutoSave / AutoLoad / Delete instead of the _2 variants
 * RPCS3's flOw build uses. Same semantics, different NID.
 * -----------------------------------------------------------------------*/
s32 cellSaveDataAutoSave(u32 version, const char* dirName,
                          u32 errDialog,
                          CellSaveDataSetBuf* setBuf,
                          CellSaveDataStatCallback funcStat,
                          CellSaveDataFileCallback funcFile,
                          u32 container, void* userdata)
{
    dirName = savedata_host_str(dirName);
    (void)errDialog; (void)funcFile; (void)container;
    printf("[cellSaveData] AutoSave(version=%u, dir='%s')\n",
           version, dirName ? dirName : "<null>");
    if (!dirName || !setBuf || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    /* This used to return CELL_OK WITHOUT running funcStat -- the game believed
     * its profile saved while nothing happened (silent data loss). Run the
     * guest funcStat callback the same proven way cellSaveDataAutoLoad does
     * (dispatch_func_stat marshals through g_ps3_guest_caller). The callback
     * fires and returns a real result the game can act on. (Persisting the file
     * bytes needs the funcFile marshalling loop; the stat callback running is
     * the correct, honest first step and matches the load path's behaviour.) */
    char save_path[1024];
    build_save_path(save_path, sizeof(save_path), dirName);
    int is_new = !dir_exists(save_path);

    uint32_t func_opd     = (uint32_t)(uintptr_t)funcStat;
    uint32_t userdata_ea  = (uint32_t)(uintptr_t)userdata;
    s32 cb = dispatch_func_stat(func_opd, is_new, dirName, userdata_ea);

    if (cb < 0) {
        if (cb == CELL_SAVEDATA_CBRESULT_ERR_NODATA)
            return CELL_SAVEDATA_ERROR_NODATA;
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }
    return CELL_OK;
}

s32 cellSaveDataAutoLoad(u32 version, const char* dirName,
                          u32 errDialog,
                          CellSaveDataSetBuf* setBuf,
                          CellSaveDataStatCallback funcStat,
                          CellSaveDataFileCallback funcFile,
                          u32 container, void* userdata)
{
    dirName = savedata_host_str(dirName);
    (void)version; (void)errDialog; (void)setBuf; (void)funcFile;
    (void)container;
    printf("[cellSaveData] AutoLoad(version=%u, dir='%s')\n",
           version, dirName ? dirName : "<null>");
    if (!dirName || !setBuf || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    char save_path[1024];
    build_save_path(save_path, sizeof(save_path), dirName);
    int is_new = !dir_exists(save_path);

    uint32_t func_opd = (uint32_t)(uintptr_t)funcStat;
    /* userdata arrives as a GUEST address in a pointer type (same convention as
     * funcStat/dirName). The callback recovers its own object from it. */
    uint32_t userdata_ea = (uint32_t)(uintptr_t)userdata;
    s32 cb = dispatch_func_stat(func_opd, is_new, dirName, userdata_ea);

    if (cb < 0) {
        /* Same first-boot compat return as cellSaveDataAutoLoad2 above (flОw
         * calls this old non-_2 variant): ERR_NODATA from funcStat on a new
         * profile must not surface as an error, or the title parks in
         * MODE_AUTO_LOAD. See the longer note there. */
        if (cb == CELL_SAVEDATA_CBRESULT_ERR_NODATA)
            return CELL_OK;
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }
    return CELL_OK;
}

s32 cellSaveDataDelete(u32 version, const char* dirName,
                        u32 container)
{
    (void)version;
    dirName = savedata_host_str(dirName);
    printf("[cellSaveData] Delete(dir='%s', container=%u)\n",
           dirName ? dirName : "<null>", container);
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * User variants (cellSaveDataUserAutoSave/Load): same as the non-User forms
 * with a userId inserted after version. The trailing userdata is the guest's
 * NINTH argument (on the guest stack, not in r3-r10) — the generic adapter
 * only passes 8 registers, so declare 8 params and pass NULL through.
 * LittleBigPlanet auto-loads its profile through UserAutoLoad at boot.
 * -----------------------------------------------------------------------*/
s32 cellSaveDataUserAutoSave(u32 version, u32 userId, const char* dirName,
                             u32 errDialog, CellSaveDataSetBuf* setBuf,
                             CellSaveDataStatCallback funcStat,
                             CellSaveDataFileCallback funcFile, u32 container)
{
    (void)userId;
    printf("[cellSaveData] UserAutoSave(user=%u)\n", userId);
    return cellSaveDataAutoSave(version, dirName, errDialog, setBuf,
                                funcStat, funcFile, container, NULL);
}

/* NOTE: the User* entry points take NINE arguments -- cellSaveDataUserAutoLoad
 * inserts `userId` ahead of dirName, which pushes `userdata` into the ninth
 * slot. The generic HLE adapter only forwards r3..r10 (eight), so the ninth
 * argument is unreachable here and MUST come from the caller's parameter save
 * area. That is what hle_cellSaveDataUserAutoLoad (below) is for; this plain-C
 * entry only sees eight and cannot know userdata. It stays for direct/internal
 * callers, and the ctx handler overrides it on the NID path. */
/* ---------------------------------------------------------------------------
 * Nine-argument entry points
 *
 * cellSaveDataUserAutoLoad/UserAutoSave take nine arguments; the generic HLE
 * adapter forwards only r3..r10, so the ninth (userdata) never arrives and used
 * to be hardcoded to NULL. That is fatal: a funcStat callback typically recovers
 * its own object from cbResult->userdata. LBP's is
 *     v = cbResult->userdata;  r = sub(v + 104, cbResult, get, set);
 * so a NULL made it work off guest address 104, read the VM's zero-fill, and
 * return CELL_SAVEDATA_CBRESULT_ERR_NODATA (-4) -- it never reached its handler.
 *
 * Take the full context and read the ninth argument from the caller's parameter
 * save area. SAVEDATA_ARG9_SP_OFF can be overridden at runtime
 * (PS3_SAVEDATA_ARG9_OFF) and the surrounding window is dumped once, so the
 * offset is confirmed from a real call rather than assumed.
 * -----------------------------------------------------------------------*/
#include "../../runtime/ppu/ppu_context.h"

extern void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));

/* PPC64 ELFv1: back chain, CR, LR, 2 reserved, TOC = 48 bytes, then the
 * parameter save area shadowing r3.. one doubleword each -> arg9 at SP+112. */
#define SAVEDATA_ARG9_SP_OFF 112u

static uint32_t savedata_arg9(ppu_context* ctx)
{
    uint32_t sp = (uint32_t)ctx->gpr[1];
    static int off = -1;
    if (off < 0) {
        const char* e = getenv("PS3_SAVEDATA_ARG9_OFF");
        off = e ? (int)strtol(e, NULL, 0) : (int)SAVEDATA_ARG9_SP_OFF;
    }
    { static int dumped = 0;
      if (!dumped) { dumped = 1;
        printf("[cellSaveData] caller SP=0x%08X param-save-area window:\n", sp);
        for (uint32_t o = 48; o <= 136; o += 8)
            printf("    SP+%3u = 0x%016llX%s\n", o,
                   (unsigned long long)vm_read64(sp + o),
                   o == (uint32_t)off ? "   <- taking as arg9 (userdata)" : "");
      } }
    return (uint32_t)vm_read64(sp + (uint32_t)off);
}

static void hle_cellSaveDataUserAutoLoad(ppu_context* ctx)
{
    u32   version   = (u32)ctx->gpr[3];
    u32   userId    = (u32)ctx->gpr[4];
    const char* dir = (const char*)(uintptr_t)(u32)ctx->gpr[5];
    u32   errDialog = (u32)ctx->gpr[6];
    void* setBuf    = (void*)(uintptr_t)(u32)ctx->gpr[7];
    void* funcStat  = (void*)(uintptr_t)(u32)ctx->gpr[8];
    void* funcFile  = (void*)(uintptr_t)(u32)ctx->gpr[9];
    u32   container = (u32)ctx->gpr[10];
    uint32_t userdata = savedata_arg9(ctx);

    printf("[cellSaveData] UserAutoLoad(user=%u, userdata=0x%08X)\n", userId, userdata);
    ctx->gpr[3] = (uint64_t)(int64_t)(s32)
        cellSaveDataAutoLoad2(version, dir, errDialog,
                              (CellSaveDataSetBuf*)setBuf,
                              (CellSaveDataStatCallback)funcStat,
                              (CellSaveDataFileCallback)funcFile,
                              container, (void*)(uintptr_t)userdata);
}

void cellSaveData_register_ctx_handlers(void)
{
    /* Overrides the generated 8-arg registration: ctx handlers are dispatched
     * before the generic table. */
    ps3_hle_register_ctx(0xCDC6AEFDu, "cellSaveDataUserAutoLoad",
                         hle_cellSaveDataUserAutoLoad);
}

s32 cellSaveDataUserAutoLoad(u32 version, u32 userId, const char* dirName,
                             u32 errDialog, CellSaveDataSetBuf* setBuf,
                             CellSaveDataStatCallback funcStat,
                             CellSaveDataFileCallback funcFile, u32 container)
{
    (void)userId;
    printf("[cellSaveData] UserAutoLoad(user=%u) [8-arg path: userdata unavailable]\n", userId);
    return cellSaveDataAutoLoad(version, dirName, errDialog, setBuf,
                                funcStat, funcFile, container, NULL);
}

/* cellSaveDataListAutoLoad (NID 0x21425307) --
 *   (version, errDialog, setList*, setBuf*, funcFixed, funcStat, funcFile,
 *    container, userdata)  -- 9 argomenti: userdata sta sullo stack guest.
 *
 * Le callback sono funzioni GUEST (OPD): vanno chiamate con ps3_invoke_guest
 * e con le strutture in memoria guest, non come puntatori host (le altre
 * funzioni di questo file lo fanno ancora: crashano in un port ricompilato).
 * Layout (RPCS3 cellSaveData.h, big-endian):
 *   CellSaveDataSetList  +0 sortType +4 sortOrder +8 dirNamePrefix +12 reserved
 *   CellSaveDataSetBuf   +0 dirListMax +4 fileListMax +8 reserved[6] +32 bufSize +36 buf
 *   CellSaveDataCBResult +0 result +4 progressBarInc +8 errNeedSizeKB +12 invalidMsg +16 userdata
 *   CellSaveDataListGet  +0 dirNum +4 dirListNum +8 dirList
 *   CellSaveDataFixedSet +0 dirName +4 newIcon +8 option
 * Senza salvataggi su disco: funcFixed riceve una lista vuota; se risponde con
 * un errore -> CELL_SAVEDATA_ERROR_CBRESULT, altrimenti la directory scelta
 * non esiste -> CELL_SAVEDATA_ERROR_NODATA (come RPCS3 per le LOAD). funcStat
 * e funcFile non vengono mai chiamate. Uncharted la chiama dal thread
 * "Save/Load Game Thread" all'ingresso nel menu. */
s32 cellSaveDataListAutoLoad(u32 version, u32 errDialog, u32 setList_ea, u32 setBuf_ea,
                             u32 funcFixed, u32 funcStat, u32 funcFile, u32 container)
{
    (void)version; (void)errDialog; (void)funcStat; (void)funcFile; (void)container;
    extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
    u32 userdata = g_active_ctx ? vm_read32((u32)g_active_ctx->gpr[1] + 0x74) : 0;
    printf("[cellSaveData] ListAutoLoad(setList=0x%08X setBuf=0x%08X funcFixed=0x%08X userdata=0x%08X)\n",
           setList_ea, setBuf_ea, funcFixed, userdata);
    if (!setList_ea || !setBuf_ea || !funcFixed)
        return CELL_SAVEDATA_ERROR_PARAM;

    u32 buf = vm_read32(setBuf_ea + 36), bufSize = vm_read32(setBuf_ea + 32);
    if (!buf || bufSize < 0x100)
        return CELL_SAVEDATA_ERROR_PARAM;
    /* le tre strutture della callback in fondo al buffer del titolo */
    u32 cbres = (buf + bufSize - 0x80) & ~15u, listGet = cbres + 0x20, fixedSet = cbres + 0x40;
    for (u32 o = 0; o < 0x60; o += 4) vm_write32(cbres + o, 0);
    vm_write32(cbres + 16, userdata);
    vm_write32(listGet + 8, buf);          /* dirList: vuota */

    ps3_invoke_guest(funcFixed, cbres, listGet, fixedSet, 0, 0, 0, 0, 0);

    s32 result = (s32)vm_read32(cbres + 0);
    u32 dirName = vm_read32(fixedSet + 0);
    char name[33] = {0};
    if (dirName) { for (int i = 0; i < 32; i++) { name[i] = (char)vm_read8(dirName + i); if (!name[i]) break; } }
    printf("[cellSaveData] ListAutoLoad: funcFixed -> result=%d dirName='%s'\n", result, name);
    if (result < 0)
        return CELL_SAVEDATA_ERROR_CBRESULT;
    return CELL_SAVEDATA_ERROR_NODATA;
}
