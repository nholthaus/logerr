//----------------------------
//  INCLUDES
//----------------------------

#include "backtraceSymbols.h"

// std
#include <iostream>

//--------------------------------------------------------------------------------------------------
//	findMatchingFile (public ) [static ]
//--------------------------------------------------------------------------------------------------
int findMatchingFile(struct dl_phdr_info* info, size_t size, void* data)
{
	FileMatch* match = (FileMatch*)data;

	for (uint32_t i = 0; i < info->dlpi_phnum; i++)
	{
		const ElfW(Phdr)& phdr = info->dlpi_phdr[i];

		if (phdr.p_type == PT_LOAD)
		{
			ElfW(Addr) vaddr = phdr.p_vaddr + info->dlpi_addr;
			ElfW(Addr) maddr = ElfW(Addr)(match->mAddress);
			if ((maddr >= vaddr) &&
				(maddr < vaddr + phdr.p_memsz))
			{
				match->mFile = info->dlpi_name;
				match->mBase = (void*)info->dlpi_addr;
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
		printf("Error bfd file \"%s\" flagged as having no symbols.\n", fileName);
		return NULL;
	}

	asymbol** syms;
	unsigned int size;

	long symcount = bfd_read_minisymbols(abfd, false, (void**)&syms, &size);
	if (symcount == 0)
		symcount = bfd_read_minisymbols(abfd, true, (void**)&syms, &size);

	if (symcount < 0)
	{
		printf("Error bfd file \"%s\", found no symbols.\n", fileName);
		return NULL;
	}

	return syms;
}

//--------------------------------------------------------------------------------------------------
//	translateAddressesBuf (public ) [static ]
//--------------------------------------------------------------------------------------------------
char** translateAddressesBuf(bfd* abfd, bfd_vma* addr, int numAddr, asymbol** syms)
{
	char** ret_buf = NULL;
	int32_t    total = 0;

	char   b;
	char*  buf = &b;
	int32_t    len = 0;

	for (uint32_t state = 0; state < 2; state++)
	{
		if (state == 1)
		{
			ret_buf = (char**)malloc(total + (sizeof(char*) * numAddr));
			buf = (char*)(ret_buf + numAddr);
			len = total;
		}

		for (int32_t i = 0; i < numAddr; i++)
		{
			FileLineDesc desc(syms, addr[i]);

			if (state == 1)
				ret_buf[i] = buf;

			bfd_map_over_sections(abfd, findAddressInSection, (void*)&desc);

			if (!desc.mFound)
			{
				total += snprintf(buf, len, "[0x%llx] \?\? \?\?:0", (long long unsigned int) addr[i]) + 1;

			}
			else {

				const char* name = desc.mFunctionname;
				if (name == NULL || *name == '\0')
					name = "??";
				if (desc.mFilename != NULL)
				{
					char* h = strrchr(desc.mFilename, '/');
					if (h != NULL)
						desc.mFilename = h + 1;
				}
				total += snprintf(buf, len, "%s:%u %s", desc.mFilename ? desc.mFilename : "??", desc.mLine, name) + 1;
				// elog << "\"" << buf << "\"\n";
			}
		}

		if (state == 1)
		{
			buf = buf + total + 1;
		}
	}

	return ret_buf;
}

//--------------------------------------------------------------------------------------------------
//	processFile (public ) [static ]
//--------------------------------------------------------------------------------------------------
char** processFile(const char* fileName, bfd_vma* addr, int naddr)
{
	bfd* abfd = bfd_openr(fileName, NULL);
	if (!abfd)
	{
		printf("Error opening bfd file \"%s\"\n", fileName);
		return NULL;
	}

	if (bfd_check_format(abfd, bfd_archive))
	{
		printf("Cannot get addresses from archive \"%s\"\n", fileName);
		bfd_close(abfd);
		return NULL;
	}

	char** matching;
	if (!bfd_check_format_matches(abfd, bfd_object, &matching))
	{
		printf("Format does not match for archive \"%s\"\n", fileName);
		bfd_close(abfd);
		return NULL;
	}

	asymbol** syms = kstSlurpSymtab(abfd, fileName);
	if (!syms)
	{
		printf("Failed to read symbol table for archive \"%s\"\n", fileName);
		bfd_close(abfd);
		return NULL;
	}

	char** retBuf = translateAddressesBuf(abfd, addr, naddr, syms);

	free(syms);

	bfd_close(abfd);
	return retBuf;
}

//--------------------------------------------------------------------------------------------------
//	findAddressInSection (public ) [static ]
//--------------------------------------------------------------------------------------------------
void findAddressInSection(bfd* abfd, asection* section, void* data)
{
	FileLineDesc* desc = (FileLineDesc*)data;
	return desc->findAddressInSection(abfd, section);
}

//--------------------------------------------------------------------------------------------------
//	backtraceSymbols (public ) []
//--------------------------------------------------------------------------------------------------
char** backtraceSymbols(void* const* addrList, int numAddr)
{
	char*** locations = (char***)alloca(sizeof(char**) * numAddr);

	// initialize the bfd library
	bfd_init();

	int total = 0;
	uint32_t idx = numAddr;
	for (int32_t i = 0; i < numAddr; i++)
	{
		// find which executable, or library the symbol is from
		FileMatch match(addrList[--idx]);
		dl_iterate_phdr(findMatchingFile, &match);

		// adjust the address in the global space of your binary to an
		// offset in the relevant library
		bfd_vma addr = (bfd_vma)(addrList[idx]);
		addr -= (bfd_vma)(match.mBase);

		// lookup the symbol
		if (match.mFile && strlen(match.mFile))
			locations[idx] = processFile(match.mFile, &addr, 1);
		else
			locations[idx] = processFile("/proc/self/exe", &addr, 1);

		total += strlen(locations[idx][0]) + 1;
	}

	// return all the file and line information for each address
	char** final = (char**)malloc(total + (numAddr * sizeof(char*)));
	char* f_strings = (char*)(final + numAddr);

	for (int32_t i = 0; i < numAddr; i++)
	{
		strcpy(f_strings, locations[i][0]);
		free(locations[i]);
		final[i] = f_strings;
		f_strings += strlen(f_strings) + 1;
	}

	return final;
}

//--------------------------------------------------------------------------------------------------
//	findAddressInSection (public ) []
//--------------------------------------------------------------------------------------------------
void FileLineDesc::findAddressInSection(bfd* abfd, asection* section)
{
//	if (mFound)
//		return;
//
//	if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0)
//		return;
//
//	bfd_vma vma = bfd_get_section_vma(abfd, section);
//	if (mPc < vma)
//		return;
//
//	bfd_size_type size = bfd_section_size(abfd, section);
//	if (mPc >= (vma + size))
//		return;
//
//	mFound = bfd_find_nearest_line(abfd, section, mSyms, (mPc - vma),
//		(const char**)&mFilename, (const char**)&mFunctionname, &mLine);
}

