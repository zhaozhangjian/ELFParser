#include "types.h"

#include <string>
#include <vector>
#include <gelf.h>
#include <libelf.h>
#include <stdint.h>

class ELFParser;
class Section {
public:
    Section(ELFParser* e);
    virtual ~Section();
    bool pull(size_t idx);
    std::string Name() const;
    virtual bool PullData() { return true; };
protected:
    ELFParser*    ep_;
    GElf_Shdr    hdr_;
    const char* data_;
    size_t      size_;
    size_t    scnidx_;

    friend class ELFParser;
};

class StringTable : public Section {
public:
    StringTable(ELFParser* e);
    ~StringTable();
    const char* GetString(size_t idx);
};

class SymbolTable : public Section {
public:
    SymbolTable(ELFParser* e);
    ~SymbolTable();
    bool PullData() override;
private:
    StringTable* strtab_;

    friend class ELFParser;
};

class ELFParser {
public:
    ELFParser(const char* fname);
    ~ELFParser();
    bool PullStrtabSymtab();
    bool ExtactKernels();
    const std::map<std::string, KernInfo>&  GetKernelMap();

private:
    bool MemoryMapFile();
    bool MemoryUnmapFile();
    bool AddShStringTable();
    bool PullKernelMetadata();
    bool elfError(const char* msg0, const char* msg1 = nullptr);
    size_t find_number(const char* str, int beg, int end);
    std::string find_string(const char* str, int beg, int end);
    StringTable* GetShStringTable();
    StringTable* GetStringTable();
    SymbolTable* GetSymbolTable();

private:
    const char*         fname_;
    const char*        buffer_;
    uint64_t    bundle_offset_;
    int                    fd_;
    int                bfsize_;
    GElf_Ehdr            ehdr_;
    Elf*                  elf_;
    Section*         shstrtab_;
    Section*           strtab_;
    Section*           symtab_;
    Section*             note_;
    std::map<std::string, KernInfo>  kernels_;

    friend class Section;
    friend class StringTable;
    friend class SymbolTable;
};