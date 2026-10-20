// SPDX-License-Identifier: MIT
// Independent xref-table fixture; adapted from the iisacc MIT engineering counterpart.
#pragma once
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

inline std::string fixtureStream(std::string_view body, std::string_view dictionary = {}) {
    return "<< /Length " + std::to_string(body.size()) + " " + std::string(dictionary) + " >>\nstream\n" + std::string(body) + "\nendstream";
}
// Fixture serialization is independent of QPDF. It writes real xref-table PDF bytes,
// with inherited geometry, page content, fields, an attachment and preserved XMP.
inline std::string pdfFixture(bool signedMarkers = false, bool xfa = false, std::string fieldPolicy = {}, int inheritedRotation = 0) {
    std::vector<std::string> objects{
        "<< /Type /Catalog /Pages 2 0 R /AcroForm 14 0 R /Names 17 0 R /Metadata 15 0 R " +
            std::string(signedMarkers ? "/Perms << /DocMDP 19 0 R >> " : "") + ">>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 612 792] "
            + std::string(inheritedRotation ? "/Rotate " + std::to_string(inheritedRotation) : "") + " >>",
        "<< /Type /Page /Parent 2 0 R /CropBox [-10 -20 600 760] /Resources 5 0 R /Contents 6 0 R /Annots [8 0 R 9 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [-30 15 450 715] /Rotate -90 /UserUnit 2 /Resources 5 0 R /Contents 7 0 R /Annots [10 0 R] >>",
        "<< /Font << /F1 11 0 R /Helv 11 0 R >> >>",
        fixtureStream("BT /F1 12 Tf 72 720 Td (Page One fixture) Tj ET\n"),
        fixtureStream("BT /F1 12 Tf 50 600 Td (Page Two fixture) Tj ET\n"),
        "<< /Type /Annot /Subtype /Text /Contents (original note) /Rect [30 40 50 60] /P 3 0 R >>",
        "<< " + fieldPolicy + " /Type /Annot /Subtype /Widget /FT /Tx /T (customer) /V (before) /DA (/Helv 12 Tf 0 g) /Rect [72 650 220 680] /P 3 0 R /AP << /N 16 0 R >> >>",
        "<< /Type /Annot /Subtype /Widget /FT /Btn /T (accepted) /V /Off /Rect [50 500 65 515] /P 4 0 R >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
        fixtureStream("fixture data\n", "/Type /EmbeddedFile /Subtype /text#2fplain /Params << /Size 13 >>"),
        "<< /Type /Filespec /F (attachment.txt) /UF (attachment.txt) /Desc (Fixture attachment) /EF << /F 12 0 R >> >>",
        "<< /Fields [9 0 R 10 0 R] /DR 5 0 R /DA (/Helv 12 Tf 0 g) " + std::string(xfa ? "/XFA 15 0 R " : "") + ">>",
        fixtureStream("<?xpacket begin=''?><x:xmpmeta xmlns:x='adobe:ns:meta/'></x:xmpmeta><?xpacket end='w'?>", "/Type /Metadata /Subtype /XML"),
        fixtureStream("BT /Helv 12 Tf 0 g 0 0 Td (before) Tj ET", "/Type /XObject /Subtype /Form /BBox [0 0 148 30] /Resources 5 0 R"),
        "<< /EmbeddedFiles 18 0 R >>",
        "<< /Names [(attachment.txt) 13 0 R] >>",
        signedMarkers ? "<< /Type /Sig /ByteRange [0 1 2 3] /Contents <00> /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached >>" : "null",
        "<< /Title (Original fixture) /Author (Independent test) /Subject (Preserve me) >>"
    };
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets{0};
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(static_cast<std::size_t>(out.tellp()));
        out << i + 1 << " 0 obj\n" << objects[i] << "\nendobj\n";
    }
    const auto xref = static_cast<std::size_t>(out.tellp());
    out << "xref\n0 " << offsets.size() << "\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) out << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    out << "trailer\n<< /Size " << offsets.size() << " /Root 1 0 R /Info 20 0 R >>\nstartxref\n" << xref << "\n%%EOF\n";
    return out.str();
}
