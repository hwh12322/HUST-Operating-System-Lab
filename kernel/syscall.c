/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"
#include "elf.h"

#include "spike_interface/spike_utils.h"
extern elf_sym symbols[100];
extern char symnames[64][32];
extern int symcount;

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

int print_functionname(uint64 ret_addr) {
  for(int i=0;i<symcount;i++){
    if(ret_addr >= symbols[i].st_value && ret_addr < symbols[i].st_value+symbols[i].st_size){
    //在堆栈回溯（stack backtrace）的过程中，这样的范围检查通常用于确定当前的返回地址属于哪个函数。
    //当函数 A 调用函数 B 时，B 的返回地址（即 A 中调用点的地址）通常位于 A 函数代码的某个位置。
    //通过检查这个地址是否在 A 函数的地址范围内，可以确定 B 是被 A 调用的函数。
      sprint("%s\n",symnames[i]);
      if(strcmp(symnames[i],"main")==0) return 0;
    }
  }
  return 1;
}

int sys_user_print_backtrace( int depth ){
  uint64 fp = current->trapframe->regs.s0 + 8, ra = current->trapframe->regs.ra;
  fp = *(uint64 *)(fp - 16);
  ra = *(uint64 *)(fp - 8);//栈帧中print_backtrace函数的返回地址（在f8函数的地址范围内）
  for( int i = 0; i < depth; i++) {
    if ( print_functionname(ra) == 0) return 0;
    fp = *(uint64 *)(fp - 16);
    ra = *(uint64 *)(fp - 8);
  } 
  return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_print_backtrace:
      return sys_user_print_backtrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
