# Supported formats and known limits

| Format | Searchable in core | Built-in view | Source location | Optional capability |
| --- | --- | --- | --- | --- |
| PDF with text | Yes | Yes | Page | None |
| Scanned PDF | Metadata in core | Yes | Page | OCR for searchable text |
| TXT, Markdown, CSV, JSON, source code | Yes | Yes | Text offset / passage | None |
| DOCX | Yes, from document XML | Extracted text | Paragraph | LibreOffice for faithful rendering |
| PPTX | Yes, from slide XML | Extracted text | Slide | LibreOffice for faithful rendering |
| PNG, JPEG, BMP, GIF, WebP | Metadata in core | Yes | File | OCR for embedded text |
| MP3, WAV, M4A, MP4, MOV, MKV | Metadata in core | External open | Timestamp when transcribed | Media playback/conversion and transcription |
| Legacy DOC, XLS, PPT | Metadata only | External open | File | LibreOffice conversion |
| ELLA Markdown note | Yes | Yes | Text / source backlink | None |

Password-protected, encrypted, malformed, or unsupported files remain in the
catalog with an actionable extraction status. ELLA never treats a failed extract
as permission to modify the source. Unicode filenames and extracted text are
stored through Qt and SQLite.

DOCX and PPTX support extracts readable text rather than reproducing the Office
layout. Spreadsheet cell extraction is outside the 1.0 scope. PDF text order
depends on the PDF's text layer. OCR quality and languages depend on the chosen
Tesseract package.

Semantic retrieval is English-first in 1.0 and requires an audited optional
`e5-small-v2` component plus a built passage index. Keyword and phrase search stay
available without it. There is no cloud account, AI chat, browser extension,
collaboration, graph view, or cross-platform installer in 1.0.
