# WebKit `WebCore` Defensive Patch Audit for PS5 FW 14.22 (`26.07-14.22.00`)

**Lead Systems Security Researcher:** Adrián García Casado (`@adri22235`)  
**Research Entity:** García Casado Security Research (García Casado Cyber Lab)  
**Standard Compliance:** ISO/IEC 29147 (Coordinated Vulnerability Disclosure) & ISO/IEC 30111  
**Baseline Tree:** WebKit `605.1.15` / internal `616`-era (`webkitgtk-2.42.0` source reference, LLInt-only, `ENABLE_JIT=0`, `USE_ISO_MALLOC=0`)

---

## 1. Methodology & Cherry-Pick Delta Filtering

Because PlayStation 5 builds JavaScriptCore **without JIT** (`ENABLE_JIT=0`, `ENABLE_DFG_JIT=0`, `ENABLE_FTL_JIT=0`, `ENABLE_B3_JIT=0`, `ENABLE_WEBASSEMBLY=0`) and **without WebGPU**, upstream browser CVEs in JIT tiers, Wasm compilers, `<datalist>`, or WebGPU are non-applicable (`config-silent`).  

This audit cross-checks upstream **November 2026 `WebCore` memory-safety remediations** (landed *after* PS5 FW `26.07-14.22.00` shipped on November 2, 2026) directly against the pinned `webkitgtk-2.42.0` (`616`-era) implementation to separate genuine architectural exposure from post-fork features.

---

## 2. Source-Verified `WebCore` Remediation Matrix

| Upstream Fix (Date) | Subsystem & Symbol | `webkitgtk-2.42.0` (`616`-era) Status | Required API Surface on PS5 `14.22` | Defensive Remediation Pattern |
| :--- | :--- | :--- | :--- | :--- |
| **`425cd4e565`** *(Nov 13, 2026)* | `Source/WebCore/css/CSSSegmentedFontFace.cpp` — `CSSSegmentedFontFace::fontRanges()` | **MATCHES PATTERN:** Inserts empty `FontRanges` into `HashMap m_cache` and holds `auto& ranges = addResult.iterator->value` across the `m_fontFaces` loop. | `FontFace`, `document.fonts`, `<canvas>` 2D context *(note: `OffscreenCanvas` is absent on PS5, standard `<canvas>` is present)* | Lookup via `m_cache.find(key)`, populate a stack-local `FontRanges`, and insert into `m_cache` only after all font-load callbacks finish so table rehashing cannot invalidate `ranges`. |
| **`ff40757890`** *(Nov 6, 2026, Bug `323098`)* | `Source/WebCore/bindings/js/InternalReadableStream.cpp` — `InternalReadableStream::tee()` | **MATCHES PATTERN:** Uses `Detail::SequenceConverter<IDLObject>::convert(...)` (JS iteration protocol) with release-stripped `ASSERT(results.size() == 2)`. | `ReadableStream` *(confirmed present on PS5; note `TransformStream` `CVE-2026-43705` was already patched on-device)* | Replace `IDLSequence` conversion with direct indexed array slot reads (`getInternalBuiltinResultObjects<2>`) and explicit `isInternalReadableStream()` slot checks. |
| **`f4d7b99959`** *(Nov 13, 2026, Bug `321934`)* | `Source/WebCore/xml/parser/XMLDocumentParserLibxml2.cpp` — `XMLDocumentParser::startElementNs()` | **MATCHES PATTERN:** Calls `performMicrotaskCheckpoint()` on custom-element construction *before* `handleNamespaceAttributes()` and `handleElementAttributes()` copy libxml2 `xmlChar*` spans. | XML parser (`DOMParser` / XHTML), Custom Elements registry | Materialize all libxml2 namespace and attribute spans into WebKit-owned `Vector<Attribute>` storage *prior* to draining microtasks. |
| **`5f6a898283`** *(Nov 13, 2026)* | `Source/WebCore/css/CSSStyleSheet.cpp` — `CSSStyleSheet::replaceSync()` | **ABSENT IN `2.42.0`:** Uses `RuleMutationScope` directly without the newer `StyleSheetContentsCache` detached fast path. | `CSSStyleSheet.replaceSync` *(present on PS5, but fast-path presence unconfirmed)* | Bypassed unless Sony independently backported `StyleSheetContentsCache` into the `14.xx` fork. |

---

## 3. Root Cause Architecture Notes (Non-JIT)

### 3.1 `CSSSegmentedFontFace::fontRanges()` Container Reentrancy
Holding a C++ reference (`auto&`) into a `WTF::HashMap` bucket across any call that resolves promises or fires synchronous observer notifications creates a classic container-rehash lifetime flaw. Whenever `FontDescriptionKey` incorporates `computedSize`, reentrant font measurements insert distinct keys until `m_cache` reallocates its backing table, invalidating the outer stack frame's reference.

### 3.2 `InternalReadableStream::tee()` vs `TransformStream` Cherry-Pick Gap
While Sony selectively backported the June 30, 2026 `TransformStream` fix (`8fd92b1021`, throwing `TypeError` on unexpected iterator returns), the sibling call site in `InternalReadableStream::tee()` relied on `SequenceConverter<IDLObject>::convert` + debug-only `ASSERT(results.size() == 2)` until November 6, 2026—four days after FW `14.22` shipped. Avoiding protocol-observable iteration on internal JS builtin return tuples removes the mismatch completely.

### 3.3 `XMLDocumentParser::startElementNs()` Span Lifetime Ordering
During SAX2 callbacks, `libxml2` passes raw pointers into its active parser input buffer. Running `performMicrotaskCheckpoint()` before copying those pointers allows DOM/frame detachment handlers to free the underlying `xmlParserInputBuffer` before `handleElementAttributes()` reads it. Eagerly copying spans into `Vector<Attribute>` enforces lifetime ownership prior to any script execution.

---

## 4. Source-Verified Exclusions (Preventing False Leads on FW 14.22)

Direct commit inspection in `WebKit/WebKit` eliminates two widely circulated non-applicable CVE candidates for PlayStation 5 FW `14.22`:

| CVE ID | Upstream Fix Commit & Bugzilla | Exact Patched File(s) | PS5 `14.22` Verdict | Reason |
| :--- | :--- | :--- | :--- | :--- |
| **`CVE-2025-43265`** *(Safari 18.6)* | `d96ab2fa64` *(Bug `294182`)* | `Source/JavaScriptCore/yarr/YarrJIT.cpp` (`+8/-39`) | **CONFIG-SILENT / NON-APPLICABLE** | Exclusively modifies `YarrJIT`, which is compiled out on PlayStation 5 (`ENABLE_JIT=0`). |
| **`CVE-2026-64718`** *(Safari 26.6 / 27)* | `95f9f59bb1`, `2917a663b0` *(Bug `313935`)* | `Source/WebCore/platform/graphics/cg/PathCG.cpp` *(with `OffscreenCanvas` worker test)* | **NON-APPLICABLE** | Targets Apple's CoreGraphics backend (`PathCG`) and `OffscreenCanvas`; PS5 builds without CoreGraphics and reports `OffscreenCanvas=false`. |

---

## 5. Next Verification & Defensive Testing

1. Run [`tests/test_webcore_1422_invariants.c`](tests/test_webcore_1422_invariants.c) (`make -C tests run`) to verify the `HashMap` bucket-rehash, `libxml2` span ownership ordering, and `InternalReadableStream::tee` validation models.
2. Confirm runtime patch state of `InternalReadableStream::tee()` (`ReadableStream.prototype.tee`) and `XMLDocumentParser` custom-element microtask handling on target firmware via safe exception/boundary checks (`TypeError` assertions).
3. Cross-reference [`FIRMWARE_COMPATIBILITY_1422.md`](FIRMWARE_COMPATIBILITY_1422.md) with any vendor security advisories published after November 2, 2026 (`Safari 27.2+` / `WebKitGTK 2.54+`).
