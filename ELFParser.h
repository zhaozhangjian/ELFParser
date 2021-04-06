#include <string>
#include <vector>
#include <map>
#include <gelf.h>
#include <libelf.h>
#include <stdint.h>

struct KernInfo {
    unsigned int _desc;
    unsigned int _desz;
    unsigned int _mach;
    unsigned int _masz;
};

typedef std::map<std::string, KernInfo> KernelMap;

class ELFParser;

class Section {
public:
    Section(ELFParser* e);
    bool pull(size_t idx);
    std::string Name() const;
    virtual bool PullData() { return true; };
protected:
    ELFParser*    ep_;
    GElf_Shdr    hdr_;
    const char* data_;
    size_t      size_;
    size_t    scnidx_;
};

class StringTable : public Section {
public:
    StringTable(ELFParser* e);
    const char* GetString(size_t idx);
};

class SymbolTable : public Section {
public:
    SymbolTable(ELFParser* e);
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
    bool AddShStringTable();
    bool ExtactKernels();
    const KernelMap&  GetKernelMap();

private:
    bool MemoryMapFile();
    bool MemoryUnmapFile();
    StringTable* GetShStringTable();
    StringTable* GetStringTable();
    SymbolTable* GetSymbolTable();
    bool elfError(const char* msg);

private:
    const char*  fname_;
    const char* buffer_;
    int             fd_;
    int         bfsize_;
    GElf_Ehdr     ehdr_;
    Elf*           elf_;
    Section*  shstrtab_;
    Section*    strtab_;
    Section*    symtab_;
    KernelMap  kernels_;

    friend class Section;
    friend class StringTable;
    friend class SymbolTable;
};