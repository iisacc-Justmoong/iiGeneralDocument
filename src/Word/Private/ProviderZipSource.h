#pragma once

#include <iiFileProvider.h>
#include <zip.h>

namespace ii::document::detail {
// libzip owns this adapter. The device is opened and owned by iiFileProvider;
// ZIP interpretation and decompression remain private to the document codec.
struct ProviderZipInput {
    std::unique_ptr<QIODevice> device;
    zip_error_t error;
    explicit ProviderZipInput(std::unique_ptr<QIODevice> input) : device(std::move(input)) { zip_error_init(&error); }
    ~ProviderZipInput() { zip_error_fini(&error); }
    static zip_int64_t command(void *context, void *data, zip_uint64_t length, zip_source_cmd_t operation) {
        auto &input = *static_cast<ProviderZipInput *>(context);
        switch (operation) {
        case ZIP_SOURCE_OPEN:
            if (input.device->seek(0)) return 0;
            zip_error_set(&input.error, ZIP_ER_SEEK, 0); return -1;
        case ZIP_SOURCE_READ: {
            const auto count = input.device->read(static_cast<char *>(data), static_cast<qint64>(
                std::min<zip_uint64_t>(length, std::numeric_limits<qint64>::max())));
            if (count < 0) zip_error_set(&input.error, ZIP_ER_READ, 0);
            return count;
        }
        case ZIP_SOURCE_CLOSE: return 0;
        case ZIP_SOURCE_STAT: {
            if (length < sizeof(zip_stat_t)) return -1;
            auto &stat = *static_cast<zip_stat_t *>(data); zip_stat_init(&stat);
            stat.size = static_cast<zip_uint64_t>(input.device->size()); stat.valid = ZIP_STAT_SIZE;
            return sizeof(zip_stat_t);
        }
        case ZIP_SOURCE_ERROR: return zip_error_to_data(&input.error, data, length);
        case ZIP_SOURCE_FREE: delete &input; return 0;
        case ZIP_SOURCE_SEEK: {
            const auto offset = zip_source_seek_compute_offset(static_cast<zip_uint64_t>(input.device->pos()),
                static_cast<zip_uint64_t>(input.device->size()), data, length, &input.error);
            if (offset < 0 || !input.device->seek(offset)) { zip_error_set(&input.error, ZIP_ER_SEEK, 0); return -1; }
            return 0;
        }
        case ZIP_SOURCE_TELL: return input.device->pos();
        case ZIP_SOURCE_SUPPORTS: return ZIP_SOURCE_SUPPORTS_SEEKABLE;
        default: zip_error_set(&input.error, ZIP_ER_OPNOTSUPP, 0); return -1;
        }
    }
};

inline zip_t *openProviderZip(const std::filesystem::path &path, int *openError) {
    try {
        auto input = std::make_unique<ProviderZipInput>(iiFileProvider::File::openRead(iiFileProvider::File::pathString(path)));
        zip_error_t error; zip_error_init(&error);
        auto *source = zip_source_function_create(ProviderZipInput::command, input.get(), &error);
        if (source) input.release();
        auto *archive = source ? zip_open_from_source(source, ZIP_RDONLY, &error) : nullptr;
        if (!archive && source) zip_source_free(source);
        *openError = zip_error_code_zip(&error); zip_error_fini(&error);
        return archive;
    } catch (const iiFileProvider::FileError &) { *openError = ZIP_ER_OPEN; return nullptr; }
}
} // namespace ii::document::detail
