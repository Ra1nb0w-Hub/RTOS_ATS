/**
 * @file ats_fs.c
 * @brief ATS文件系统功能 - Windows平台实现
 *
 * 使用标准C文件I/O（fopen/fclose/fread/fwrite/fseek）实现，
 * 兼容MSVC与MinGW/GCC工具链。
 *
 * 所有文件操作基于默认根目录，运行时自动获取：
 *   Windows: %LOCALAPPDATA%/ATS/ats_data/
 *   Linux:   ~/.local/share/ATS/ats_data/
 *
 * 传入的路径如 "exdata/config.dat" 会被解析为上述根目录下的完整路径。
 * 当使用 ATS_FS_FLAG_CREATE 打开文件时，会自动创建路径中不存在的目录。
 */

#include "ats_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <io.h>      /* _access, _mkdir */
#include <direct.h>  /* _mkdir */
#include <windows.h> /* GetModuleFileNameW */
#include <shlwapi.h> /* PathCombineA */
#else
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#endif

/* =========================================================
 * 默认存储根目录
 * Windows: %LOCALAPPDATA%/ATS/ats_data/
 * Linux:   ~/.local/share/ATS/ats_data/
 * ========================================================= */

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

static char s_fs_root[MAX_PATH] = {0}; /* 运行时初始化 */

static void init_fs_root(void)
{
    if (s_fs_root[0] != '\0')
        return;

#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");
    if (local)
    {
        snprintf(s_fs_root, sizeof(s_fs_root), "%s\\ATS\\ats_data\\", local);
    }
    else
    {
        /* 回退：使用 exe 所在目录 */
        char exePath[MAX_PATH];
        if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0)
        {
            char *lastSlash = strrchr(exePath, '\\');
            if (lastSlash) *lastSlash = '\0';
            snprintf(s_fs_root, sizeof(s_fs_root), "%s\\ats_data\\", exePath);
        }
        else
        {
            strcpy(s_fs_root, ".\\ats_data\\");
        }
    }
#else
    const char *home = getenv("HOME");
    if (home)
    {
        snprintf(s_fs_root, sizeof(s_fs_root), "%s/.local/share/ATS/ats_data/", home);
    }
    else
    {
        strcpy(s_fs_root, "./ats_data/");
    }
#endif
}

#define ATS_FS_ROOT (s_fs_root)

/* Windows下 stat 宏 */
#ifdef _WIN32
#define F_OK 0
#endif

/* =========================================================
 * 内部句柄表
 * 将 ats_fs_handle_t (int) 映射到 FILE*
 * 使用简单的固定大小表（最多支持 ATS_FS_MAX_FILES 个同时打开的文件）
 * ========================================================= */

#define ATS_FS_MAX_FILES 64
#define ATS_FS_INVALID_HANDLE (-1)

static FILE *s_file_table[ATS_FS_MAX_FILES] = {NULL};

static int alloc_handle(FILE *fp)
{
    for (int i = 0; i < ATS_FS_MAX_FILES; i++)
    {
        if (s_file_table[i] == NULL)
        {
            s_file_table[i] = fp;
            return i;
        }
    }
    return ATS_FS_INVALID_HANDLE;
}

static FILE *get_file(ats_fs_handle_t handle)
{
    if (handle < 0 || handle >= ATS_FS_MAX_FILES)
        return NULL;
    return s_file_table[handle];
}

static void free_handle(ats_fs_handle_t handle)
{
    if (handle >= 0 && handle < ATS_FS_MAX_FILES)
    {
        s_file_table[handle] = NULL;
    }
}

/* =========================================================
 * 内部辅助函数
 * ========================================================= */

/**
 * @brief 拼接完整路径：根目录 + 传入路径
 *
 * 返回由 malloc 分配的字符串，调用方需要 free()
 */
static char *build_full_path(const char *pathName)
{
    if (!pathName)
        return NULL;

    init_fs_root();

    size_t rootLen = strlen(ATS_FS_ROOT);
    size_t pathLen = strlen(pathName);
    char *fullPath = (char *)malloc(rootLen + pathLen + 1);
    if (!fullPath)
        return NULL;

    strcpy(fullPath, ATS_FS_ROOT);
    strcat(fullPath, pathName);
    return fullPath;
}

/**
 * @brief 递归创建目录
 *
 * 会修改 path 字符串（临时插入 \0），逐级创建每一层目录。
 * 只在 Windows 上使用 _mkdir，Linux 上使用 mkdir。
 */
static int mkdir_recursive(char *path)
{
    char *p = path;
    char sep = '/';

#ifdef _WIN32
    /* Windows 下同时支持 / 和 \，将 / 替换为 \ */
    for (p = path; *p; p++)
    {
        if (*p == '/')
            *p = '\\';
    }
    sep = '\\';
#endif

    /* 从第二级目录开始逐级创建 */
    for (p = path + 1; *p; p++)
    {
        if (*p == sep)
        {
            *p = '\0';
#ifdef _WIN32
            _mkdir(path);
#else
            mkdir(path, 0755);
#endif
            *p = sep;
        }
    }

#ifdef _WIN32
    return (_mkdir(path) == 0 || errno == EEXIST) ? 0 : -1;
#else
    return (mkdir(path, 0755) == 0 || errno == EEXIST) ? 0 : -1;
#endif
}

/**
 * @brief 从路径中提取目录部分
 *
 * 返回由 malloc 分配的字符串（以 \0 结尾），
 * 包含路径中最后一个分隔符之前的部分。
 * 例如 "exdata/config.dat" -> "exdata/"
 *      "config.dat"       -> "./"
 */
static char *extract_dir(const char *path)
{
    if (!path)
        return NULL;

    /* 找最后一个分隔符 */
    const char *lastSep = strrchr(path, '/');
#ifdef _WIN32
    const char *lastBS = strrchr(path, '\\');
    if (!lastSep || lastBS > lastSep)
        lastSep = lastBS;
#endif

    if (!lastSep)
    {
        /* 没有目录部分 */
        char *dir = (char *)malloc(3);
        if (dir)
        {
            strcpy(dir, ".\0");
        }
        return dir;
    }

    size_t len = (size_t)(lastSep - path) + 1; /* 包含分隔符 */
    char *dir = (char *)malloc(len + 1);
    if (!dir)
        return NULL;

    strncpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

/* =========================================================
 * 接口实现
 * ========================================================= */

int ats_fs_open(ats_fs_handle_t *handle, const char *pathName,
                ats_fs_open_flags_t flags, ats_fs_open_mode_t mode)
{
    if (!handle || !pathName)
        return -1;

    /* 拼接完整路径 */
    char *fullPath = build_full_path(pathName);
    if (!fullPath)
        return -1;

    const char *fmode = "rb"; /* 默认 */

    if (flags == ATS_FS_FLAG_CREATE)
    {
        /* 自动创建路径中不存在的目录 */
        char *dir = extract_dir(fullPath);
        if (dir)
        {
            mkdir_recursive(dir);
            free(dir);
        }

        /* 始终创建/覆盖 */
        switch (mode)
        {
        case ATS_FS_MODE_RDONLY:
            fmode = "w+b";
            break; /* 创建后可读写 */
        case ATS_FS_MODE_WRONLY:
            fmode = "wb";
            break;
        case ATS_FS_MODE_RDWR:
            fmode = "w+b";
            break;
        default:
            fmode = "w+b";
            break;
        }
    }
    else
    {
        /* ATS_FS_FLAG_OPEN：文件必须已存在 */
        switch (mode)
        {
        case ATS_FS_MODE_RDONLY:
            fmode = "rb";
            break;
        case ATS_FS_MODE_WRONLY:
            fmode = "r+b";
            break; /* 打开已存在文件仅写 */
        case ATS_FS_MODE_RDWR:
            fmode = "r+b";
            break;
        default:
            fmode = "rb";
            break;
        }
    }

    FILE *fp = fopen(fullPath, fmode);
    free(fullPath);

    if (!fp)
        return -1;

    int h = alloc_handle(fp);
    if (h == ATS_FS_INVALID_HANDLE)
    {
        fclose(fp);
        return -1;
    }

    *handle = (ats_fs_handle_t)h;
    return 0;
}

int ats_fs_close(ats_fs_handle_t handle)
{
    FILE *fp = get_file(handle);
    if (!fp)
        return -1;
    fclose(fp);
    free_handle(handle);
    return 0;
}

int ats_fs_read(ats_fs_handle_t handle, void *buf, size_t count)
{
    FILE *fp = get_file(handle);
    if (!fp || !buf)
        return -1;
    size_t n = fread(buf, 1, count, fp);
    return (int)n;
}

int ats_fs_write(ats_fs_handle_t handle, const void *buf, size_t count)
{
    FILE *fp = get_file(handle);
    if (!fp || !buf)
        return -1;
    size_t n = fwrite(buf, 1, count, fp);
    return (int)n;
}

int ats_fs_seek(ats_fs_handle_t handle, size_t offset, ats_fs_whence_t whence)
{
    FILE *fp = get_file(handle);
    if (!fp)
        return -1;

    int origin = SEEK_SET;
    switch (whence)
    {
    case ATS_FS_WHENCE_BEGIN:
        origin = SEEK_SET;
        break;
    case ATS_FS_WHENCE_CURRENT:
        origin = SEEK_CUR;
        break;
    case ATS_FS_WHENCE_END:
        origin = SEEK_END;
        break;
    default:
        origin = SEEK_SET;
        break;
    }

    return (fseek(fp, (long)offset, origin) == 0) ? 0 : -1;
}

int ats_fs_size(const char *pathName)
{
    if (!pathName)
        return -1;

    char *fullPath = build_full_path(pathName);
    if (!fullPath)
        return -1;

    struct stat st;
    int ret = (stat(fullPath, &st) == 0) ? (int)st.st_size : -1;
    free(fullPath);
    return ret;
}

int ats_fs_remove(const char *pathName)
{
    if (!pathName)
        return -1;

    char *fullPath = build_full_path(pathName);
    if (!fullPath)
        return -1;

    int ret = (remove(fullPath) == 0) ? 0 : -1;
    free(fullPath);
    return ret;
}

int ats_fs_exist(const char *pathName)
{
    if (!pathName)
        return -1;

    char *fullPath = build_full_path(pathName);
    if (!fullPath)
        return -1;

#ifdef _WIN32
    int ret = (_access(fullPath, F_OK) == 0) ? 0 : -1;
#else
    struct stat st;
    int ret = (stat(fullPath, &st) == 0) ? 0 : -1;
#endif
    free(fullPath);
    return ret;
}

char *ats_fs_fullpath(const char *pathName)
{
    return build_full_path(pathName);
}
