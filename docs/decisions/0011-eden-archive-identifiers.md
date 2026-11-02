# 0011 — Eden Archive Format: name, extension, magic bytes and MIME type

Date: 2026-11-01 · Status: **accepted** (toddhow decided the extension `.edenpak` on 2026-11-01, as relayed in the task 0051 brief; the magic bytes and the short name are chosen by task 0051 as proposals inside that decision) · Task: `docs/tasks/0051-eden-archive-format-spec-v0-1.md` · Specification: `docs/spec/eden-archive-format-v0.1.md`

Open point for toddhow first: the short name **EdenPak** is also an unrelated US trademark in the field of paper and plastic tableware (EDENPAK LLC, from a search snippet; the trademark page itself could not be opened) and the name of a dormant GitHub account. This does not touch the extension, the magic or the full name; it matters only if "EdenPak" is used as a product name. Keep it as the short name, or use only "Eden Archive Format" and "`.edenpak`"?

## Decision

| Item | Value |
| --- | --- |
| Format name | **Eden Archive Format** |
| Public name | **Eden Archive** (toddhow, 2026-11-01: "Eden Archive will be the public text"). This settles the open point about the unrelated EDENPAK trademark: the name "EdenPak" is not used in documentation, launcher text or messages. The extension `.edenpak` and the magic `EDENPAK` + `01` are unchanged |
| File extension | **`.edenpak`** (decided by toddhow). `.eaf` is rejected: it is the extension of ELAN Annotation Format files |
| Magic (first 8 bytes) | `45 44 45 4E 50 41 4B 01`: ASCII `EDENPAK`, then the container-generation byte `01` (a proposal of task 0051). A probe matches the seven ASCII bytes only, so a later generation is reported as unsupported |
| Footer magic | `45 44 45 4E 45 4E 44 01`: ASCII `EDENEND`, then `01` |
| MIME type | `application/vnd.openeden.edenpak`, **a proposal only**; nothing is registered with IANA and registration is not requested |

The full layout, the registries and everything else are in the specification; this record fixes only the identifiers and the evidence behind them.

## Collision check (summary; sources, URLs and exact results are in Annex A of the specification)

Run on 2026-11-01. Nothing was found that clashes with `.edenpak` or the magic, and none of this is a clearance: it is "no hit found in X, Y, Z on this date".

- **Extension `.edenpak`: no hit found** in a general web search (two queries), file.org (a generated placeholder page exists that lists no program: neither a hit nor a clearance), fileinfo.com (404), file-extensions.org (404), the Library of Congress search (0 results) and Digital Formats browse list (nothing containing "eden"), and the GitHub repository and issue search API (0 results; one repository in a 2018 account named `EdenPak`, a test repository). **Not checked:** the UK National Archives PRONOM registry (the site refused the request, HTTP 403) and GitHub code search (needs a signed-in session).
- **Magic: no hit found** in the `file`/libmagic database (`magic/Magdir`, commit `584caf71`, 2026-10-30; two comment lines contain "eden" as part of a surname and a German word, neither a signature), shared-mime-info (`freedesktop.org.xml.in`, newest commit `93706a8b`, 2026-10-21), Wikipedia's List of file signatures and Gary Kessler's file signatures table (the 26 April 2025 page). Not checked: other signature collections (TrID, PRONOM signatures, vendor lists).
- **Name:** no file format called "Eden Archive Format" or "Eden Archive" was found. The word **Eden is shared**: an iOS game, Eden World Builder, saves `.eden` files; an open-source Nintendo Switch emulator is named Eden; HP calculators have an "Eden" documentation set; EPLAN uses `.edz`. None uses `.edenpak` or the magic, so the extension and magic stay unambiguous as file identifiers; the format is always written in full as "Eden Archive Format" and the product is OpenEden. A trademark listing for EDENPAK exists in an unrelated field (above). No trademark advice is available to the project; the naming policy of the archive design doc, section 15 (governance), is to be written when the specification is published.
- **MIME:** the IANA `application` media types list (downloaded 2026-11-01) has no type whose name contains "eden" and no `application/vnd.openeden*`.

## Consequences

- `docs/design/eden-archive-format.md` records the extension and magic (section 15) and points here.
- Task 0050's notes say the launcher's data-files page must learn "the pack extension and magic when 0051 fixes them": they are the ones above.
- Task 0052 (the reference library and the archive factory) matches on the seven bytes `EDENPAK` and uses `.edenpak` as a hint only.
- If a real clash is found later, this record is superseded by a numbered record, not edited.
