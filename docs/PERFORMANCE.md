# Form field identity lookup

PDF writing builds a local ordered index of source object-number/generation
pairs once, then resolves each existing form field through that index. Reordered
fields retain their original object identities without rescanning every PDF
field for every document field. Duplicate references retain first-match behavior.

The original-name fallback remains a live search because renaming a field or a
parent can change fully-qualified names during the same write. It is not cached
using stale names. The index is private to one write and does not survive a PDF
mutation or hold references across documents.

`PdfFormLookupTest` creates an independent xref-table PDF with 256 fields, reverses
the document field order, exercises missing-source name fallback, renames all
fields and verifies that all original values and identities survive a round trip.
