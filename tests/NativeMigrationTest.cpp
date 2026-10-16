#include "TestSupport.h"
#include <filesystem>
#include <iiFileProviderNative.h>
#include <iiGeneralDocument.h>
#include <nlohmann/json.hpp>
using namespace ii::document;
using namespace iiFileProvider::native;
int main() {
    const std::filesystem::path fixtures{IIGENERALDOCUMENT_MIGRATION_FIXTURES};
    const auto legacyBytes = File::read(fixtures / "legacy.tsdoc");
    const auto old = nlohmann::json::parse(legacyBytes);
    auto document = ThinkingSpaceDocument::fromFileBytes(legacyBytes);
    expect(document.header.metadata.at("Title") == "After", "legacy header is preserved");
    expect(document.body.htmlBlocks.html() == "<main><p>원본 📄</p></main>",
           "legacy Unicode body is preserved");
    expect(document.versionHistory.verifyIntegrity(), "legacy SHA-256 history remains valid");
    const auto &versions = document.versionHistory.versions();
    expect(versions.size() == old["history"]["versions"].size(),
           "legacy history count is preserved");
    for (std::size_t i = 0; i < versions.size(); ++i)
        expect(versions[i].objectId == old["history"]["versions"][i]["id"].get<std::string>(),
               "legacy history object IDs do not change");
    auto legacyProfile = FileAuthor::fromJsonText(File::read(fixtures / "legacy-profile.json"));
    expect(legacyProfile.has_value(), "legacy detailed author profile is readable");
    expect(nlohmann::json::parse(legacyProfile->toJsonText()) ==
               nlohmann::json::parse(File::read(fixtures / "legacy-profile.json")),
           "legacy profile retains all details and attribution");
    expect(nlohmann::json::parse(document.authorship().dump()) ==
               nlohmann::json::parse(File::read(fixtures / "legacy-authorship.json")),
           "legacy authorship ledger is semantically identical");
    expect(!document.authorship().hasActiveAuthor(),
           "persisted profiles cannot select the active editor");
    auto reopened = ThinkingSpaceDocument::fromFileBytes(document.toFileBytes());
    expect(reopened.versionHistory.verifyIntegrity() &&
               reopened.versionHistory.head()->objectId == versions.back().objectId,
           "schema 1 to 2 upgrade retains history hashes");
    auto html = HtmlBlockDocument::fromFileBytes(File::read(fixtures / "legacy.html"));
    expect(html.authorship().firstEditor()->metadata().account.sub == "legacy-profile",
           "legacy Base64 authorship comment is preserved");
    // Byte diffs intentionally share partial UTF-8 characters. Serialization must
    // retain these bytes.
    ThinkingSpaceDocument unicode;
    unicode.header.metadata["Title"] = "나";
    unicode.body.htmlBlocks = HtmlBlockDocument::fromHtml("<p>😀</p>");
    auto first = unicode.recordVersion("시작", "2026-10-14T00:00:00.000Z");
    unicode.header.metadata["Title"] = "너";
    unicode.body.htmlBlocks = HtmlBlockDocument::fromHtml("<p>😁</p>");
    auto second = unicode.recordVersion("수정", "2026-10-14T00:01:00.000Z");
    auto roundtrip = ThinkingSpaceDocument::fromFileBytes(unicode.toFileBytes());
    expect(roundtrip.versionHistory.verifyIntegrity(),
           "Unicode byte diffs round-trip without invalid UTF-8 JSON strings");
    expect(roundtrip.versionHistory.head()->objectId == second.objectId,
           "Unicode diff and commit hashes are preserved");
    auto before = roundtrip.versionHistory.findSnapshot(first.snapshotObjectId);
    auto diff = roundtrip.versionHistory.findDiff(second.diffObjectId);
    expect(before != nullptr && diff != nullptr,
           "previous Unicode snapshot and diff remain addressable");
    expect(before->headerMetadata.at("Title") == "나" &&
               diff->body.apply(before->bodyHtml) == std::optional<std::string>("<p>😁</p>"),
           "Unicode snapshot and byte diff retain exact content");
    return 0;
}
