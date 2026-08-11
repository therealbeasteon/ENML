# Reference index

The ENML reference library is 181 documents, roughly 574 MB, held outside this
repository. It cannot be published here: most of it is commercial books and
vendor specifications, and redistributing those would be publishing other
people's work without a licence. The four items that *are* redistributable live
in `references/public-domain/`.

This index exists so the library is usable as a working tool without being
copied. `PROJECT_VISION.md` sets the rule these sources are read under:
**references teach principles, ENML determines implementation.**

## Sources that changed a decision

These are the ones whose contribution is traceable to a specific milestone. The
rest of the library is context; these left fingerprints.

| Source | What it established | Where it landed |
| --- | --- | --- |
| Tanenbaum, *Modern Operating Systems* 9.6.3 | Capability systems generally have all-or-nothing revocation and uncontrolled proliferation | M7.1c - derivation-tree revocation, and a transfer right |
| QNX (`qnx-paper92.pdf`) | Four kernel services behind fourteen calls in 605 lines; interrupt handlers inside user processes; scheduling at message boundaries rather than only at timer intervals | M7.0, M7.1a/b/d/e |
| *Microdrivers* (ASPLOS 08) | A user-level driver that takes a lock, disables interrupts and dies leaves the kernel unable to begin recovery | M7.1d - no user code in interrupt context |
| OSTEP | Receive livelock; gaming a scheduler by relinquishing just before a slice expires | M7.1d masking; M7.1e charge-never-refund |
| Clark and Hengartner, *Panic Passwords* | 2P defeated by iteration; forced randomisation; **the locking event must be invariant to credential type**; Damerau-Levenshtein for typo separation | M4.10, M4.10c, M4.10e |
| iOS Security Guide | Secure erasure is "especially challenging on flash storage, where wear-leveling might mean multiple copies of data need to be erased"; Effaceable Storage; escalating delays with optional wipe at ten | M4.10a - erasure destroys keys, not data |
| *One-time Traceable Ring Signatures* (2021-1054) | Anonymity with no group manager; de-anonymisation on double-signing; **only hash evaluations**, post-quantum, under a second for a ring of 2^10 | M7.4c - attestation policy on one mandatory primitive |
| *Ring Signatures with User-Controlled Linkability* (2022-1743) | Linkability controlled by the signer; no group manager; fully decentralised | M7.4c - linkage is the user's choice per verifier |
| Bender, Katz, Morselli, *Ring Signatures: Stronger Definitions* | The natural definitions are too weak; anonymity under full key exposure and unforgeability under insider corruption are required | M7.4c - recorded as a requirement on any scheme later chosen |
| *Merkle Tree Ladder Mode* (2022-1730) | PQC signatures of 666 to 49,856 bytes condensed to 248-472 | M5.8 - why a log is affordable over a metered radio |
| *Merkle Trees: Collision Probability* (2402.04367) | Longer paths raise root-collision probability; longer hashes lower it | M5.8 - bounded proof depth, and a computed path length |
| ARM ISA A64, AArch64 white paper | Instruction and system architecture | M7.3 machine contract; M7.3c still unwritten |
| Symbian OS Internals; Architecture Sourcebook | Minimal kernel responsibilities; capabilities as static sign-time tokens; checks cheap enough for constrained hardware | M7.1c contrast; M7.0 decomposition |
| NIST FIPS 197; FIPS 180-4 | AES; SHA-2 | Key Service AEAD; boot measurement, digests, Merkle logs |

## Full library

Grouped by subject. Filenames are exactly as supplied, so an entry can be matched
to the file that the reading notes cite.

### Architecture and instruction sets

- `AArch64 White paper - June 2021.pdf`
- `ISA_A64_xml_v88A-2021-12_OPT.pdf`
- `Introduction to Assembly Language Programming_ From Soup to Nuts_.pdf`
- `JAVA PROGRAMMING Notes.pdf`
- `O'Reilly - Programming C# 2nd Edition.pdf`
- `The_C++_Programming_Language_4th_Edition_Bjarne_Stroustrup.pdf`
- `optimizing_cpp.pdf`
- `sprac30a.pdf`

### Boot and firmware

- `01-11-2024-1730459061-6-IJGET-9.AdvancedBootloaderDesignforEmbeddedSystems_secureandefficientfirmwareupdates.pdf`
- `2025-330-paper.pdf`
- `A_Secure_Boot_Loader_System_for_Loading_an_Operati.pdf`
- `Android Boot, DRTM, UKIs.pdf`
- `Android Generic Boot Loader.pdf`
- `Bootloadable_v1_50 (1).pdf`
- `Bootloadable_v1_50.pdf`
- `CE Boot Framework.pdf`
- `CSI_UEFI_SECURE_BOOT.pdf`
- `SECURE BOOTLOADER GUIDE.PDF`
- `U-Boot-Bootloader-for-IoT-Platform-Alexey-Brodkin-Synopsys-3.pdf`
- `android-boot-slides-2.0.pdf`
- `ourdev_437970.pdf`
- `perrot-secure-boot.pdf`
- `secure-boot.pdf`
- `secure_boot_on_imx6.pdf`
- `ug489-gecko-bootloader-user-guide-gsdk-4.pdf`

### Cryptography and protocols

- `140sp1053.pdf`
- `140sp3725.pdf`
- `2021-1054.pdf`
- `2022-1730.pdf`
- `2022-1743.pdf`
- `2023-03-24-KeyStorage.pdf`
- `2023-905.pdf`
- `2308.02785v2.pdf`
- `2402.04367v1.pdf`
- `CacheAttackAES.pdf`
- `NIST.FIPS.197-upd1.pdf`
- `Pass-the-Passkey_A4_v2.pdf`
- `Users-Guide-to-Passkeys-HYPR-Info-Brief.pdf`
- `encryption.pdf`
- `merkle-tree-ladder-mode-pqc2022.pdf`
- `nist.fips.180-4.pdf`
- `openssl-cookbook.pdf`
- `pkcs-1v2-12.pdf`
- `ring-signatures-stronger-definitions-and-constructions-3cvmzrfobz.pdf`
- `ring.pdf`
- `sha256-384-512.pdf`
- `sha256.pdf`
- `x86softwarereverseengineeringcrackingandcountermeasures.pdf`

### Device security and roots of trust

- `2022-545.pdf`
- `Case_Study_UID_NJ.pdf`
- `K102909811S219.pdf`
- `NIST-Tehranipoor.pdf`
- `NIST.SP.1800-35.pdf`
- `SDCOEMobileDeviceEncryptionProcedure.pdf`
- `feb1_mobility-roots-of-trust_regenscheid.pdf`
- `mdse-nist-sp1800-22-draft-2.pdf`
- `nbsir76-1041.pdf`
- `nist.sp.800-124r1.pdf`
- `nistir5917.pdf`

### Duress and coercion

- `an-213_duress_codes_in_protege_gx.pdf`
- `clark.pdf`

### Hardening, sandboxing, exploitation

- `06-sandboxing.pdf`
- `2024___LockOS___Poster_Cominlabs.pdf`
- `A Guide to Kernel Exploitation Attacking the Core (2011).pdf`
- `CYBER-SECURITY-USING-SANDBOX.pdf`
- `General_OS_Hardening-1.pdf`
- `LPC 2016 - OP-TEE.pdf`
- `OS_Hardening.pdf`
- `System-Hardening-Explained.pdf`
- `eb-power-of-sandboxing.pdf`
- `optee-readthedocs-io-en-latest.pdf`
- `us-14-Gorenc-Thinking-Outside-The-Sandbox-Violating-Trust-Boundaries-In-Uncommon-Ways-WP.pdf`

### Kernel and OS internals

- `Development of a driver in linux_android.pdf`
- `EUROSYS16.pdf`
- `IKM_CIDR07.pdf`
- `Introduction-to-Linux-Kernel-Driver-Programming-Michael-Opdenacker-Bootlin-.pdf`
- `Modern.Operating.Systems.2nd.Ed_.by_.Tanenbaum-not-scanned-1-1.pdf`
- `Operating Systems_ Principles and Practice, Vol. 1_ Kernels and Processes ( PDFDrive ).pdf`
- `UNIX_Users_Manual_Release_3_Jun80.pdf`
- `William Stallings - Operating Systems (1).pdf`
- `book-riscv-rev3.pdf`
- `ch2.pdf`
- `enrico.pdf`
- `kocialkowski-current-overview-drm-kms-driver-side-apis.pdf`
- `l11.pdf`
- `lec14-notes.pdf`
- `lf_linux_kernel_development_2010.pdf`
- `linux-kernel-slides.pdf`
- `microdrivers-asplos08.pdf`
- `operating_systems_three_easy_pieces.pdf`
- `oracle writing device drivers.pdf`
- `qnx-paper92.pdf`
- `short07.pdf`
- `tcsc24-cpu.pdf`
- `the_design_of_the_unix_operating_system.pdf`

### Mobile OS architecture

- `BB_Architecture.pdf`
- `GrapheneOS-Quick-Start-Guide-generic-GrapheneGoat-v24.07.pdf`
- `Knox-security-white-paper-r1.pdf`
- `L-0000567779-pdf.pdf`
- `LiAPSys14.pdf`
- `Mobile-Lock_MDM-Solution-Brief.pdf`
- `Samsung - OneUI 6 - EN.pdf`
- `apple-platform-security-guide.pdf`
- `htcb007.pdf`
- `iOS_Security_Guide.pdf`
- `oneui_design_guide_eng.pdf`
- `thesymbianosarchitecturesourcebook.pdf`
- `tizen-architecture-linuxcollab.pdf`
- `tizen_for_platform_developers_and_manufacturers.pdf`

### Other supplied material

- `0470018453.01.pdf`
- `12-344-154355665249-52.pdf`
- `12-610-157770877159-63.pdf`
- `2.-Compter-Science-Syeda-Tooba-Kazmi.pdf`
- `2008-142.pdf`
- `20121107-elce.pdf`
- `2107.08695v1.pdf`
- `2205.12270v1.pdf`
- `225.pdf`
- `2512.21663v1.pdf`
- `2604.14228v2.pdf`
- `373b72ec7b04dab558a5415cbb106901.pdf`
- `49c52b6cdc25db32b5db0bc9c18e8e7c913f.pdf`
- `90001129.pdf`
- `978-3-642-39218-4_16_Chapter.pdf`
- `9781118852781.excerpt.pdf`
- `9789348107794_Sample.pdf`
- `9ae82cb18283b325c7681d5d0e795de66b9c.pdf`
- `A2-2.pdf`
- `CourseOutline4900Ev10.pdf`
- `FULLTEXT01.pdf`
- `FULLTEXT02.pdf`
- `IJETCSIT-V5I2P103 (1).pdf`
- `IJETCSIT-V5I2P103.pdf`
- `IJIRT170139_PAPER.pdf`
- `IJNRD2403290.pdf`
- `IJPTT-1.PDF`
- `IJRAR22B2235.pdf`
- `SANS-Webcast-Presentation.pdf`
- `Session_4.3_II_Bhat.pdf`
- `The Implications of Creating an iPhone Backdoor.pdf`
- `Thesis.pdf`
- `Thesis_Terrence-Jo.pdf`
- `Unconfirmed 151105.crdownload`
- `Unconfirmed 470966.crdownload`
- `book.pdf`
- `ddo_article_stevejobs.pdf`
- `laia_2018_J._Phys.__Conf._Ser._1007_012016.pdf`
- `lcna_co2012_marinas.pdf`
- `luo-vel-hu.pdf`
- `nspw2020-spero.pdf`
- `paper1.pdf`
- `paper6.pdf`
- `sec25_slides_liang-junkai.pdf`
- `tamin-msthesis.pdf`

### Telephony, radio, RCS

- `2023-06-02-Mobile-Network-Security.pdf`
- `Datasheet-RCS-en.pdf`
- `Everything_You_Wanted_to_Know_About_RCS.pdf`
- `rcs-business-messaging-1-pager.pdf`

### UI and UX

- `5.-Mobile-app-UIUX-Design.pdf`
- `Android-UI-Design.pdf`
- `DesignOfMobileApp.pdf`
- `Human_Interface_Guidelines_v1.7.0.pdf`
- `InnovationsinUiUxDesignofMobileApplicationsTrendsPracticesandChallenges.pdf`
- `Microsoft-Design-Language-1603.pdf`
- `Mobile_App_UX_Principles_3.pdf`
- `Operating Systems - Internals and Design Principles - 7th E.pdf`
- `TextureDraping_EGSR_2009.pdf`
- `UI Design and Interaction Guide for Windows Phone 7 v2.0.pdf`
- `UI.Design.Guide.pdf`
- `UI_Guidelines_BlackBerry_Smartphones_7_1 (1).pdf`
- `UI_Guidelines_BlackBerry_Smartphones_7_1.pdf`
- `e-Book3DModellingAnimation_Final.pdf`
- `graphicdesigntheory_helenarmstrong.pdf`
- `graphicdesignthinking.pdf`
- `the-basics-of-ux-design.pdf`
- `ui design.pdf`
- `ui.pdf`
- `uidesign.key.pdf`
- `uiux_design_with_figma_en.pdf`
- `uxpin_mobile_ui_design_patterns_2014.pdf`
- `wiley_the_essential_guide_to_user_interf.pdf`

## Obtaining these

Books and vendor specifications are available from their publishers. Papers cited
above by number are IACR ePrint (`eprint.iacr.org/<year>/<id>`) or arXiv
(`arxiv.org/abs/<id>`) identifiers. NIST publications are free from
`csrc.nist.gov`.

## Rule of use

From `docs/REFERENCE_ADDITIONS_2026_08_10.md`: design claims may not be taken
from sources outside this library unless the project owner authorises them. One
such authorisation has been given, and is recorded in
`docs/M4_10_COERCION_RESISTANT_UNLOCK.md` under M4.10d.
