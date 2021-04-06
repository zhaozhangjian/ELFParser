#include "ELFParser.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <iostream>

static bool suffix_find_cast(std::string &str, const std::string &suf) {
    bool ret = false;
    if (str.size() >= suf.size() &&
        str.compare(str.size() - suf.size(), suf.size(), suf) == 0) {
        str = str.substr(0, str.size() - suf.size());
        ret = true;
    }
    return ret;
}

// == begin Section ==
Section::Section(ELFParser* e)
    : ep_(e), data_(nullptr), size_(0), scnidx_(0) {}

bool Section::pull(size_t idx) {
    scnidx_ = idx;

    Elf_Scn* scn = elf_getscn(ep_->elf_, idx);
    if (scn == nullptr) { return false; }

    if (!gelf_getshdr(scn, &hdr_)) { return false; }

    Elf_Data* edata = elf_getdata(scn, nullptr);
    if (edata) {
        data_ = static_cast<const char*>(const_cast<const void*>(edata->d_buf));
        size_ = edata->d_size;
    }
    return true;
}

std::string Section::Name() const {
    return std::string(ep_->GetShStringTable()->GetString(hdr_.sh_name));
}
// == end Section ==

// == begin StringTable ==
StringTable::StringTable(ELFParser* e)
    : Section(e) {}

const char* StringTable::GetString(size_t idx) {
    return idx < size_ ? (data_ + idx) : nullptr;
}
// == end StringTable ==

// == begin SymbolTable ==
SymbolTable::SymbolTable(ELFParser* e)
    : Section(e), strtab_(nullptr) {}

bool SymbolTable::PullData() {
    strtab_ = ep_->GetStringTable();
    return true;
}
// == end SymbolTable ==

// == begin ELFParser ==
ELFParser::ELFParser(const char* fname)
    : fname_(fname), buffer_(nullptr), fd_(0), bfsize_(0),
      elf_(nullptr), shstrtab_(nullptr), strtab_(nullptr), symtab_(nullptr) {}

ELFParser::~ELFParser() {
    MemoryUnmapFile();

    elf_end(elf_);

    if (shstrtab_) delete shstrtab_;
    if (strtab_)   delete strtab_;
    if (symtab_)   delete symtab_;
}

bool ELFParser::elfError(const char* msg) {
    std::cout << "Error: " << msg << std::endl;
    return false;
}

bool ELFParser::MemoryMapFile() {
    if (fname_ == nullptr) {
        return false;
    }

    struct stat st;
    fd_ = open(fname_, O_RDONLY);
    if (fd_ < 0) {
        return elfError("open elf file failed!");
    }

    if (fstat(fd_, &st) != 0) {
        close(fd_);
        return elfError("stat elf file failed!");
    }

    bfsize_ = st.st_size;

    void* start;
    if (bfsize_ == 0 || (start = mmap(NULL, bfsize_, PROT_READ, MAP_SHARED, fd_, 0)) == MAP_FAILED) {
        return false;
    } else {
        buffer_ = static_cast<const char*>(start);
    }

    return true;
}

bool ELFParser::MemoryUnmapFile() {
    if (munmap(const_cast<void*>(reinterpret_cast<const void*>(buffer_)), bfsize_) == -1) {
        return elfError("mnumap ELFParser::buffer_ failed!");
    }
    if(close(fd_) == -1) {
        return elfError("close ELFParser::fd_ failed!");
    }

    return true;
}

bool ELFParser::AddShStringTable() {
    GElf_Shdr shdr;
    Elf_Scn* scn = elf_getscn(elf_, ehdr_.e_shstrndx);
    if (gelf_getshdr(scn, &shdr) == nullptr) return elfError("gelf_getshdr failed!");
    Section* section = new StringTable(this);
    if (!section->pull(ehdr_.e_shstrndx)) return elfError("section pull failed!");
    if (!section->PullData()) return elfError(".shstrtab PullData failed!");

    shstrtab_ = section;
    return true;
}

bool ELFParser::PullStrtabSymtab() {
    if (!MemoryMapFile()) {
        return false;
    }

    if ((elf_ = elf_memory(const_cast<char*>(buffer_), bfsize_)) == nullptr) {
        return elfError("elf_memory failed!");
    }

    if (!gelf_getehdr(elf_, &ehdr_)) return elfError("gelf_gerehdr failed!");

    if(!AddShStringTable()) return elfError("add .shstrtab failed!");

    Elf_Scn* scn;
    GElf_Shdr shdr;
    for (size_t i = 0; i < ehdr_.e_shnum; ++i) {
        scn = elf_getscn(elf_, i);
        if (gelf_getshdr(scn, &shdr) == nullptr) return elfError("gelf_getshdr failed!");

        if (shdr.sh_type != SHT_STRTAB && shdr.sh_type != SHT_SYMTAB) continue;

        Section* section = nullptr;
        if (shdr.sh_type == SHT_STRTAB) {
            section = new StringTable(this);
            if (!section->pull(i)) return elfError("section pull failed!");

            if (section->Name() == std::string(".strtab")) { // we only want .strtab section
                strtab_ = section;
            } else {
                delete section;
                continue;
            }
        } else if (shdr.sh_type == SHT_SYMTAB) {
            section = new SymbolTable(this);
            if (!section->pull(i)) return elfError("section pull failed!");
            symtab_ = section;
        }
    }

    if (!strtab_) return elfError(".strtab not found!");
    if (!strtab_->PullData()) return elfError(".strtab PullData failed!");

    if (!symtab_) return elfError(".symtab not found!");
    if (!symtab_->PullData()) return elfError(".symtab PullData failed!");

    return true;
}

bool ELFParser::ExtactKernels() {
    SymbolTable* symtab = GetSymbolTable();

    if (!symtab) return elfError("ELFParser::symtab_ is not ready!");

    for (size_t i = 0; i < symtab->size_ / sizeof(GElf_Sym); ++i) {
        const GElf_Sym* sym = reinterpret_cast<const GElf_Sym*>(symtab->data_ + i * sizeof(GElf_Sym));
        if (GELF_ST_BIND(sym->st_info) == STB_GLOBAL) {
            if (GELF_ST_TYPE(sym->st_info) == STT_FUNC) {
                // 8: 0000000000001000   596 FUNC    GLOBAL PROTECTED    7 _Z9vectorAddPKiS0_Pii
                std::string name = std::string(symtab->strtab_->GetString(sym->st_name));
                KernInfo& info = kernels_[name];
                info._mach = sym->st_value;
                info._masz = sym->st_size;
            } else if (GELF_ST_TYPE(sym->st_info) == STT_OBJECT) {
                // 9: 0000000000000b80    64 OBJECT  GLOBAL PROTECTED    6 _Z9vectorAddPKiS0_Pii.kd
                std::string name = std::string(symtab->strtab_->GetString(sym->st_name));
                if (!suffix_find_cast(name, ".kd")) {
                    return elfError("data object name should end with .kd!");
                }
                KernInfo& info = kernels_[name];
                info._desc = sym->st_value;
                info._desz = sym->st_size;
            }
        }
    }

    return true;
}

const KernelMap& ELFParser::GetKernelMap() { return kernels_; }

StringTable* ELFParser::GetShStringTable() { return dynamic_cast<StringTable*>(shstrtab_); }
StringTable* ELFParser::GetStringTable() { return dynamic_cast<StringTable*>(strtab_); }
SymbolTable* ELFParser::GetSymbolTable() { return dynamic_cast<SymbolTable*>(symtab_); }
// == end ELFParser ==