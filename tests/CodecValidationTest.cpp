#include "Codec/Private/Base64.h"
#include "Codec/Private/Xml.h"
#include "TestSupport.h"
using namespace ii::document::detail;
int main() {
    for (auto raw : {"<r>&unknown;</r>", "<r>&#0;</r>", "<r>&#xD800;</r>", "<r>&incomplete</r>",
                     "<r a='1' a='2'/>", "<r><unknown:p/></r>",
                     "<r xmlns:a='x' xmlns:b='x' a:v='1' b:v='2'/>"})
        expect(XmlReader(ByteBuffer(raw)).hasError(),
               "malformed entity and namespace declarations fail closed");
    XmlReader valid(ByteBuffer("<r>&amp;&lt;&#x1f600;</r>"));
    expect(valid.readNextStartElement(), "valid XML root opens");
    expect(valid.readElementText().toStdString() == "&<😀",
           "predefined and numeric entities decode once");
    std::string utf16 = "\xff\xfe";
    const std::u16string source = u"<?xml version='1.0' encoding='UTF-16'?><r>한글 😀</r>";
    for (char16_t c : source) {
        utf16 += static_cast<char>(c & 255);
        utf16 += static_cast<char>(c >> 8);
    }
    XmlReader wide{ByteBuffer(utf16)};
    expect(!wide.hasError() && wide.readNextStartElement() &&
               wide.readElementText().toStdString() == "한글 😀",
           "UTF-16 BOM and surrogate pairs remain compatible");
    std::string broken = "\xff\xfe";
    for (char16_t c :
         std::u16string{u'<', u'r', u'>', static_cast<char16_t>(0xd800), u'<', u'/', u'r', u'>'}) {
        broken += static_cast<char>(c & 255);
        broken += static_cast<char>(c >> 8);
    }
    XmlReader badWide{ByteBuffer(broken)};
    expect(badWide.hasError(), "invalid UTF-16 does not silently lose a surrogate");
    bool rejected = false;
    ByteBuffer output;
    try {
        XmlWriter writer(output);
        writer.writeStartDocument(Text("1.0"));
        writer.writeStartElement(Text("r"));
        writer.writeCharacters(Text(std::string(1, '\0')));
    } catch (const ii::document::DocumentError &) {
        rejected = true;
    }
    expect(rejected, "XML writer rejects invalid scalar content");
    std::string binary("a\0\xff", 3);
    expect(decodeBase64(encodeBase64(binary)) == binary,
           "Base64 preserves arbitrary history bytes");
    for (auto encoded : {"YQ=A", "YR==", "YQ=", "!Q=="}) {
        rejected = false;
        try {
            (void)decodeBase64(encoded);
        } catch (const ii::document::DocumentError &) {
            rejected = true;
        }
        expect(rejected, "Base64 rejects malformed and noncanonical encodings");
    }
    return 0;
}
