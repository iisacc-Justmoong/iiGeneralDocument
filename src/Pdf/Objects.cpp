// SPDX-License-Identifier: AGPL-3.0-only
// Adapted from the iisacc-authored Acrobat engineering counterpart (MIT).
// The preserved MIT notice is in third_party/licenses/acrobat-counterpart/LICENSE.
#include "Pdf/Objects.h"
#include "Pdf/Private/TextValidation.h"
#include <qpdf/Buffer.hh>
#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ii::document::pdf {
namespace {
using Object = QPDFObjectHandle;
struct Snapshot {
    std::string version;
    std::vector<Page> pages;
    std::map<std::string,std::string> metadata;
    std::vector<FormField> fields;
    std::vector<Attachment> attachments;
    bool containsSignatures{};
    std::size_t signatureDictionaryCount{};
    bool needsAppearances{};
    bool hasXfa{};
    std::vector<std::string> warnings;
};
struct Failure { Error error; };
[[noreturn]] void fail(ErrorCode code, std::string message) {
    throw Failure{{code, std::move(message)}};
}
template<class T, class Fn> Result<T> guarded(Fn&& function) {
    try {
        if constexpr (std::is_void_v<T>) { function(); return {}; }
        else return function();
    } catch (const Failure& f) {
        return std::unexpected(f.error);
    } catch (const QPDFExc& e) {
        return std::unexpected(Error{
            e.getErrorCode() == qpdf_e_password ? ErrorCode::encryptedInput : ErrorCode::malformedPdf,
            e.what()});
    } catch (const std::exception& e) {
        return std::unexpected(Error{ErrorCode::backendFailure, e.what()});
    }
}
void validateUtf8(std::string_view value) {
    if (!detail::validPdfText(value))
        fail(ErrorCode::invalidInput, "Text must be valid UTF-8 without NUL and at most 1 MiB.");
}
void load(QPDF& q,std::span<const std::byte> bytes) {
    if(bytes.empty())fail(ErrorCode::invalidInput,"PDF bytes must be nonempty.");
    q.setSuppressWarnings(true);q.setAttemptRecovery(false);
    q.processMemoryFile("iiGeneralDocument.Pdf",reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if(q.isEncrypted())fail(ErrorCode::encryptedInput,"Encrypted input is not supported by the native PDF object codec.");
}
bool signatureMarker(Object object) {
    if (!object.isDictionary()) return false;
    return object.hasKey("/ByteRange") ||
        (object.getKey("/Type").isNameAndEquals("/Sig") && !object.getKey("/Contents").isNull());
}
std::size_t signatures(QPDF& q) {
    std::size_t count{};
    std::set<QPDFObjGen> seen;
    for (auto object : q.getAllObjects()) {
        if (signatureMarker(object)) {
            ++count;
            if (object.isIndirect()) seen.insert(object.getObjGen());
        }
    }
    for (auto field : QPDFAcroFormDocumentHelper(q).getFormFields()) {
        auto value = field.getValue();
        if (field.getFieldType() == "/Sig" && !value.isNull() &&
            (!value.isIndirect() || !seen.contains(value.getObjGen()))) {
            ++count;
            if (value.isIndirect()) seen.insert(value.getObjGen());
        }
    }
    const auto perms = q.getRoot().getKey("/Perms");
    const auto docMdp = perms.isDictionary() ? perms.getKey("/DocMDP") : Object::newNull();
    if (!docMdp.isNull() && (!docMdp.isIndirect() || !seen.contains(docMdp.getObjGen()))) ++count;
    return count;
}
void mutableInput(QPDF& q) {
    if (signatures(q) != 0)
        fail(ErrorCode::signedInput, "Signed/ByteRange/DocMDP input is refused; signatures are not validated or rewritten.");
    auto acro = q.getRoot().getKey("/AcroForm");
    if (acro.isDictionary() && !acro.getKey("/XFA").isNull())
        fail(ErrorCode::unsupportedFeature, "Editing XFA documents is not supported.");
}
std::optional<Box> rectangle(Object a) {
    if (!a.isArray() || a.getArrayNItems() != 4) return std::nullopt;
    double values[4]{};
    for (int i = 0; i < 4; ++i) {
        if (!a.getArrayItem(i).getValueAsNumber(values[i]) || !std::isfinite(values[i])) return std::nullopt;
    }
    if (values[2] <= values[0] || values[3] <= values[1]) return std::nullopt;
    return Box{values[0], values[1], values[2], values[3]};
}
void validateBox(Box r) {
    if (!std::isfinite(r.x0) || !std::isfinite(r.y0) || !std::isfinite(r.x1) || !std::isfinite(r.y1) ||
        r.x1 <= r.x0 || r.y1 <= r.y0)
        fail(ErrorCode::invalidInput, "Box must contain four finite, increasing coordinates.");
}
int rotation(Object value) {
    if (value.isNull()) return 0;
    long long number{};
    if (!value.getValueAsInt(number) || number % 90 != 0)
        fail(ErrorCode::malformedPdf, "Page Rotate must be an integer multiple of 90.");
    return static_cast<int>((number % 360 + 360) % 360);
}
std::string text(Object value) {
    if (value.isString()) return value.getUTF8Value();
    if (value.isName()) return value.getName();
    if (value.isNull()) return {};
    if (value.isArray()) return "[array]";
    if (value.isDictionary()) return "[dictionary]";
    if (value.isStream()) return "[stream]";
    return value.unparse();
}
std::vector<std::string> warnings(QPDF& q) {
    std::vector<std::string> result;
    for (const auto& warning : q.getWarnings()) result.emplace_back(warning.what());
    return result;
}
Snapshot describe(QPDF& q) {
    Snapshot result;
    result.version = q.getPDFVersion();
    result.signatureDictionaryCount = signatures(q);
    result.containsSignatures = result.signatureDictionaryCount != 0;
    const auto info = q.getTrailer().getKey("/Info");
    if (info.isDictionary()) {
        for (const auto& key : info.getKeys()) {
            const auto value = info.getKey(key);
            if (value.isString()) result.metadata.emplace(key.substr(1), value.getUTF8Value());
        }
    }
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    result.pages.reserve(pages.size());
    for (std::size_t i = 0; i < pages.size(); ++i) {
        auto& source = pages[i];
        const auto media = rectangle(source.getMediaBox());
        const auto crop = rectangle(source.getCropBox());
        if (!media || !crop) fail(ErrorCode::malformedPdf, "Page box must contain four finite, increasing coordinates.");
        Page page;
        page.index = i;
        page.mediaBox = *media;
        page.cropBox = *crop;
        page.clockwiseRotation = rotation(source.getAttribute("/Rotate", false));
        const auto userUnit = source.getObjectHandle().getKey("/UserUnit");
        if (!userUnit.isNull() && (!userUnit.getValueAsNumber(page.userUnit) || !std::isfinite(page.userUnit) || page.userUnit <= 0))
            fail(ErrorCode::malformedPdf, "Page UserUnit must be positive and finite.");
        for (auto annotation : source.getAnnotations()) {
            const auto handle = annotation.getObjectHandle();
            page.annotations.push_back({annotation.getSubtype(), text(handle.getKey("/Contents")),
                rectangle(handle.getKey("/Rect")), text(handle.getKey("/T"))});
        }
        result.pages.push_back(std::move(page));
    }
    QPDFAcroFormDocumentHelper forms(q);
    result.needsAppearances = forms.getNeedAppearances();
    auto acro = q.getRoot().getKey("/AcroForm");
    result.hasXfa = acro.isDictionary() && !acro.getKey("/XFA").isNull();
    for (auto field : forms.getFormFields()) {
        const auto maximum = field.getInheritableFieldValue("/MaxLen");
        result.fields.push_back({field.getFullyQualifiedName(), field.getFieldType(), text(field.getValue()),
            static_cast<std::uint32_t>(field.getFlags()),
            maximum.isNull() ? std::nullopt : std::optional<int>{maximum.getIntValueAsInt()}});
    }
    for (const auto& [key, spec] : QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFiles()) {
        Attachment attachment{key, spec->getFilename(), spec->getDescription(), {}, std::nullopt};
        const auto stream = spec->getEmbeddedFileStream();
        if (stream.isStream()) {
            QPDFEFStreamObjectHelper helper(stream);
            attachment.mimeType = helper.getSubtype();
            const auto params = stream.getDict().getKey("/Params");
            unsigned long long size{};
            if (params.isDictionary() && params.getKey("/Size").getValueAsUInt(size)) attachment.declaredSize = size;
        }
        result.attachments.push_back(std::move(attachment));
    }
    result.warnings = warnings(q);
    return result;
}
std::vector<std::size_t> selection(std::span<const std::size_t> requested,std::size_t count,bool emptyMeansAll) {
    std::vector<std::size_t> result;
    if(requested.empty()&&emptyMeansAll) {result.reserve(count);for(std::size_t i=0;i<count;++i)result.push_back(i);}
    else result.assign(requested.begin(),requested.end());
    if(result.empty())fail(ErrorCode::invalidInput,"At least one selected page is required.");
    for(auto index:result)if(index>=count)fail(ErrorCode::invalidInput,"Page indices are zero based and must exist.");
    return result;
}
void copyPage(QPDF& output, QPDF& input, const QPDFPageObjectHelper& page) {
    QPDFPageDocumentHelper destination(output);
    QPDFAcroFormDocumentHelper toForms(output);
    // Analyze existing fields before a copied widget becomes visible on a page.
    // Otherwise QPDF temporarily sees an orphan widget before we register it.
    (void)toForms.getFormFields();
    destination.addPage(page, false);
    auto copied = destination.getAllPages().back();
    QPDFAcroFormDocumentHelper fromForms(input);
    toForms.fixCopiedAnnotations(copied.getObjectHandle(), page.getObjectHandle(), fromForms);
}

template<class Verify>
Bytes serialize(QPDF& q,std::size_t expectedPages,Verify verify) {
    if (!q.getWarnings().empty())
        fail(ErrorCode::verificationFailed, "PDF assembly or editing produced backend warnings.");
    QPDFWriter writer(q);writer.setOutputMemory();writer.setDeterministicID(true);
    writer.setPreserveEncryption(false);writer.write();
    const auto buffer=writer.getBufferSharedPointer();
    Bytes bytes(buffer->getSize());
    std::copy_n(reinterpret_cast<const std::byte*>(buffer->getBuffer()),bytes.size(),bytes.begin());
    QPDF reopened;load(reopened,bytes);const auto state=describe(reopened);
    if(state.pages.size()!=expectedPages||state.containsSignatures||!state.warnings.empty()||!verify(state))
        fail(ErrorCode::verificationFailed,"Serialized PDF failed the object's postconditions.");
    return bytes;
}
bool always(const Snapshot&){return true;}
Document parsed(Bytes bytes) {
    auto result=Document::fromBytes(bytes);
    if(!result)throw Failure{result.error()};
    return std::move(*result);
}
} // namespace

struct Document::Impl { Bytes bytes; Snapshot state; };
Document::Document(std::unique_ptr<Impl> impl) noexcept:impl_(std::move(impl)){}
Document::Document(Document&&) noexcept=default;
Document& Document::operator=(Document&&) noexcept=default;
Document::~Document()=default;

Result<Document> Document::fromBytes(std::span<const std::byte> bytes) {
    return guarded<Document>([&]{
        auto impl=std::make_unique<Impl>();impl->bytes.assign(bytes.begin(),bytes.end());
        QPDF q;load(q,impl->bytes);impl->state=describe(q);
        return Document(std::move(impl));
    });
}
Bytes Document::toBytes() const {if(!impl_)throw std::logic_error("Moved-from PDF object");return impl_->bytes;}
const std::vector<Page>& Document::pages() const {if(!impl_)throw std::logic_error("Moved-from PDF object");return impl_->state.pages;}
const std::vector<FormField>& Document::formFields() const {if(!impl_)throw std::logic_error("Moved-from PDF object");return impl_->state.fields;}
const std::vector<Attachment>& Document::attachments() const {if(!impl_)throw std::logic_error("Moved-from PDF object");return impl_->state.attachments;}
const std::map<std::string,std::string>& Document::metadata() const {if(!impl_)throw std::logic_error("Moved-from PDF object");return impl_->state.metadata;}
std::string_view Document::version() const {if(!impl_)throw std::logic_error("Moved-from PDF object");return impl_->state.version;}
bool Document::hasDigitalSignatures() const {if(!impl_)throw std::logic_error("Moved-from PDF object");return impl_->state.containsSignatures;}
bool Document::hasXfa() const {if(!impl_)throw std::logic_error("Moved-from PDF object");return impl_->state.hasXfa;}
bool Document::needsAppearances() const {if(!impl_)throw std::logic_error("Moved-from PDF object");return impl_->state.needsAppearances;}
const std::vector<std::string>& Document::warnings() const {if(!impl_)throw std::logic_error("Moved-from PDF object");return impl_->state.warnings;}

Result<Document> Document::extractPages(std::span<const std::size_t> requested) const {
    return guarded<Document>([&]{
        const auto bytes=toBytes();QPDF source;load(source,bytes);mutableInput(source);
        auto pages=QPDFPageDocumentHelper(source).getAllPages();
        const auto chosen=selection(requested,pages.size(),false);
        QPDF destination;destination.emptyPDF();destination.setSuppressWarnings(true);
        for(auto index:chosen)copyPage(destination,source,pages[index]);
        return parsed(serialize(destination,chosen.size(),always));
    });
}
Result<Document> Document::merge(std::span<const PageSelection> selections) {
    return guarded<Document>([&]{
        if(selections.empty())fail(ErrorCode::invalidInput,"At least one document selection is required.");
        QPDF destination;destination.emptyPDF();destination.setSuppressWarnings(true);
        std::vector<Bytes> inputs;std::vector<std::unique_ptr<QPDF>> sources;
        inputs.reserve(selections.size());sources.reserve(selections.size());std::size_t total{};
        for(const auto& item:selections) {
            inputs.push_back(item.document.get().toBytes());auto source=std::make_unique<QPDF>();
            load(*source,inputs.back());mutableInput(*source);
            auto pages=QPDFPageDocumentHelper(*source).getAllPages();
            const auto chosen=selection(item.pages,pages.size(),true);
            if(chosen.size()>std::numeric_limits<std::size_t>::max()-total)fail(ErrorCode::invalidInput,"Page count overflow.");
            for(auto index:chosen)copyPage(destination,*source,pages[index]);
            total+=chosen.size();sources.push_back(std::move(source));
        }
        return parsed(serialize(destination,total,always));
    });
}

Editor::Editor(Document& document) noexcept:document_(document){}
Result<void> Editor::rotatePages(std::span<const std::size_t> requested,int degrees) {
    return guarded<void>([&]{
        if(degrees%90)fail(ErrorCode::invalidInput,"Rotation must be a multiple of 90 degrees.");
        const auto bytes=document_.toBytes();QPDF q;load(q,bytes);mutableInput(q);
        auto pages=QPDFPageDocumentHelper(q).getAllPages();const auto before=describe(q);
        const auto chosen=selection(requested,pages.size(),false);
        if(std::set<std::size_t>(chosen.begin(),chosen.end()).size()!=chosen.size())fail(ErrorCode::invalidInput,"Repeated rotation page indices are not allowed.");
        const int normalized=(degrees%360+360)%360;
        for(auto index:chosen)pages[index].rotatePage(normalized,true);
        auto next=serialize(q,pages.size(),[&](const Snapshot& after){
            for(std::size_t i=0;i<pages.size();++i) {
                const bool selected=std::ranges::find(chosen,i)!=chosen.end();
                if(after.pages[i].clockwiseRotation!=(before.pages[i].clockwiseRotation+(selected?normalized:0))%360)return false;
            }return true;
        });
        document_=parsed(std::move(next));
    });
}
Result<void> Editor::setMetadata(std::span<const MetadataChange> changes) {
    return guarded<void>([&]{
        const std::set<std::string> allowed{"Title","Author","Subject","Keywords","Creator","Producer"};
        if(changes.empty())fail(ErrorCode::invalidInput,"At least one metadata change is required.");
        std::set<std::string> keys;
        for(const auto& change:changes) {
            if(!allowed.contains(change.key)||!keys.insert(change.key).second)fail(ErrorCode::invalidInput,"Metadata keys must be supported and unique.");
            if(change.value)validateUtf8(*change.value);
        }
        const auto bytes=document_.toBytes();QPDF q;load(q,bytes);mutableInput(q);
        auto info=q.getTrailer().getKey("/Info");
        if(!info.isDictionary()){info=q.makeIndirectObject(Object::newDictionary());q.getTrailer().replaceKey("/Info",info);}
        for(const auto& change:changes) {
            if(change.value)info.replaceKey("/"+change.key,Object::newUnicodeString(*change.value));
            else info.removeKey("/"+change.key);
        }
        const auto count=document_.pages().size();
        auto next=serialize(q,count,[&](const Snapshot& after){
            for(const auto& change:changes) {
                const auto found=after.metadata.find(change.key);
                if(change.value?(found==after.metadata.end()||found->second!=*change.value):(found!=after.metadata.end()))return false;
            }return true;
        });document_=parsed(std::move(next));
    });
}
Result<void> Editor::addTextNote(std::size_t page,Box box,std::string_view contents,std::string_view author) {
    return guarded<void>([&]{
        validateBox(box);validateUtf8(contents);validateUtf8(author);
        if(contents.empty())fail(ErrorCode::invalidInput,"A note must have nonempty contents.");
        const auto bytes=document_.toBytes();QPDF q;load(q,bytes);mutableInput(q);
        auto pages=QPDFPageDocumentHelper(q).getAllPages();const std::size_t requested[]{page};selection(requested,pages.size(),false);
        const auto oldCount=document_.pages()[page].annotations.size();
        auto target=pages[page].getObjectHandle();auto note=Object::newDictionary();
        note.replaceKey("/Type",Object::newName("/Annot"));note.replaceKey("/Subtype",Object::newName("/Text"));
        note.replaceKey("/Rect",Object::newArray({Object::newReal(box.x0,12),Object::newReal(box.y0,12),Object::newReal(box.x1,12),Object::newReal(box.y1,12)}));
        note.replaceKey("/Contents",Object::newUnicodeString(std::string(contents)));
        note.replaceKey("/T",Object::newUnicodeString(std::string(author)));
        note.replaceKey("/Name",Object::newName("/Note"));note.replaceKey("/Open",Object::newBool(false));note.replaceKey("/P",target);
        auto annotations=target.getKey("/Annots");
        if(annotations.isNull())annotations=Object::newArray();
        else if(!annotations.isArray())fail(ErrorCode::malformedPdf,"Page Annots must be an array.");
        else annotations=annotations.shallowCopy();
        annotations.appendItem(q.makeIndirectObject(note));target.replaceKey("/Annots",annotations);
        auto next=serialize(q,pages.size(),[&](const Snapshot& after){const auto& notes=after.pages[page].annotations;return notes.size()==oldCount+1&&notes.back().subtype=="/Text"&&notes.back().contents==contents;});
        document_=parsed(std::move(next));
    });
}
Result<void> Editor::setTextField(std::string_view name,std::string_view value) {
    return guarded<void>([&]{
        validateUtf8(name);validateUtf8(value);if(name.empty())fail(ErrorCode::invalidInput,"A field name is required.");
        const auto bytes=document_.toBytes();QPDF q;load(q,bytes);mutableInput(q);
        QPDFAcroFormDocumentHelper forms(q);std::vector<QPDFFormFieldObjectHelper> matches;
        for(auto field:forms.getFormFields())if(field.getFullyQualifiedName()==name)matches.push_back(field);
        if(matches.size()!=1)fail(ErrorCode::invalidInput,"Exactly one existing field must match the name.");
        auto& field=matches.front();if(!field.isText())fail(ErrorCode::unsupportedFeature,"Only existing text fields are supported.");
        const auto flags=static_cast<std::uint32_t>(field.getFlags());
        if(flags&1U)fail(ErrorCode::unsupportedFeature,"Read-only fields cannot be updated.");
        if(flags&(1U<<25U))fail(ErrorCode::unsupportedFeature,"Rich text fields cannot be updated.");
        if(!field.getInheritableFieldValue("/MaxLen").isNull())fail(ErrorCode::unsupportedFeature,"MaxLen-constrained fields are not supported.");
        field.setV(std::string(value),true);field.getObjectHandle().removeKey("/RV");
        for(auto widget:forms.getAnnotationsForField(field))widget.getObjectHandle().removeKey("/AP");
        auto next=serialize(q,document_.pages().size(),[&](const Snapshot& after){const auto found=std::ranges::find_if(after.fields,[&](const FormField& f){return f.name==name;});return found!=after.fields.end()&&found->value==value&&after.needsAppearances;});
        document_=parsed(std::move(next));
    });
}
} // namespace ii::document::pdf
