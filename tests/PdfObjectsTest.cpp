#include <iiGeneralDocument/Pdf/Objects.h>
#include "TestSupport.h"
#include "PdfFixture.h"
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace pdf = ii::document::pdf;
template<class T> T success(pdf::Result<T> value) {
    if(!value)throw std::runtime_error(value.error().message);
    return std::move(*value);
}
void success(pdf::Result<void> value) {if(!value)throw std::runtime_error(value.error().message);}

int main() {
    try {
        std::string raw=pdfFixture();
        const auto original=std::as_bytes(std::span(raw));
        auto document=success(pdf::Document::fromBytes(original));
        const auto owned=document.toBytes();
        raw.assign(raw.size(), 'x');
        expect(document.toBytes()==owned,"Document owns input bytes independently of the caller");
        expect(document.pages().size()==2&&document.pages()[1].index==1,"Object page indices are zero based");
        expect(document.pages()[1].clockwiseRotation==270&&document.pages()[1].userUnit==2,"Original geometry retained");
        expect(document.formFields().size()==2&&document.attachments().size()==1,"Forms and attachments absorbed");
        pdf::Editor editor(document);
        const std::size_t second[]{1};success(editor.rotatePages(second,-450));
        expect(document.pages()[1].clockwiseRotation==180,"Object edit commits relative rotation");
        const pdf::MetadataChange metadata[]{{"Title","흡수한 문서 📄"},{"Author",std::nullopt}};
        success(editor.setMetadata(metadata));
        expect(document.metadata().at("Title")=="흡수한 문서 📄"&&!document.metadata().contains("Author"),"Unicode metadata and removal");
        success(editor.addTextNote(1,{-10,40,10,65},"주석 📄","iisacc"));
        expect(document.pages()[1].annotations.size()==2,"Note object committed");
        success(editor.setTextField("customer","대한민국 📄"));
        expect(document.formFields()[0].value=="대한민국 📄"&&document.needsAppearances(),"Logical form object committed");
        const auto before=document.toBytes();
        const std::size_t invalid[]{2};expect(!editor.rotatePages(invalid,90),"Invalid page rejected");
        expect(!editor.setTextField("accepted","value"),"Non-text field rejected");
        expect(document.toBytes()==before,"Failed object edits preserve bytes");
        const std::size_t selected[]{1,0,0};auto extracted=success(document.extractPages(selected));
        expect(extracted.pages().size()==3&&extracted.formFields().size()==3,"Ordered/repeated extraction preserves forms");
        expect(extracted.attachments().empty()&&extracted.metadata().empty(),"Page copy document-level scope");
        const pdf::PageSelection selections[]{{std::cref(document),{1}},{std::cref(document),{0,1}}};
        auto merged=success(pdf::Document::merge(selections));
        expect(merged.pages().size()==3&&merged.formFields().size()==3,"Object merge preserves selection order");
        std::set<std::string> names;for(const auto& field:merged.formFields())names.insert(field.name);
        expect(names.size()==merged.formFields().size(),"Merged form names are unique");

        auto reopened=success(pdf::Document::fromBytes(document.toBytes()));
        expect(reopened.metadata().at("Title")=="흡수한 문서 📄"&&reopened.attachments().size()==1,"Complete object serialization round trip");
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
