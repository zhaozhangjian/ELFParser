#include "ELFParser.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <iostream>
#include <string>

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

Section::~Section() {}

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

StringTable::~StringTable() {}

const char* StringTable::GetString(size_t idx) {
    return idx < size_ ? (data_ + idx) : nullptr;
}
// == end StringTable ==

// == begin SymbolTable ==
SymbolTable::SymbolTable(ELFParser* e)
    : Section(e), strtab_(nullptr) {}

SymbolTable::~SymbolTable() {}

bool SymbolTable::PullData() {
    strtab_ = ep_->GetStringTable();
    return true;
}
// == end SymbolTable ==

// == begin ELFParser ==
ELFParser::ELFParser(const char* fname)
    : fname_(fname), buffer_(nullptr), bundle_offset_(0), fd_(0), bfsize_(0),
      elf_(nullptr), shstrtab_(nullptr), strtab_(nullptr), symtab_(nullptr) {}

ELFParser::~ELFParser() {
    elf_end(elf_);
    MemoryUnmapFile();

    if (shstrtab_) delete shstrtab_;
    if (strtab_)   delete strtab_;
    if (symtab_)   delete symtab_;
    if (note_)     delete note_;
}

bool ELFParser::elfError(const char* msg0, const char* msg1) {
    std::cout << "Error: " << msg0 << " " << msg1 << std::endl;
    return false;
}

size_t ELFParser::find_number(const char* str, int beg, int end) {
    size_t ret = 0;
    beg += str[beg] & 0x80 ? 1 : 0;

    for (int i = 0; i < end - beg && i < sizeof(ret); ++i) {
        (reinterpret_cast<char*>(&ret))[i] = str[end - 1 - i];
    }

    return ret;
}

std::string ELFParser::find_string(const char* str, int beg, int end) {
    int b = -1, len = 0;

    while (beg < end) {
        if ((str[beg] & 0x80) == 0 && str[beg] != ' ') { // skip separator and space
            if (b == -1) b = beg;
            len++;
        }
        beg++;
    }

    return std::string(str + b, len);
}

bool ELFParser::MemoryMapFile() {
    if (fname_ == nullptr) return elfError("empty elf file name!");

    struct stat st;
    fd_ = open(fname_, O_RDONLY);
    if (fd_ < 0) return elfError("open elf file failed!");

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

uint64_t getElfOffsetFromFatBinary(const char* data) {
    std::string magic(data, sizeof(CLANG_OFFLOAD_BUNDLER_MAGIC_STR) - 1);
    if (magic.compare(CLANG_OFFLOAD_BUNDLER_MAGIC_STR)) {
        return 0;
    }

    const auto obheader = reinterpret_cast<const __ClangOffloadBundleHeader*>(data);
    const auto* desc = &obheader->desc[0];
    for (uint64_t i = 0; i < obheader->numBundles; ++i,
        desc = reinterpret_cast<const __ClangOffloadBundleDesc*>(
            reinterpret_cast<uintptr_t>(&desc->triple[0]) + desc->tripleSize)) {

        std::string triple(desc->triple, desc->tripleSize);
        if (0 == triple.compare(0, sizeof(HIP_AMDGCN_AMDHSA_TRIPLE) - 1, HIP_AMDGCN_AMDHSA_TRIPLE)
            || 0 == triple.compare(0, sizeof(HCC_AMDGCN_AMDHSA_TRIPLE) - 1, HCC_AMDGCN_AMDHSA_TRIPLE)) {
            return desc->offset;
        } else {
            continue;
        }
    }
    return 0;
}

bool ELFParser::PullStrtabSymtab() {
    if (!MemoryMapFile()) return elfError("map elf file failed!");

    bundle_offset_ = getElfOffsetFromFatBinary(buffer_);

    if ((elf_ = elf_memory(const_cast<char *>(buffer_ + bundle_offset_), bfsize_)) == nullptr) {
        return elfError("elf_memory failed!");
    }

    if (!gelf_getehdr(elf_, &ehdr_)) return elfError("gelf_gerehdr failed!");

    if(!AddShStringTable()) return elfError("add .shstrtab failed!");

    Elf_Scn* scn;
    Section* section;
    GElf_Shdr shdr;
    for (size_t i = 0; i < ehdr_.e_shnum; ++i) {
        scn = elf_getscn(elf_, i);
        if (gelf_getshdr(scn, &shdr) == nullptr) return elfError("gelf_getshdr failed!");

        section = nullptr;
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
        } else if (shdr.sh_type == SHT_NOTE) {
            section = new Section(this);
            if (!section->pull(i)) return elfError("section pull failed!");
            note_ = section;
        }
    }

    if (!strtab_) return elfError(".strtab not found!");
    if (!strtab_->PullData()) return elfError(".strtab PullData failed!");

    if (!symtab_) return elfError(".symtab not found!");
    if (!symtab_->PullData()) return elfError(".symtab PullData failed!");

    return true;
}

bool ELFParser::PullKernelMetadata() {
    if (kernels_.empty()) return true;
    if (!note_) return elfError(".note not found!");

    std::map<std::string, KernMeta>* KernMetaMap = new std::map<std::string, KernMeta>;
    std::string metadata = std::string(note_->data_, note_->size_);

    size_t beg = metadata.find("AMDGPU");
    if (beg == std::string::npos) return elfError("AMDGPU not found in kernel metadata!");

    static constexpr size_t    symbol_sz =  7; // ".symbol"
    static constexpr size_t    offset_sz =  7; // ".offset"
    static constexpr size_t      size_sz =  5; // ".size"
    static constexpr size_t valuekind_sz = 11; // ".value_kind"
    static constexpr size_t group_seg_sz = 25; // ".group_segment_fixed_size"
    static constexpr size_t arg_align_sz = 22; // ".kernarg_segment_align"
    static constexpr size_t  kern_arg_sz = 21; // ".kernarg_segment_size"
    static constexpr size_t      vgpr_sz = 11; // ".vgpr_count"

    size_t anchor = metadata.find("amdhsa.version");
    if (anchor == std::string::npos) return elfError("amdhsa.version not found!");

    do {
        KernMeta kernMeta;
        KernParam param;
        size_t end, finish;

        beg = metadata.find(".args", beg + 1);
        if (beg == std::string::npos || beg > anchor) break;

        finish = metadata.find(".wavefront_size", beg);

        do {
            // .offset | .size | .value_kind
            size_t b = metadata.find(".offset", beg);
            if (b == std::string::npos || b > finish) break;

            beg = b + offset_sz;
            end = metadata.find(".size", beg);
            param._offset = find_number(metadata.c_str(), beg, end - 1);

            beg = end + size_sz;
            end = metadata.find(".value_kind", beg);
            param._size = find_number(metadata.c_str(), beg, end -1);

            beg = end + valuekind_sz + 1;
            end = metadata.find(".", beg);
            std::string vkind = find_string(metadata.c_str(), beg, end);
            auto it = ArgValueKindV3.find(vkind);
            if (it == ArgValueKindV3.end()) return elfError("unsuppored value kind of", vkind.c_str());
            param._type = it->second;

            kernMeta._params.push_back(std::move(param));
        } while (true);

        beg = metadata.find(".group_segment_fixed_size", beg) + group_seg_sz;
        end = metadata.find(".kernarg_segment_align", beg);
        kernMeta._ldsz = find_number(metadata.c_str(), beg, end - 1);

        beg = end + arg_align_sz;
        end = metadata.find(".kernarg_segment_size", beg);
        kernMeta._kaal = find_number(metadata.c_str(), beg, end - 1);

        beg = end + kern_arg_sz;
        end = metadata.find(".language", beg);
        kernMeta._kasz = find_number(metadata.c_str(), beg, end - 1);

        beg = metadata.find(".symbol", beg) + symbol_sz;
        end = metadata.find(".kd", beg);
        std::string name = find_string(metadata.c_str(), beg, end);

        beg = metadata.find(".vgpr_count", beg) + vgpr_sz;
        end = metadata.find(".vgpr_spill_count", beg);
        kernMeta._vgpr = find_number(metadata.c_str(), beg, end -1);

        (*KernMetaMap)[name] = std::move(kernMeta);
    } while (true);

    if (KernMetaMap->size() != kernels_.size()) return elfError("mismatch of symbol count and metadata count before integrate!");

    for (auto it = KernMetaMap->begin(); it != KernMetaMap->end(); ++it) {
        kernels_[it->first]._meta = std::move(it->second);
    }

    if (KernMetaMap->size() != kernels_.size()) return elfError("mismatch of symbol count and metadata count after integrate!");

    delete KernMetaMap;
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
                info._bundle_offset = static_cast<unsigned int>(bundle_offset_);
                info._mach = sym->st_value;
                info._masz = sym->st_size;
            } else if (GELF_ST_TYPE(sym->st_info) == STT_OBJECT) {
                // 9: 0000000000000b80    64 OBJECT  GLOBAL PROTECTED    6 _Z9vectorAddPKiS0_Pii.kd
                std::string name = std::string(symtab->strtab_->GetString(sym->st_name));
                if (!suffix_find_cast(name, ".kd")) {
                    return elfError("data object name should end with .kd!");
                }
                KernInfo& info = kernels_[name];
                info._bundle_offset = static_cast<unsigned int>(bundle_offset_);
                info._desc = sym->st_value;
                info._desz = sym->st_size;
            }
        }
    }

    if (!PullKernelMetadata()) return elfError("parse metadata failed!");

    return true;
}

const std::map<std::string, KernInfo>& ELFParser::GetKernelMap() { return kernels_; }

StringTable* ELFParser::GetShStringTable() { return dynamic_cast<StringTable*>(shstrtab_); }
StringTable* ELFParser::GetStringTable() { return dynamic_cast<StringTable*>(strtab_); }
SymbolTable* ELFParser::GetSymbolTable() { return dynamic_cast<SymbolTable*>(symtab_); }
// == end ELFParser ==