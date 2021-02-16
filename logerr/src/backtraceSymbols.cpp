//----------------------------
//  INCLUDES
//----------------------------

#include "backtraceSymbols.h"
#include <logerr>

// C
#include <bfd.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <link.h>

// std
#include <iostream>
#include <sstream>

// NOTE: There are two different bfd interfaces, and you don't know which one you're going to have on your platform.
// So, try to figure it out and call the right things.
#ifdef bfd_section_flags
#define BFD_INTERFACE_A
#elif defined(bfd_set_section_flags)
#define BFD_INTERFACE_B
#endif

class FileMatch
{
public:
	explicit FileMatch(void* addr)
	    : mAddress(addr)
	{
	}

	void*       mAddress;
	const char* mFile = nullptr;
	void*       mBase = nullptr;
};

class FileLineDesc
{
public:
	FileLineDesc(asymbol** syms, bfd_vma pc)
	    : mPc(pc)
	    , mFound(false)
	    , mSyms(syms)
	{
	}

	void findAddressInSection(bfd* abfd, asection* section);

	bfd_vma      mPc;
	std::string  mFilename;
	std::string  mFunctionname;
	unsigned int mLine  = 0;
	bool         mFound = false;
	asymbol**    mSyms  = nullptr;
};

static int                                              findMatchingFile(struct dl_phdr_info* info, size_t size, void* data);
static asymbol**                                        kstSlurpSymtab(bfd* abfd, const char* fileName);
static std::vector<std::pair<std::string, std::string>> translateAddressesBuf(bfd* abfd, bfd_vma* addr, int numAddr, asymbol** syms);
static std::vector<std::pair<std::string, std::string>> processFile(const char* fileName, bfd_vma* addr, int naddr);
static void                                             findAddressInSection(bfd* abfd, asection* section, void* data);

//--------------------------------------------------------------------------------------------------
//	findMatchingFile (public ) [static ]
//--------------------------------------------------------------------------------------------------
int findMatchingFile(struct dl_phdr_info* info, size_t, void* data)
{
	FileMatch* match = (FileMatch*) data;

	for (uint32_t i = 0; i < info->dlpi_phnum; i++)
	{
		const ElfW(Phdr)& phdr = info->dlpi_phdr[i];

		if (phdr.p_type == PT_LOAD)
		{
			ElfW(Addr) vaddr = phdr.p_vaddr + info->dlpi_addr;
			ElfW(Addr) maddr = ElfW(Addr)(match->mAddress);
			if ((maddr >= vaddr) && (maddr < vaddr + phdr.p_memsz))
			{
				match->mFile = info->dlpi_name;
				match->mBase = (void*) info->dlpi_addr;
				return 1;
			}
		}
	}
	return 0;
}

//--------------------------------------------------------------------------------------------------
//	kstSlurpSymtab (public ) [static ]
//--------------------------------------------------------------------------------------------------
asymbol** kstSlurpSymtab(bfd* abfd, const char* fileName)
{
	if (!(bfd_get_file_flags(abfd) & HAS_SYMS))
	{
		LOGERR << "Error bfd file " << fileName << " flagged as having no symbols." << std::endl;
		return nullptr;
	}

	asymbol**    syms;
	unsigned int size;

	long symcount = bfd_read_minisymbols(abfd, false, (void**) &syms, &size);
	if (symcount == 0)
		symcount = bfd_read_minisymbols(abfd, true, (void**) &syms, &size);

	if (symcount < 0)
	{
		LOGERR << "Error bfd file " << fileName << " found no symbols." << std::endl;
		return nullptr;
	}

	return syms;
}

//--------------------------------------------------------------------------------------------------
//	translateAddressesBuf (public ) [static ]
//--------------------------------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> translateAddressesBuf(bfd* abfd, bfd_vma* addr, int numAddr, asymbol** syms)
{
	std::vector<std::pair<std::string, std::string>> addressBuffer;

	for (int32_t i = 0; i < numAddr; i++)
	{
		FileLineDesc desc(syms, addr[i]);
		bfd_map_over_sections(abfd, findAddressInSection, (void*) &desc);

		if (!desc.mFound)
		{
			std::stringstream ss;
			ss << "[0x" << std::hex << addr[i] << "]";
			addressBuffer.emplace_back("??:0", ss.str());
		}
		else
		{

			std::string& functionName = desc.mFunctionname;
			std::string& filename     = desc.mFilename;

			if (functionName.empty())
				functionName = "??";
			if (!desc.mFilename.empty())
			{
				std::stringstream ss;
				ss << desc.mFilename;
				while (getline(ss, filename, '/')) {};
				filename.append(":").append(std::to_string(desc.mLine));
			}

			addressBuffer.emplace_back(filename, functionName);
		}
	}

	return addressBuffer;
}

//--------------------------------------------------------------------------------------------------
//	processFile (public ) [static ]
//--------------------------------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> processFile(const char* fileName, bfd_vma* addr, int naddr)
{
	std::vector<std::pair<std::string, std::string>> ret_buf;

	bfd* abfd = bfd_openr(fileName, NULL);
	if (!abfd)
	{
		LOGERR << "Error opening bfd file  " << fileName << std::endl;
		return ret_buf;
	}

	if (bfd_check_format(abfd, bfd_archive))
	{
		LOGERR << "Cannot get addresses from archive  " << fileName << std::endl;
		bfd_close(abfd);
		return ret_buf;
	}

	char** matching;
	if (!bfd_check_format_matches(abfd, bfd_object, &matching))
	{
		LOGERR << "Format does not match for archive  " << fileName << std::endl;
		bfd_close(abfd);
		return ret_buf;
	}

	asymbol** syms = kstSlurpSymtab(abfd, fileName);
	if (!syms)
	{
		LOGERR << "Failed to read symbol table for archive  " << fileName << std::endl;
		bfd_close(abfd);
		return ret_buf;
	}

	ret_buf = translateAddressesBuf(abfd, addr, naddr, syms);

	free(syms);

	bfd_close(abfd);
	return ret_buf;
}

//--------------------------------------------------------------------------------------------------
//	findAddressInSection (public ) [static ]
//--------------------------------------------------------------------------------------------------
void findAddressInSection(bfd* abfd, asection* section, void* data)
{
	auto* desc = (FileLineDesc*) data;
	return desc->findAddressInSection(abfd, section);
}

//--------------------------------------------------------------------------------------------------
//	backtraceSymbols (public ) []
//--------------------------------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> backtraceSymbols(void* const* addrList, int numAddr)
{
	std::vector<std::pair<std::string, std::string>> symbols;

	// initialize the bfd library
	bfd_init();

	uint32_t idx = numAddr;
	for (int32_t i = 0; i < numAddr; i++)
	{
		// find which executable, or library the symbol is from
		FileMatch match(addrList[--idx]);
		dl_iterate_phdr(findMatchingFile, &match);

		// adjust the address in the global space of your binary to an
		// offset in the relevant library
		auto addr = (bfd_vma)(addrList[idx]);
		addr -= (bfd_vma)(match.mBase);

		// lookup the symbol
		if (match.mFile && strlen(match.mFile))
		{
			auto&& val = processFile(match.mFile, &addr, 1);
			if (!val.empty())
				symbols.insert(symbols.begin(), std::make_move_iterator(val.begin()), std::make_move_iterator(val.end()));
		}
		else
		{
			auto&& val = processFile("/proc/self/exe", &addr, 1);
			if (!val.empty())
				symbols.insert(symbols.begin(), std::make_move_iterator(val.begin()), std::make_move_iterator(val.end()));
		}
	}

	return symbols;
}

//--------------------------------------------------------------------------------------------------
//	findAddressInSection (public ) []
//--------------------------------------------------------------------------------------------------
void FileLineDesc::findAddressInSection(bfd* abfd, asection* section)
{
	if (mFound)
		return;

#ifdef USE_OLD_BFD
	if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0)
#else
	if ((bfd_section_flags(section) & SEC_ALLOC) == 0)
#endif
		return;

#ifdef USE_OLD_BFD
	bfd_vma vma = bfd_section_vma(abfd, section);
#else
	bfd_vma vma = bfd_section_vma(section);
#endif
	if (mPc < vma)
		return;

#ifdef USE_OLD_BFD
	bfd_size_type size = bfd_section_size(abfd, section);
#else
	bfd_size_type size = bfd_section_size(section);
#endif
	if (mPc >= (vma + size))
		return;

	char* pFilename     = nullptr;
	char* pFunctionname = nullptr;

	mFound = bfd_find_nearest_line(abfd, section, mSyms, (mPc - vma), (const char**) &pFilename, (const char**) &pFunctionname, &mLine);

	if (pFilename) mFilename.assign(pFilename, strlen(pFilename));
	if (pFunctionname) mFunctionname.assign(pFunctionname, strlen(pFunctionname));
}
