#include "ELFParser.h"
#include <iostream>

bool ELF_parser(const char* fname, std::map<std::string, KernInfo>& kernels) {
    ELFParser* ep = new ELFParser(fname);
    if(!ep->PullStrtabSymtab()) {
        std::cout << "Error: ELFParser::PullStrtabSymtab() failed!" << std::endl;
        return false;
    }
    if (!ep->ExtactKernels()) {
        std::cout << "Error: ELFParser::ExtactKernels() failed!" << std::endl;
        return false;
    }
    kernels = ep->GetKernelMap();

    delete ep;
    return true;
}

int main() {
    const char fname[] = "./samples/_code_object0000.o";
    std::map<std::string, KernInfo> kernels;
    if (!ELF_parser(fname, kernels)) {
        std::cout << "Error: parse ELF failed!" << std::endl;
    } else {
        for (auto it = kernels.begin(); it != kernels.end(); ++it) {
            std::cout << "Name:" << it->first
                    << "\nDesc:" << it->second._desc
                    << "\nDeSz:" << it->second._desz
                    << "\nMach:" << it->second._mach
                    << "\nMaSz:" << it->second._masz
                    << "\nKaSz:" << it->second._meta._kasz
                    << "\nVgpr:" << it->second._meta._vgpr;

            size_t i = 0;
            for (auto it1 = it->second._meta._params.begin(); it1 != it->second._meta._params.end(); ++it1) {
                std::cout << "\nArg" << i << " offset: " << it1->_offset
                                        << " size: " << it1->_size
                                        << " type: " << it1->_type;
                ++i;
            }
            std::cout << std::endl;
        }
        
    }

    return 0;
}
