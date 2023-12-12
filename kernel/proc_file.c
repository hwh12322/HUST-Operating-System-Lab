/*
 * Interface functions between file system and kernel/processes. added @lab4_1
 */

#include "proc_file.h"
#include "vmm.h"
#include "hostfs.h"
#include "pmm.h"
#include "process.h"
#include "ramdev.h"
#include "rfs.h"
#include "riscv.h"
#include "spike_interface/spike_file.h"
#include "spike_interface/spike_utils.h"
#include "util/functions.h"
#include "util/string.h"

//
// initialize file system
//
void fs_init(void) {
  // initialize the vfs
  vfs_init();

  // register hostfs and mount it as the root
  if( register_hostfs() < 0 ) panic( "fs_init: cannot register hostfs.\n" );
  struct device *hostdev = init_host_device("HOSTDEV");
  vfs_mount("HOSTDEV", MOUNT_AS_ROOT);

  // register and mount rfs
  if( register_rfs() < 0 ) panic( "fs_init: cannot register rfs.\n" );
  struct device *ramdisk0 = init_rfs_device("RAMDISK0");
  rfs_format_dev(ramdisk0);
  vfs_mount("RAMDISK0", MOUNT_DEFAULT);
}

//
// initialize a proc_file_management data structure for a process.
// return the pointer to the page containing the data structure.
//
proc_file_management *init_proc_file_management(void) {
  proc_file_management *pfiles = (proc_file_management *)alloc_page();
  pfiles->cwd = vfs_root_dentry; // by default, cwd is the root
  pfiles->nfiles = 0;

  for (int fd = 0; fd < MAX_FILES; ++fd)
    pfiles->opened_files[fd].status = FD_NONE;

  sprint("FS: created a file management struct for a process.\n");
  return pfiles;
}

//
// reclaim the open-file management data structure of a process.
// note: this function is not used as PKE does not actually reclaim a process.
//
void reclaim_proc_file_management(proc_file_management *pfiles) {
  free_page(pfiles);
  return;
}

//
// get an opened file from proc->opened_file array.
// return: the pointer to the opened file structure.
//
struct file *get_opened_file(int fd) {
  struct file *pfile = NULL;

  // browse opened file list to locate the fd
  for (int i = 0; i < MAX_FILES; ++i) {
    pfile = &(current->pfiles->opened_files[i]);  // file entry
    if (i == fd) break;
  }
  if (pfile == NULL) panic("do_read: invalid fd!\n");
  return pfile;
}

//
// open a file named as "pathname" with the permission of "flags".
// return: -1 on failure; non-zero file-descriptor on success.
//
int do_open(char *pathname, int flags) {
  struct file *opened_file = NULL;
  if ((opened_file = vfs_open(pathname, flags)) == NULL) return -1;

  int fd = 0;
  if (current->pfiles->nfiles >= MAX_FILES) {
    panic("do_open: no file entry for current process!\n");
  }
  struct file *pfile;
  for (fd = 0; fd < MAX_FILES; ++fd) {
    pfile = &(current->pfiles->opened_files[fd]);
    if (pfile->status == FD_NONE) break;
  }

  // initialize this file structure
  memcpy(pfile, opened_file, sizeof(struct file));

  ++current->pfiles->nfiles;
  return fd;
}

//
// read content of a file ("fd") into "buf" for "count".
// return: actual length of data read from the file.
//
int do_read(int fd, char *buf, uint64 count) {
  struct file *pfile = get_opened_file(fd);

  if (pfile->readable == 0) panic("do_read: no readable file!\n");

  char buffer[count + 1];
  int len = vfs_read(pfile, buffer, count);
  buffer[count] = '\0';
  strcpy(buf, buffer);
  return len;
}

//
// write content ("buf") whose length is "count" to a file "fd".
// return: actual length of data written to the file.
//
int do_write(int fd, char *buf, uint64 count) {
  struct file *pfile = get_opened_file(fd);

  if (pfile->writable == 0) panic("do_write: cannot write file!\n");

  int len = vfs_write(pfile, buf, count);
  return len;
}

//
// reposition the file offset
//
int do_lseek(int fd, int offset, int whence) {
  struct file *pfile = get_opened_file(fd);
  return vfs_lseek(pfile, offset, whence);
}

//
// read the vinode information
//
int do_stat(int fd, struct istat *istat) {
  struct file *pfile = get_opened_file(fd);
  return vfs_stat(pfile, istat);
}

//
// read the inode information on the disk
//
int do_disk_stat(int fd, struct istat *istat) {
  struct file *pfile = get_opened_file(fd);
  return vfs_disk_stat(pfile, istat);
}

//
// close a file
//
int do_close(int fd) {
  struct file *pfile = get_opened_file(fd);
  return vfs_close(pfile);
}

//
// open a directory
// return: the fd of the directory file
//
int do_opendir(char *pathname) {
  struct file *opened_file = NULL;
  if ((opened_file = vfs_opendir(pathname)) == NULL) return -1;

  int fd = 0;
  struct file *pfile;
  for (fd = 0; fd < MAX_FILES; ++fd) {
    pfile = &(current->pfiles->opened_files[fd]);
    if (pfile->status == FD_NONE) break;
  }
  if (pfile->status != FD_NONE)  // no free entry
    panic("do_opendir: no file entry for current process!\n");

  // initialize this file structure
  memcpy(pfile, opened_file, sizeof(struct file));

  ++current->pfiles->nfiles;
  return fd;
}

//
// read a directory entry
//
int do_readdir(int fd, struct dir *dir) {
  struct file *pfile = get_opened_file(fd);
  return vfs_readdir(pfile, dir);
}

//
// make a new directory
//
int do_mkdir(char *pathname) {
  return vfs_mkdir(pathname);
}

//
// close a directory
//
int do_closedir(int fd) {
  struct file *pfile = get_opened_file(fd);
  return vfs_closedir(pfile);
}

//
// create hard link to a file
//
int do_link(char *oldpath, char *newpath) {
  return vfs_link(oldpath, newpath);
}

//
// remove a hard link to a file
//
int do_unlink(char *path) {
  return vfs_unlink(path);
}

char *strrchr(const char *str, int ch) {
    const char *last = NULL;
    for (; *str != '\0'; str++) {
        if (*str == ch) {
            last = str;
        }
    }
    return (char *)last;
}

void resolve_path(char* dest, char* relapath) {
    char* cwd = current->cwd; // 当前工作目录
    int relapath_len = strlen(relapath); // 相对路径的长度
    int cwd_len = strlen(cwd); // 当前工作目录的长度

    // 检查相对路径是否以 "../" 开头
    if (relapath[1] == '.' ) {
        int flag = FALSE; // 标记是否找到当前工作目录中的最后一个 '/'
        // 倒序遍历当前工作目录以找到最后一个 '/'
        for (int i = cwd_len; i >= 0; i--) {
            dest[i] = '\0'; // 初始化目标字符串
            if (flag) {
                // 如果找到了 '/', 开始复制当前工作目录到目标字符串
                dest[i] = cwd[i];
            }
            if (cwd[i - 1] == '/') {
                // 找到最后一个 '/'，更新 flag 标记
                flag = TRUE;
            }
        }
        int i = strlen(dest); // 获取更新后的目标字符串长度
        // 将 "../" 之后的部分追加到目标字符串
        for (int j = 3; relapath[j] != '\0'; j++) {
            dest[i++] = relapath[j];
        }
    }
    // 检查相对路径是否以 "./" 开头
    else if ( relapath[1] == '/') {
        // 如果是，直接将当前工作目录复制到目标字符串
        strcpy(dest, cwd);
        int j = strlen(dest); // 获取复制后目标字符串的长度

        // 将相对路径中 "./" 之后的部分追加到目标字符串
        for (int i = 1; relapath[i] != '\0'; i++) {
            if (j == 1) {
                // 如果当前工作目录是根目录（只有一个字符），不添加额外的 '/'
                dest[j + i - 2] = relapath[i];
            } else {
                // 否则，添加 '/' 和相对路径的其余部分
                dest[j + i - 1] = relapath[i];
            }
        }
    } 
    else {
        // 如果相对路径不是以 "./" 或 "../" 开头，不做任何处理
        return;
    }
}

//参数:
//char* path: 一个字符数组，用于存储当前工作目录的路径。
//功能:
//函数通过 memcpy 将 current->cwd（当前进程的工作目录）复制到提供的 path 数组中。MAX_PATH_LEN 定义了可以复制的最大字符数，以确保不会超过 path 数组的大小。
//返回值:
//返回 0 表示操作成功完成。
int do_rcwd(char* path) {
  memcpy(path, current->cwd, MAX_PATH_LEN);
  return 0;
}

//参数:
//char* path: 一个字符数组，包含要更改为的新工作目录的路径。
//功能:
//首先，函数使用 memset 初始化 path_resolved 数组，确保没有残留数据。
//如果提供的 path 是相对路径（以 '.' 开头），则调用 resolve_relative_path 函数来解析这个相对路径。解析后的路径存储在 path_resolved 中。
//resolve_relative_path 函数根据当前工作目录和提供的相对路径来计算新的工作目录。
//如果 path 是绝对路径（不以 '.' 开头），则直接将其作为新的工作目录。
//使用 memcpy 将解析或提供的路径复制到 current->cwd，从而更新当前工作目录。
int do_ccwd(char* path) {
  char path_resolved[MAX_PATH_LEN];
  memset(path_resolved, 0, MAX_PATH_LEN);
  if (path[0] == '.') {
    resolve_path(path_resolved, path);
    memcpy(current->cwd, path_resolved, MAX_PATH_LEN);
  }
  else {
    memcpy(current->cwd, path, MAX_PATH_LEN);
  }
  return 0;
}

void* memcpy1(void* dest, const void* src, size_t len) {
  const char* s = src;
  char* d = dest;

  if ((((uintptr_t)dest | (uintptr_t)src) & (sizeof(uintptr_t) - 1)) == 0) {
    while ((void*)d < (dest + len - (sizeof(uintptr_t) - 1))) {
      *(uintptr_t*)d = *(const uintptr_t*)s;
      d += sizeof(uintptr_t);
      s += sizeof(uintptr_t);
    }
  }

  while (d < (char*)(dest + len)) *d++ = *s++;

  return dest;
}

void* memset1(void* dest, int byte, size_t len) {
  if ((((uintptr_t)dest | len) & (sizeof(uintptr_t) - 1)) == 0) {
    uintptr_t word = byte & 0xFF;
    word |= word << 8;
    word |= word << 16;
    word |= word << 16 << 16;

    uintptr_t* d = dest;
    while (d < (uintptr_t*)(dest + len)) *d++ = word;
  } else {
    char* d = dest;
    while (d < (char*)(dest + len)) *d++ = byte;
  }
  return dest;
}

size_t strlen1(const char* s) {
  const char* p = s;
  while (*p) p++;
  return p - s;
}

int strcmp1(const char* s1, const char* s2) {
  unsigned char c1, c2;

  do {
    c1 = *s1++;
    c2 = *s2++;
  } while (c1 != 0 && c1 == c2);

  return c1 - c2;
}

char* strcpy1(char* dest, const char* src) {
  char* d = dest;
  while ((*d++ = *src++))
    ;
  return dest;
}

char *strchr1(const char *p, int ch)
{
	char c;
	c = ch;
	for (;; ++p) {
		if (*p == c)
			return ((char *)p);
		if (*p == '\0')
			return (NULL);
	}
}

char* strtok1(char* str, const char* delim) {
  static char* current;
  if (str != NULL) current = str;
  if (current == NULL) return NULL;

  char* start = current;
  while (*start != '\0' && strchr(delim, *start) != NULL) start++;

  if (*start == '\0') {
    current = NULL;
    return current;
  }

  char* end = start;
  while (*end != '\0' && strchr(delim, *end) == NULL) end++;

  if (*end != '\0') {
    *end = '\0';
    current = end + 1;
  } else
    current = NULL;
  return start;
}

char *strcat1(char *dst, const char *src) {
  strcpy(dst + strlen(dst), src);
  return dst;
}

long atol1(const char* str) {
  long res = 0;
  int sign = 0;

  while (*str == ' ') str++;

  if (*str == '-' || *str == '+') {
    sign = *str == '-';
    str++;
  }

  while (*str) {
    res *= 10;
    res += *str++ - '0';
  }

  return sign ? -res : res;
}

void* memmove1(void* dst, const void* src, size_t n) {
  const char* s;
  char* d;

  s = src;
  d = dst;
  if (s < d && s + n > d) {
    s += n;
    d += n;
    while (n-- > 0) *--d = *--s;
  } else
    while (n-- > 0) *d++ = *s++;

  return dst;
}

// Like strncpy but guaranteed to NUL-terminate.
char* safestrcpy1(char* s, const char* t, int n) {
  char* os;

  os = s;
  if (n <= 0) return os;
  while (--n > 0 && (*s++ = *t++) != 0)
    ;
  *s = 0;
  return os;
}
