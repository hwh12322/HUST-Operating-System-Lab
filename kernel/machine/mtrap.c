#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include <string.h>

char full_path[256];
char full_file[8192];
struct stat f_stat;

static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() { panic("Load access fault!"); }

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }

static void handle_illegal_instruction() { panic("Illegal instruction!"); }

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { panic("Misaligned AMO!"); }

// added @lab1_3
static void handle_timer() {
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}

//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap() {

  // 从 mepc 寄存器读取引发异常的指令地址
  uint64 mepc = read_csr(mepc);

  // 获取当前进程的引用
  process *current_process = current;

  // 检查当前进程是否有有效的调试信息
  if (current_process->debugline == NULL) {
      panic("No debug information available!");
  }

  // 遍历 line 数组以找到与 mepc 匹配的地址
  for (int i = 0; i < current_process->line_ind; i++) {
      if (current_process->line[i].addr == mepc) {
          // 找到了匹配的源代码位置
          int file_index = current_process->line[i].file;
          uint64 dir_index = current_process->file[file_index].dir;
          int line_number = current_process->line[i].line;

          // 构建完整的文件路径
          int dir_len = strlen(current_process->dir[dir_index]);
          strcpy(full_path, current_process->dir[dir_index]);
          full_path[dir_len] = '/';
          strcpy(full_path + dir_len + 1, current_process->file[file_index].file);

          // 打开文件并读取其状态
          spike_file_t * _file_ = spike_file_open(full_path, O_RDONLY, 0);
          struct stat f_stat;
          spike_file_stat(_file_, &f_stat);

          // 读取整个文件到 full_file 数组
          spike_file_read(_file_, full_file, f_stat.st_size);
          spike_file_close(_file_); // 关闭文件

          // 从文件中提取特定行
          int offset = 0, row = 1;
          while (offset < f_stat.st_size) {
              int temp = 0;
              // 查找每一行的结尾
              while (offset + temp < f_stat.st_size && full_file[offset + temp] != '\n') temp++;
              // 检查是否是我们要找的行
              if (row == line_number) {
                  full_file[offset + temp] = '\0'; // 在行尾添加字符串结束符
                  break;
              }
              offset += (temp + 1); //跳转到下一行
              row++;
          }

          // 输出异常信息，包括文件路径、行号和具体的源代码行
          sprint("Runtime error at %s:%d\n%s\n",
                full_path,
                line_number,
                full_file + offset);
      }
  }

  uint64 mcause = read_csr(mcause);
  switch (mcause) {
    case CAUSE_MTIMER:
      handle_timer();
      break;
    case CAUSE_FETCH_ACCESS:
      handle_instruction_access_fault();
      break;
    case CAUSE_LOAD_ACCESS:
      handle_load_access_fault();
    case CAUSE_STORE_ACCESS:
      handle_store_access_fault();
      break;
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
      handle_illegal_instruction();

      break;
    case CAUSE_MISALIGNED_LOAD:
      handle_misaligned_load();
      break;
    case CAUSE_MISALIGNED_STORE:
      handle_misaligned_store();
      break;

    default:
      sprint("machine trap(): unexpected mscause %p\n", mcause);
      sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
      panic( "unexpected exception happened in M-mode.\n" );
      break;
  }
}
