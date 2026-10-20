#include "Pdf/PdfDocumentReader.h"
#include "Pdf/PdfDocumentWriter.h"
#include "TestSupport.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

int main()
{
    using namespace ii::document;
    std::string refs;
    constexpr int count=256;
    for (int i=0;i<count;++i) refs+=std::to_string(5+i)+" 0 R ";
    std::vector<std::string> objects{
        "<< /Type /Catalog /Pages 2 0 R /AcroForm 4 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots ["+refs+"] >>",
        "<< /Fields ["+refs+"] >>"};
    for (int i=0;i<count;++i) objects.push_back("<< /Type /Annot /Subtype /Widget /FT /Tx /T (field"+std::to_string(i)+") /V (unchanged) /Rect [0 0 100 20] /P 3 0 R >>");
    std::ostringstream bytes;bytes<<"%PDF-1.7\n";
    std::vector<std::size_t> offsets{0};
    for (std::size_t i=0;i<objects.size();++i) {
        offsets.push_back(static_cast<std::size_t>(bytes.tellp()));
        bytes<<i+1<<" 0 obj\n"<<objects[i]<<"\nendobj\n";
    }
    const auto xref=static_cast<std::size_t>(bytes.tellp());
    bytes<<"xref\n0 "<<offsets.size()<<"\n0000000000 65535 f \n";
    for (std::size_t i=1;i<offsets.size();++i) bytes<<std::setw(10)<<std::setfill('0')<<offsets[i]<<" 00000 n \n";
    bytes<<"trailer\n<< /Size "<<offsets.size()<<" /Root 1 0 R >>\nstartxref\n"<<xref<<"\n%%EOF\n";
    const auto data=bytes.str();
    auto read=PdfDocumentReader{}.readBytes(std::as_bytes(std::span(data)));
    expect(!read.hasErrors() && read.document.formFields().size()==count,"Independent many-field PDF is readable");
    auto& fields=read.document.formFields();
    std::reverse(fields.begin(),fields.end());
    fields.front().setSource(std::nullopt); // Name fallback remains available.
    for (auto& field:fields) field.setName("renamed_"+field.name());
    auto written=PdfDocumentWriter{}.writeBytes(read.document);
    expect(!written.hasErrors(),"Reordered field identities and name fallback save successfully");
    auto reopened=PdfDocumentReader{}.readBytes(written.bytes);
    expect(!reopened.hasErrors() && reopened.document.formFields().size()==count,"All source fields remain present");
    for (const auto& field:reopened.document.formFields()) {
        expect(field.name().starts_with("renamed_field"),"Every source identity was renamed correctly");
        expect(field.value().stringValue()=="unchanged","Renaming retains form values");
    }
    return 0;
}
