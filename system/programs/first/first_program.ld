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
ASSERT(DEFINED(COOKIE_FIRST_ENTRY_OFFSET),
       "COOKIE_FIRST_ENTRY_OFFSET was not passed; the build must supply the one offset the kernel also seals")

SECTIONS
{
    . = COOKIE_FIRST_PROGRAM_BASE;

    /* KEEP, because --gc-sections would otherwise collect the decoy: it is not
     * reachable from ENTRY and nothing calls it, which is precisely what makes
     * it a decoy. `[[gnu::used]]` stops the *compiler* discarding it and says
     * nothing to the linker. The ASSERT at the foot of this file would catch
     * its absence, but catching it here means the build never gets that far. */
    .text.decoy : {
        __cookie_first_decoy_start = .;
        KEEP(*(.text.decoy))
        __cookie_first_decoy_end = .;
    }

    . = COOKIE_FIRST_PROGRAM_BASE + COOKIE_FIRST_ENTRY_OFFSET;

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

ASSERT(__cookie_first_decoy_end <= COOKIE_FIRST_PROGRAM_BASE + COOKIE_FIRST_ENTRY_OFFSET,
       "the decoy overruns the real entry; raise COOKIE_FIRST_ENTRY_OFFSET")

ASSERT(__cookie_first_program_end - COOKIE_FIRST_PROGRAM_BASE <= 0x1000,
       "the first program no longer fits in one page, which the boot proof's code region is")

ASSERT(DEFINED(cookie_first_decoy),
       "the decoy entry was discarded; a kernel entering at the base would then run whatever is there")
