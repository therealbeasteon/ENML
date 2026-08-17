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

/* Where the boot proof maps a process's code region. One definition: the build
 * passes it in, so this file and the kernel cannot disagree about it.
 */
COOKIE_FIRST_PROGRAM_BASE = DEFINED(COOKIE_FIRST_PROGRAM_BASE)
    ? COOKIE_FIRST_PROGRAM_BASE : 0x10000000;

/* The decoy sits at the base; the real entry is a fixed distance above it, far
 * enough that the decoy's own code cannot reach it by falling through. 0x200 is
 * not tuned - it is simply larger than either function compiles to, and the
 * ASSERT below is what enforces that rather than the number being trusted.
 */
COOKIE_FIRST_ENTRY_OFFSET = 0x200;

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
