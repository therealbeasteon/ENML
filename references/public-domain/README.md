# Redistributable references

The ENML reference library is 181 documents and roughly 574 MB. Almost none of it
can live here: it is largely commercial books - Stroustrup, Stallings, Tanenbaum,
*Symbian OS Internals*, ARM's architecture manuals - and publishing those in a
public repository would be redistributing other people's work without a licence.

These four can be, and they are the ones the security work actually leaned on.

| File | Source | Why it is here |
| --- | --- | --- |
| `NIST.FIPS.197-upd1.pdf` | NIST FIPS 197 (AES) | US Government work, public domain. The AEAD primitive behind the Key Service. |
| `nist.fips.180-4.pdf` | NIST FIPS 180-4 (SHA-2) | US Government work, public domain. Boot measurement, package digests, Merkle logs, and the single primitive the attestation design needs. |
| `nist.sp.800-124r1.pdf` | NIST SP 800-124r1 | US Government work, public domain. Mobile device security guidance. |
| `clark.pdf` | Clark and Hengartner, *Panic Passwords: Authenticating under Duress* | Published by its authors under a Creative Commons licence that expressly permits copying and distribution. The source for M4.10. |

Everything else is listed in `docs/REFERENCE_INDEX.md` with enough detail to be
obtained from its publisher.
