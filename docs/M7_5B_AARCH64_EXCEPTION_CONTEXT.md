# M7.5b — AArch64 exception/context contract

M7.5a proved that the real AArch64 machine backend compiles and can program the architectural Generic Timer. M7.5b fixes the register/exception contract that must exist before Cookie Kernel can accept a syscall, take an interrupt or switch threads on hardware.

## Security properties

- The EL1 vector table is treated as a fixed architectural object: 16 entries, 128 bytes each, 2048-byte table/alignment.
- A synchronous exception is not assumed to be a syscall. ESR_EL1 is decoded and only an AArch64 `SVC #0` is eligible for Cookie syscall dispatch.
- The frozen Cookie syscall number remains in the register ABI; the SVC immediate is reserved at zero so there is one syscall-number namespace rather than two partially overlapping selectors.
- The integer exception frame preserves X0-X30 plus SP_EL0, ELR_EL1, SPSR_EL1, ESR_EL1 and FAR_EL1 in one 16-byte-aligned machine-owned structure.
- FP/SIMD state is deliberately excluded until its enable/save/restore policy is explicit. Userspace must not be allowed to create cross-thread V-register leakage through an incomplete context switch.
- Portable kernel code does not inspect the AArch64 frame. Architecture state remains behind the machine seam.

## Next implementation order

1. add the EL1 vector table in AArch64 assembly and verify linker alignment;
2. save the complete integer frame before calling any C++ exception dispatcher;
3. reject every synchronous lower-EL exception that is not the exact Cookie SVC encoding;
4. connect validated SVC to the frozen kernel ABI dispatcher;
5. restore the possibly modified return frame and use `eret`;
6. use the same saved-frame contract for scheduler context switching;
7. only then enable user-mode entry and begin QEMU syscall smoke testing.

Interrupt/GIC delivery and MMU/page-table bring-up remain separate slices. This milestone does not claim a bootable kernel until those mechanisms and the standalone image path exist.
