/* Link map for Cookie's first compiled program.
 *
 * The two entry points land at addresses the kernel has to know, so they are
 * placed here rather than left to section ordering. The offsets are exported as
 * symbols and the build reads them back out of the ELF, which is what keeps the
 * kernel's sealed entry and this layout from being two separate statements of one
 * address - the defect PR #145 found in the ARM64 Image header.
 *
 * Deliberately no .data, .bss or .got output sections that accept input. A
 * program with writable static data would need the loader to place initialised
 * bytes and zero the rest, which is a second thing the .ckx plan would have to
 * describe; the first program has no need of either, and the ASSERTs below make
 * that a checked property rather than a hope. A GOT entry in particular would be
 * an absolute pointer in static data, which is exactly what this image must not
 * contain if it is to be mapped wherever the plan says.
 */

ENTRY(cookie_first_entry)

/* Where the boot proof maps this program's code region, and how far into it the
 * real entry sits. Both are supplied by the build with --defsym, from the two
 * variables the top-level CMakeLists.txt declares, and the kernel's boot image
 * is compiled against the same two.
 *
 * Deliberately no `DEFINED(X) ? X : <default>` fallback, which is what this file
 * used to carry. A fallback is a second definition: it makes a build that failed
 * to pass the address link successfully at the wrong one, and the disagreement
 * would then be discovered by a machine that entered a page containing nothing.
 * Without it, the same mistake is an undefined-symbol error at link time.
 */
ASSERT(DEFINED(COOKIE_FIRST_PROGRAM_BASE),
       "COOKIE_FIRST_PROGRAM_BASE was not passed; the build must supply the one address the kernel also maps")

SECTIONS
{
    . = COOKIE_FIRST_PROGRAM_BASE;

    /* The entry is the base of the region, and there is nothing before it.
     *
     * A decoy used to sit here - four words that carried the wrong witness -
     * so that a kernel entering the mapping at its base rather than at the
     * sealed entry was caught. docs/M7_16_ENTRY_FROM_REGION.md deletes the
     * situation it guarded against: the entry *is* the base now, derived by the
     * kernel from this region rather than named by anyone, so there is no other
     * address to enter at and nothing for a decoy to catch. */
    .text : {
        __cookie_first_entry_start = .;
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
        __cookie_first_entry_end = .;
    }

    __cookie_first_program_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
        *(.ARM.attributes)
    }
}

ASSERT(__cookie_first_entry_start == COOKIE_FIRST_PROGRAM_BASE,
       "the entry is not the base of the region, which is where the kernel will derive it from")

ASSERT(__cookie_first_program_end - COOKIE_FIRST_PROGRAM_BASE <= 0x1000,
       "the first program no longer fits in one page, which the boot proof's code region is")

