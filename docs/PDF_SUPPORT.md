# PDF support contract

## First-class editable objects

| PDF concern | Read | Edit | Write |
| --- | --- | --- | --- |
| Page media box and rotation | Yes | Yes | Yes |
| Text objects and string operands | Yes | Yes | Yes |
| Vector paths and paint operators | Yes | Yes | Yes |
| Image XObjects and encoded bytes | Yes | Yes | Yes |
| Recursive Form XObjects | Yes | Yes, shared-resource semantics | Yes |
| Inline images | Yes | Instruction-level | Yes |
| Shadings | Yes | Instruction-level | Yes |
| Marked content | Yes | Instruction-level | Yes |
| Graphics state and color operators | Yes | Instruction-level | Yes |
| Unknown/future operators | Preserved | Instruction-level | Yes |
| Annotations | Yes | Yes | Yes |
| Existing terminal AcroForm fields | Yes | Yes | Yes |
| Document information dictionary | Common keys | Yes | Yes |

## Preserved but not first-class in 0.1

Outlines, destinations, optional-content groups, embedded files, structure trees, page labels, JavaScript, and arbitrary catalog extensions remain in the QPDF source object graph when a loaded document is rewritten. They do not yet have dedicated domain objects. Replacing a page content stream can still affect signatures, tagged-PDF associations, or producer-specific assumptions, so product workflows must test representative files.

## Explicit limits

- Text operands are font-encoded bytes. Universal Unicode extraction and automatic re-encoding for arbitrary embedded fonts are not claimed.
- New documents use a Helvetica/WinAnsi fallback. Embedding and subsetting a caller-supplied font is not implemented.
- Existing page count and order must remain unchanged. New documents may contain any number of newly created pages.
- Existing AcroForm fields can be updated; constructing a new field/widget/appearance hierarchy is not implemented.
- Digital signatures are never preserved after a rewrite. The writer rejects signed input unless `allowInvalidatingDigitalSignatures` is true.
- Encryption settings are not reproduced. The writer rejects encrypted input unless `allowRemovingEncryption` is true.
- QPDF recovery warnings are surfaced; successful recovery is not proof that a damaged source matches its producer's original intent.

## Verification

The writer validates the model before mutation, writes through QPDF, reopens the destination, parses every page content stream, checks the page count, and returns warnings. Release verification additionally renders representative PDFs with Poppler and visually checks text, paths, images, annotations, clipping, and page geometry.
