#pragma once

#include <cstddef>
#include <cstdint>

#define ELFMAG "\177ELF"
#define SELFMAG 4

enum : unsigned char {
  EI_CLASS = 4,
  EI_DATA = 5,
  ELFCLASS64 = 2,
  ELFDATA2LSB = 1,
};

enum : std::uint16_t {
  EM_386 = 3,
  EM_ARM = 40,
  EM_X86_64 = 62,
  EM_AARCH64 = 183,
};

enum : std::uint32_t {
  SHT_PROGBITS = 1,
  SHT_SYMTAB = 2,
  SHT_DYNSYM = 11,
  SHT_INIT_ARRAY = 14,
};

enum : unsigned char {
  STB_GLOBAL = 1,
};

constexpr unsigned char ELF64_ST_BIND(unsigned char info) noexcept {
  return static_cast<unsigned char>(info >> 4);
}

using Elf64_Addr = std::uint64_t;
using Elf64_Off = std::uint64_t;
using Elf64_Half = std::uint16_t;
using Elf64_Word = std::uint32_t;
using Elf64_Xword = std::uint64_t;

struct Elf64_Ehdr {
  unsigned char e_ident[16];
  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;
  Elf64_Addr e_entry;
  Elf64_Off e_phoff;
  Elf64_Off e_shoff;
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;
  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;
};

struct Elf64_Phdr {
  Elf64_Word p_type;
  Elf64_Word p_flags;
  Elf64_Off p_offset;
  Elf64_Addr p_vaddr;
  Elf64_Addr p_paddr;
  Elf64_Xword p_filesz;
  Elf64_Xword p_memsz;
  Elf64_Xword p_align;
};

struct Elf64_Shdr {
  Elf64_Word sh_name;
  Elf64_Word sh_type;
  Elf64_Xword sh_flags;
  Elf64_Addr sh_addr;
  Elf64_Off sh_offset;
  Elf64_Xword sh_size;
  Elf64_Word sh_link;
  Elf64_Word sh_info;
  Elf64_Xword sh_addralign;
  Elf64_Xword sh_entsize;
};

struct Elf64_Sym {
  Elf64_Word st_name;
  unsigned char st_info;
  unsigned char st_other;
  Elf64_Half st_shndx;
  Elf64_Addr st_value;
  Elf64_Xword st_size;
};

static_assert(sizeof(Elf64_Ehdr) == 64, "Elf64_Ehdr layout mismatch");
static_assert(offsetof(Elf64_Ehdr, e_phoff) == 32, "Elf64_Ehdr::e_phoff offset mismatch");
static_assert(offsetof(Elf64_Ehdr, e_shoff) == 40, "Elf64_Ehdr::e_shoff offset mismatch");
static_assert(sizeof(Elf64_Phdr) == 56, "Elf64_Phdr layout mismatch");
static_assert(offsetof(Elf64_Phdr, p_offset) == 8, "Elf64_Phdr::p_offset offset mismatch");
static_assert(sizeof(Elf64_Shdr) == 64, "Elf64_Shdr layout mismatch");
static_assert(offsetof(Elf64_Shdr, sh_offset) == 24, "Elf64_Shdr::sh_offset offset mismatch");
static_assert(sizeof(Elf64_Sym) == 24, "Elf64_Sym layout mismatch");
static_assert(offsetof(Elf64_Sym, st_value) == 8, "Elf64_Sym::st_value offset mismatch");
