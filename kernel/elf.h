#ifndef _ELF_H_
#define _ELF_H_

#include "util/types.h"
#include "process.h"

#define MAX_CMDLINE_ARGS 64

// Section header
typedef struct elf_sect_header_table {
  uint32 sh_name;		/* 该成员指定节的名称。 */
  uint32 sh_type;		/* 该成员对该部分的内容和语义进行分类 */
  uint64 sh_flags;		/* Miscellaneous section attributes */
  uint64 sh_addr;		/* Section virtual addr at execution */
  uint64 sh_offset;		/* 该成员的值给出了从文件开头到该节中第一个字节的字节偏移量 */
  uint64 sh_size;		/* 该成员给出了该节的大小（以字节为单位） */
  uint32 sh_link;		/* 该成员保存一个节头表索引链接，其解释取决于节类型 */
  uint32 sh_info;		/* Additional section information */
  uint64 sh_addralign;	/* Section alignment */
  uint64 sh_entsize;	/* 某些节保存固定大小条目的表，例如符号表。对于这样的部分，该成员给出每个条目的大小（以字节为单位）。如果该节不包含固定大小条目的表，则该成员包含 0。 */
} elf_sect_header;


typedef struct symtab_table {
  uint32 st_name;		/* 符号名称在字符串表中的偏移（4字节） */
  unsigned char	st_info;	/* 符号的类型和绑定属性（1字节）*/
  unsigned char	st_other;	/* 其它信息（1字节） */
  uint16 st_shndx;		/* 符号相关联的节索引（2字节） */
  uint64 st_value;		/* 符号的值，通常是地址（8字节） */
  uint64 st_size;		/* 符号的大小（8字节） */
} elf_sym;


// elf header structure
typedef struct elf_header_t {
  uint32 magic;
  uint8 elf[12];
  uint16 type;      /* Object file type */
  uint16 machine;   /* Architecture */
  uint32 version;   /* Object file version */
  uint64 entry;     /* Entry point virtual address */
  uint64 phoff;     /* Program header table file offset */
  uint64 shoff;     /* Section header table file offset */
  uint32 flags;     /* Processor-specific flags */
  uint16 ehsize;    /* ELF header size in bytes */
  uint16 phentsize; /* Program header table entry size */
  uint16 phnum;     /* Program header table entry count */
  uint16 shentsize; /* Section header table entry size */
  uint16 shnum;     /* Section header table entry count */
  uint16 shstrndx;  /* Section header string table index */
} elf_header;

// Program segment header.
typedef struct elf_prog_header_t {
  uint32 type;   /* Segment type */
  uint32 flags;  /* Segment flags */
  uint64 off;    /* Segment file offset */
  uint64 vaddr;  /* Segment virtual address */
  uint64 paddr;  /* Segment physical address */
  uint64 filesz; /* Segment size in file */
  uint64 memsz;  /* Segment size in memory */
  uint64 align;  /* Segment alignment */
} elf_prog_header;

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian
#define ELF_PROG_LOAD 1

typedef enum elf_status_t {
  EL_OK = 0,

  EL_EIO,
  EL_ENOMEM,
  EL_NOTELF,
  EL_ERR,

} elf_status;

typedef struct elf_ctx_t {
  void *info;
  elf_header ehdr;
} elf_ctx;

void loading_functionname(elf_ctx *ctx);
elf_status elf_init(elf_ctx *ctx, void *info);
elf_status elf_load(elf_ctx *ctx);

void load_bincode_from_host_elf(process *p);

#endif
