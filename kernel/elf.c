/*
 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
 */
#include "elf.h"
#include "string.h"
#include "riscv.h"
#include "spike_interface/spike_utils.h"
#define SHT_SYMTAB	2
#define SHT_STRTAB	3
elf_sym symbols[100];
char symnames[64][32];
int symcount;

typedef struct elf_info_t {
  spike_file_t *f;
  process *p;
  
} elf_info;

//
// the implementation of allocater. allocates memory space for later segment loading
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  // directly returns the virtual address as we are in the Bare mode in lab1_x
  return (void *)elf_va;
}

//
// actual file reading, using the spike file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_info *msg = (elf_info *)ctx->info;
  // call spike file utility to load the content of elf file into memory.
  // spike_file_pread will read the elf file (msg->f) from offset to memory (indicated by
  // *dest) for nb bytes.
  return spike_file_pread(msg->f, dest, nb, offset);
}

//
// init elf_ctx, a data structure that loads the elf.
//
elf_status elf_init(elf_ctx *ctx, void *info) {
  ctx->info = info;

  // load the elf header
  if (elf_fpread(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr)) return EL_EIO;

  // check the signature (magic value) of the elf
  if (ctx->ehdr.magic != ELF_MAGIC) return EL_NOTELF;

  return EL_OK;
}

//
// load the elf segments to memory regions as we are in Bare mode in lab1
//
elf_status elf_load(elf_ctx *ctx) {
  // elf_prog_header structure is defined in kernel/elf.h
  elf_prog_header ph_addr;
  int i, off;

  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers
    if (elf_fpread(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr)) return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;

    // allocate memory block before elf loading
    void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);

    // actual loading
    if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
      return EL_EIO;
  }

  return EL_OK;
}

typedef union {
  uint64 buf[MAX_CMDLINE_ARGS];
  char *argv[MAX_CMDLINE_ARGS];
} arg_buf;

//
// returns the number (should be 1) of string(s) after PKE kernel in command line.
// and store the string(s) in arg_bug_msg.
//
static size_t parse_args(arg_buf *arg_bug_msg) {
  // HTIFSYS_getmainvars frontend call reads command arguments to (input) *arg_bug_msg
  long r = frontend_syscall(HTIFSYS_getmainvars, (uint64)arg_bug_msg,
      sizeof(*arg_bug_msg), 0, 0, 0, 0, 0);
  kassert(r == 0);

  size_t pk_argc = arg_bug_msg->buf[0];
  uint64 *pk_argv = &arg_bug_msg->buf[1];

  int arg = 1;  // skip the PKE OS kernel string, leave behind only the application name
  for (size_t i = 0; arg + i < pk_argc; i++)
    arg_bug_msg->argv[i] = (char *)(uintptr_t)pk_argv[arg + i];

  //returns the number of strings after PKE kernel in command line
  return pk_argc - arg;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(process *p) {
  arg_buf arg_bug_msg;

  // retrieve command line arguements
  size_t argc = parse_args(&arg_bug_msg);
  if (!argc) panic("You need to specify the application program!\n");

  sprint("Application: %s\n", arg_bug_msg.argv[0]);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;

  info.f = spike_file_open(arg_bug_msg.argv[0], O_RDONLY, 0);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  loading_functionname(&elfloader);
  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close the host spike file
  spike_file_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

//从 ELF 文件中提取所有函数的符号和名称
void loading_functionname(elf_ctx *ctx){
// 查找 shstrtab 节:
  elf_sect_header shstr;
  uint64 shstr_offset = ctx->ehdr.shoff + ctx->ehdr.shstrndx * ctx->ehdr.shentsize;//计算节头字符串表（shstrtab）的偏移量
  elf_fpread(ctx, (void *)&shstr, sizeof(shstr), shstr_offset);
  //sprint("%d\n", shstr_sh.sh_size);   208
  char shstrtab_head[shstr.sh_size];
  elf_fpread(ctx, &shstrtab_head, shstr.sh_size, shstr.sh_offset);//从 ELF 文件中读取 shstrtab 的节头信息
  //sprint("%d %d\n", shstr_offset, shstr_sect_off);

//查找 strtab 和 symtab 节
  uint16 section_number = ctx->ehdr.shnum;//从 elf_ctx 中获取节的数量
  elf_sect_header sym;
  elf_sect_header str;
  elf_sect_header temp_section_header;
  for(int i=0; i<section_number; i++) { //遍历所有节头，读取每个节头信息到 temp_section_header
    uint64 each_section_offset = ctx->ehdr.shoff + ctx->ehdr.shentsize * i;
    elf_fpread(ctx, (void*)&temp_section_header, sizeof(temp_section_header), each_section_offset );
    uint64 t = temp_section_header.sh_type;
    switch (t)//检查每个节的类型。如果是符号表（SHT_SYMTAB），保存到 sym。如果是字符串表，保存到 str
    {
    case SHT_SYMTAB:
      sym = temp_section_header;
      break;
    case SHT_STRTAB:
      if(strcmp(shstrtab_head + temp_section_header.sh_name,".strtab") == 0){//字符串表可能有多个
          str =  temp_section_header;
        }
      break;
    default:
      break;
    }
  }
  //加载符号和它们的名字
  uint64 str_sect_off = str.sh_offset;//字符串表的偏移量
  uint64 sym_num = sym.sh_size/sizeof(elf_sym);
  int count = 0;
  for(int i = 0; i<sym_num; i++) {//遍历符号表，对每个符号，检查是否是函数（通过 st_info 判断）。如果是，读取其名称并存储。
    elf_sym symbol;
    elf_fpread(ctx, (void*)&symbol, sizeof(symbol), sym.sh_offset + sizeof(elf_sym) * i);
    if(symbol.st_name == 0) continue;
    if(symbol.st_info == (1 << 4 | 2) ){    //STT_FUNC,检查符号是否是一个全局函数
      char symname[32];
      uint64 each_name_offset = str_sect_off + symbol.st_name;
      elf_fpread(ctx, (void*)&symname, sizeof(symname), each_name_offset );
      strcpy(symnames[count] , symname);
      symbols[count++] = symbol;
    }
  }
  symcount = count;
} 
