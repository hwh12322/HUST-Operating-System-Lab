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
#include "pmm.h"
#include "vmm.h"
#include "sched.h"

#include "spike_interface/spike_utils.h"
extern process procs[NPROC];
process* pidprocess ;
int child = 0;
//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  // buf is now an address in user space of the given app's user stack,
  // so we have to transfer it into phisical address (kernel is running in direct mapping).
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // reclaim the current process, and reschedule. added @lab3_1
  free_process( current );
  schedule();
  return 0;
}

//
// maybe, the simplest implementation of malloc in the world ... added @lab2_2
//
uint64 sys_user_allocate_page() {
  void* pa = alloc_page();
  uint64 va;
  // if there are previously reclaimed pages, use them first (this does not change the
  // size of the heap)
  if (current->user_heap.free_pages_count > 0) {
    va =  current->user_heap.free_pages_address[--current->user_heap.free_pages_count];
    assert(va < current->user_heap.heap_top);
  } else {
    // otherwise, allocate a new page (this increases the size of the heap by one page)
    va = current->user_heap.heap_top;
    current->user_heap.heap_top += PGSIZE;

    current->mapped_info[HEAP_SEGMENT].npages++;
  }
  user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));

  return va;
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
uint64 sys_user_free_page(uint64 va) {
  user_vm_unmap((pagetable_t)current->pagetable, va, PGSIZE, 1);
  // add the reclaimed page to the free page list
  current->user_heap.free_pages_address[current->user_heap.free_pages_count++] = va;
  return 0;
}

//
// kerenl entry point of naive_fork
//
ssize_t sys_user_fork() {
  sprint("User call fork.\n");
  return do_fork( current );
}

//
// kerenl entry point of yield. added @lab3_2
//
ssize_t sys_user_yield() {
  // TODO (lab3_2): implment the syscall of yield.
  // hint: the functionality of yield is to give up the processor. therefore,
  // we should set the status of currently running process to READY, insert it in
  // the rear of ready queue, and finally, schedule a READY process to run.
  current->status = READY;
  insert_to_ready_queue( current ) ;
  schedule();
  return 0;
}




//通过修改PKE内核和系统调用，为用户程序提供wait函数的功能，wait函数接受一个参数pid：
//当pid为-1时，父进程等待任意一个子进程退出即返回子进程的pid；
//当pid大于0时，父进程等待进程号为pid的子进程退出即返回子进程的pid；
//如果pid不合法或pid大于0且pid对应的进程不是当前进程的子进程，返回-1。
ssize_t sys_user_wait(int pid) {
  if( pid == -1 ){
  // 遍历进程表，寻找任意一个僵尸子进程
  // 如果没有找到，则阻塞当前进程
    for (int i = 0; i < NPROC; i++) {
      if (procs[i].parent == current){
        child = 1;
        if (procs[i].status == ZOMBIE  ) {
          procs[i].status = FREE;
          return i; 
        }
      }
    }
    if ( child == 1 ) {
        child = 0;
        current->waiting_for_any_child = 1;  // 标记为在等待任意子进程
        current->status = BLOCKED;       // 设置当前进程状态为阻塞
        schedule();
        return -2;                      // 调用调度器选择另一个进程运行
    }
  }
  // 查找特定 PID 的进程
  // 如果是子进程且已结束，则处理并返回 PID
  // 如果不是子进程或尚未结束，则阻塞或返回错误
  else if ( pid > 0 ){
    for (int i = 0; i < NPROC; i++) {
        if (procs[i].status != FREE && procs[i].pid == pid) {
            pidprocess = &procs[i]; // 返回找到的进程的指针
        }
    }
    if ( pidprocess->parent->pid != current->pid ){
      return -1;
    }
    if (pidprocess->status == ZOMBIE){ 
      pidprocess->status = FREE;
      return pidprocess->pid;
    }
    else{
        current->waiting_for_pid = pid;  // 设置正在等待的子进程的 PID
        current->status = BLOCKED;       // 设置当前进程状态为阻塞
        schedule();                      // 调用调度器选择另一个进程运行
        return -2;
    }
  }
  else return -1;
  return -1;
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
    // added @lab2_2
    case SYS_user_allocate_page:
      return sys_user_allocate_page();
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    case SYS_user_fork:
      return sys_user_fork();
    case SYS_user_yield:
      return sys_user_yield();
    case SYS_user_wait:
      return sys_user_wait(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
