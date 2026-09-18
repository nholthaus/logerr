// A shared module the symbolizer tests load AFTER taking their first trace, which is the only way to exercise the
// module-list refresh: a library already mapped when the list was read proves nothing.
//
// It is built here rather than borrowed from the system because whether a system library can be symbolized at all
// depends on how the distribution stripped it, and on whether the symbol chosen is an IFUNC whose implementation is
// local and therefore absent from the dynamic symbol table. The test must depend on neither. This exports one ordinary
// function, so its name is in the dynamic symbol table on every platform, with or without debug info.

extern "C" void logerrTestModuleFunction()
{
}
