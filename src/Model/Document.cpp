#include "Model/Document.h"

#include <utility>

namespace ii::document {

FormFieldId FormField::id() const noexcept
{
    return id_;
}

void FormField::setId(FormFieldId id) noexcept
{
    id_ = id;
}

const std::string& FormField::name() const noexcept
{
    return name_;
}

void FormField::setName(std::string utf8Name)
{
    name_ = std::move(utf8Name);
}

const std::string& FormField::originalName() const noexcept
{
    return originalName_;
}

void FormField::setOriginalName(std::string utf8Name)
{
    originalName_ = std::move(utf8Name);
}

FormFieldType FormField::type() const noexcept
{
    return type_;
}

void FormField::setType(FormFieldType type) noexcept
{
    type_ = type;
}

const PdfValue& FormField::value() const noexcept
{
    return value_;
}

void FormField::setValue(PdfValue value)
{
    value_ = std::move(value);
    utf8Value_.reset();
}

const std::optional<std::string>& FormField::utf8Value() const noexcept
{
    return utf8Value_;
}

void FormField::setUtf8Value(std::string value)
{
    utf8Value_ = std::move(value);
}

void FormField::clearUtf8Value() noexcept
{
    utf8Value_.reset();
}

int FormField::flags() const noexcept
{
    return flags_;
}

void FormField::setFlags(int flags) noexcept
{
    flags_ = flags;
}

const std::vector<std::string>& FormField::choices() const noexcept
{
    return choices_;
}

void FormField::setChoices(std::vector<std::string> choices)
{
    choices_ = std::move(choices);
}

const std::optional<SourceReference>& FormField::source() const noexcept
{
    return source_;
}

void FormField::setSource(std::optional<SourceReference> source) noexcept
{
    source_ = source;
}

Document::Document() = default;
Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;
Document::~Document() = default;

const std::vector<Page>& Document::pages() const noexcept
{
    return pages_;
}

std::vector<Page>& Document::pages() noexcept
{
    return pages_;
}

void Document::addPage(Page page)
{
    pages_.push_back(std::move(page));
}

const std::map<std::string, std::string>& Document::metadata() const noexcept
{
    return metadata_;
}

std::map<std::string, std::string>& Document::metadata() noexcept
{
    return metadata_;
}

const std::vector<FormField>& Document::formFields() const noexcept
{
    return formFields_;
}

std::vector<FormField>& Document::formFields() noexcept
{
    return formFields_;
}

void Document::addFormField(FormField field)
{
    formFields_.push_back(std::move(field));
}

const std::string& Document::pdfVersion() const noexcept
{
    return pdfVersion_;
}

void Document::setPdfVersion(std::string version)
{
    pdfVersion_ = std::move(version);
}

bool Document::isEncrypted() const noexcept
{
    return origin_.has_value() && origin_->encrypted;
}

bool Document::hasDigitalSignatures() const noexcept
{
    return origin_.has_value() && origin_->digitallySigned;
}

bool Document::hasSourcePdf() const noexcept
{
    return origin_.has_value();
}

} // namespace ii::document
