#include "core.h"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <fstream>

namespace uvsr::launcher
{
    namespace
    {
        uint64_t Little(std::span<const unsigned char> bytes, size_t offset, size_t count)
        {
            Require(count <= 8 && offset <= bytes.size() && count <= bytes.size() - offset, "The ZIP header is truncated.");
            uint64_t result = 0;
            for (size_t i = 0; i < count; ++i) result |= uint64_t(bytes[offset + i]) << (i * 8);
            return result;
        }
        struct Archive
        {
            Handle file;
            uint64_t size = 0;
            explicit Archive(const fs::path& path)
                : file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr))
            {
                WinCheck(file.value != INVALID_HANDLE_VALUE, "Open renderer archive");
                LARGE_INTEGER length{}; WinCheck(GetFileSizeEx(file, &length), "Inspect renderer archive");
                size = uint64_t(length.QuadPart);
                Require(size >= 22 && size <= MaximumArchiveBytes, "The renderer archive size is invalid.");
            }
            void Read(uint64_t offset, std::span<unsigned char> bytes)
            {
                Require(offset <= size && bytes.size() <= size - offset && bytes.size() <= UINT32_MAX, "The ZIP record exceeds the archive boundary.");
                LARGE_INTEGER position{}; position.QuadPart = offset;
                WinCheck(SetFilePointerEx(file, position, nullptr, FILE_BEGIN), "Read ZIP record");
                DWORD count = 0;
                WinCheck(::ReadFile(file, bytes.data(), DWORD(bytes.size()), &count, nullptr) && count == bytes.size(), "Read ZIP record");
            }
            std::vector<unsigned char> Read(uint64_t offset, size_t count)
            { std::vector<unsigned char> result(count); Read(offset, result); return result; }
        };
        struct Entry
        {
            std::string name;
            uint64_t compressed = 0, expanded = 0, offset = 0, data = 0;
            uint32_t crc = 0;
            uint16_t flags = 0, method = 0;
            bool directory = false;
        };
        void Zip64(std::span<const unsigned char> extra, uint64_t& expanded, uint64_t& compressed,
            uint64_t& offset, uint64_t& disk)
        {
            bool found = false;
            for (size_t begin = 0; begin < extra.size();)
            {
                auto kind = Little(extra, begin, 2), count = Little(extra, begin + 2, 2);
                begin += 4;
                Require(count <= extra.size() - begin, "The ZIP extra record is truncated.");
                if (kind == 1)
                {
                    Require(!found, "The ZIP has duplicate ZIP64 records."); found = true;
                    auto values = extra.subspan(begin, size_t(count)); size_t cursor = 0;
                    for (auto* target : {&expanded, &compressed, &offset, &disk})
                        if (*target == UINT32_MAX)
                        {
                            auto width = target == &disk ? 4u : 8u;
                            *target = Little(values, cursor, width); cursor += width;
                        }
                }
                begin += size_t(count);
            }
            Require(expanded != UINT32_MAX && compressed != UINT32_MAX && offset != UINT32_MAX && disk != UINT32_MAX,
                "The ZIP is missing required ZIP64 sizes.");
        }
        std::vector<Entry> Inspect(Archive& archive)
        {
            auto tail = archive.Read(archive.size - std::min<uint64_t>(archive.size, 65557), size_t(std::min<uint64_t>(archive.size, 65557)));
            size_t end = tail.size();
            for (size_t at = tail.size() - 22;; --at)
            {
                if (Little(tail, at, 4) == 0x06054b50 && at + 22 + Little(tail, at + 20, 2) == tail.size()) { end = at; break; }
                if (!at) break;
            }
            Require(end != tail.size(), "The renderer ZIP has no valid end record.");
            Require(Little(tail, end + 4, 2) == 0 && Little(tail, end + 6, 2) == 0 &&
                Little(tail, end + 8, 2) == Little(tail, end + 10, 2), "Split ZIP archives are not accepted.");
            uint64_t count = Little(tail, end + 10, 2), centralSize = Little(tail, end + 12, 4), central = Little(tail, end + 16, 4);
            uint64_t endOffset = archive.size - tail.size() + end;
            uint64_t directoryEnd = endOffset;
            if (count == UINT16_MAX || centralSize == UINT32_MAX || central == UINT32_MAX)
            {
                Require(endOffset >= 20, "The ZIP64 locator is missing.");
                auto locator = archive.Read(endOffset - 20, 20);
                Require(Little(locator, 0, 4) == 0x07064b50 && Little(locator, 4, 4) == 0 && Little(locator, 16, 4) == 1, "The ZIP64 locator is invalid.");
                const auto offset = Little(locator, 8, 8);
                auto extended = archive.Read(offset, 56);
                Require(Little(extended, 0, 4) == 0x06064b50 && Little(extended, 4, 8) >= 44 &&
                    Little(extended, 4, 8) <= 65536 && offset + 12 + Little(extended, 4, 8) == endOffset - 20 &&
                    Little(extended, 16, 4) == 0 && Little(extended, 20, 4) == 0 &&
                    Little(extended, 24, 8) == Little(extended, 32, 8), "The ZIP64 end record is invalid.");
                count = Little(extended, 32, 8); centralSize = Little(extended, 40, 8); central = Little(extended, 48, 8); directoryEnd = offset;
            }
            Require(count > 0 && count <= 100001 && central <= directoryEnd && centralSize == directoryEnd - central,
                "The ZIP central directory has an invalid boundary or entry count.");
            std::vector<Entry> entries; entries.reserve(size_t(count));
            std::set<std::string> paths;
            uint64_t cursor = central, expandedTotal = 0;
            for (uint64_t i = 0; i < count; ++i)
            {
                Require(cursor <= directoryEnd && directoryEnd - cursor >= 46, "The ZIP central directory is truncated.");
                auto header = archive.Read(cursor, 46);
                Require(Little(header, 0, 4) == 0x02014b50, "The ZIP central directory signature is invalid.");
                size_t nameSize = size_t(Little(header, 28, 2)), extraSize = size_t(Little(header, 30, 2)), commentSize = size_t(Little(header, 32, 2));
                const auto fullSize = 46 + nameSize + extraSize + commentSize;
                Require(fullSize <= directoryEnd - cursor && nameSize > 0, "The ZIP member header is invalid.");
                auto fields = archive.Read(cursor + 46, nameSize + extraSize);
                Entry entry;
                entry.name.assign(reinterpret_cast<const char*>(fields.data()), nameSize);
                entry.flags = uint16_t(Little(header, 8, 2)); entry.method = uint16_t(Little(header, 10, 2));
                entry.crc = uint32_t(Little(header, 16, 4)); entry.compressed = Little(header, 20, 4); entry.expanded = Little(header, 24, 4);
                entry.offset = Little(header, 42, 4); uint64_t disk = Little(header, 34, 2);
                if (disk == UINT16_MAX) disk = UINT32_MAX;
                Zip64(std::span(fields).subspan(nameSize), entry.expanded, entry.compressed, entry.offset, disk);
                Require(disk == 0 && (entry.method == 0 || entry.method == 8) && (entry.flags & ~uint16_t(0x080e)) == 0,
                    "The ZIP member uses unsupported encryption, flags, disks, or compression.");
                entry.directory = entry.name.ends_with('/');
                if (entry.directory) entry.name.pop_back();
                ValidateRelativePath(entry.name);
                auto attributes = Little(header, 38, 4);
                auto unixType = (attributes >> 16) & 0170000;
                Require(unixType == 0 || unixType == (entry.directory ? 0040000 : 0100000), "Links and special files are forbidden in renderer archives.");
                Require(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT) && paths.emplace(Lower(entry.name)).second &&
                    (entry.name == PackageName && !entry.directory || AllowedPackagePath(entry.name, entry.directory)),
                    "The renderer archive has an unsafe, duplicate, or unexpected path.");
                Require(entry.expanded <= MaximumExpandedBytes - expandedTotal && (!entry.directory || entry.expanded == 0), "The renderer archive exceeds its expanded-size limit.");
                expandedTotal += entry.expanded;
                Require(entry.offset < central && central - entry.offset >= 30, "The ZIP local header overlaps its central directory.");
                auto local = archive.Read(entry.offset, 30);
                auto localNameSize = size_t(Little(local, 26, 2)), localExtraSize = size_t(Little(local, 28, 2));
                Require(Little(local, 0, 4) == 0x04034b50 && Little(local, 6, 2) == entry.flags && Little(local, 8, 2) == entry.method &&
                    localNameSize == nameSize && 30 + localNameSize + localExtraSize <= central - entry.offset, "The ZIP local header conflicts with its directory.");
                auto localFields = archive.Read(entry.offset + 30, localNameSize + localExtraSize);
                Require(std::equal(fields.begin(), fields.begin() + nameSize, localFields.begin()), "The ZIP has conflicting local and central paths.");
                uint64_t localExpanded = Little(local, 22, 4), localCompressed = Little(local, 18, 4), unusedOffset = 0, unusedDisk = 0;
                Zip64(std::span(localFields).subspan(localNameSize), localExpanded, localCompressed, unusedOffset, unusedDisk);
                if (!(entry.flags & 8))
                    Require(localExpanded == entry.expanded && localCompressed == entry.compressed && Little(local, 14, 4) == entry.crc, "The ZIP has conflicting local sizes or CRC.");
                entry.data = entry.offset + 30 + localNameSize + localExtraSize;
                Require(entry.data <= central && entry.compressed <= central - entry.data &&
                    (entry.method != 0 || entry.compressed == entry.expanded), "The compressed ZIP data exceeds its bounds.");
                entries.push_back(std::move(entry)); cursor += fullSize;
            }
            Require(cursor == directoryEnd && paths.contains(PackageName) && paths.contains("bin/uvsr-engine.exe"), "The renderer archive inventory is incomplete.");
            std::vector<std::pair<uint64_t, uint64_t>> regions;
            for (const auto& entry : entries) regions.emplace_back(entry.offset, entry.data + entry.compressed);
            std::sort(regions.begin(), regions.end());
            for (size_t i = 1; i < regions.size(); ++i) Require(regions[i - 1].second <= regions[i].first, "ZIP members overlap.");
            return entries;
        }
        void Expand(Archive& archive, const Entry& entry, const fs::path& output, std::stop_token stop)
        {
            CreateDirectories(output.parent_path()); RejectReparseChain(output);
            Handle file(CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            WinCheck(file.value != INVALID_HANDLE_VALUE, "Create extracted file");
            struct Inflater
            {
                z_stream stream{}; bool active = false;
                ~Inflater() { if (active) inflateEnd(&stream); }
            } inflater;
            if (entry.method == 8)
            { Require(inflateInit2(&inflater.stream, -MAX_WBITS) == Z_OK, "ZIP decompression initialization failed."); inflater.active = true; }
            std::array<unsigned char, 128 * 1024> input{}, buffer{};
            uint64_t consumed = 0, writtenTotal = 0;
            uLong crc = crc32(0, nullptr, 0); bool ended = false;
            const auto write = [&](std::span<const unsigned char> bytes)
            {
                Require(bytes.size() <= entry.expanded - writtenTotal, "The ZIP member expanded beyond its declared size.");
                DWORD written = 0;
                WinCheck(WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size(), "Write extracted file");
                writtenTotal += bytes.size(); crc = crc32(crc, bytes.data(), uInt(bytes.size()));
            };
            while (consumed < entry.compressed)
            {
                CheckCancelled(stop);
                auto count = size_t(std::min<uint64_t>(input.size(), entry.compressed - consumed));
                archive.Read(entry.data + consumed, std::span(input).first(count)); consumed += count;
                if (entry.method == 0) { write(std::span(input).first(count)); continue; }
                auto& stream = inflater.stream; stream.next_in = input.data(); stream.avail_in = uInt(count);
                do
                {
                    CheckCancelled(stop);
                    stream.next_out = buffer.data(); stream.avail_out = uInt(buffer.size());
                    auto before = stream.avail_in;
                    int result = inflate(&stream, Z_NO_FLUSH);
                    size_t produced = buffer.size() - stream.avail_out;
                    Require(result == Z_OK || result == Z_STREAM_END, "The ZIP deflate stream is invalid.");
                    write(std::span(buffer).first(produced));
                    if (result == Z_STREAM_END)
                    { Require(stream.avail_in == 0 && consumed == entry.compressed, "The ZIP deflate stream has trailing compressed data."); ended = true; break; }
                    Require(produced || stream.avail_in < before, "The ZIP deflate stream made no progress.");
                } while (stream.avail_in || inflater.stream.avail_out == 0);
            }
            Require((entry.method == 0 || ended) && writtenTotal == entry.expanded && uint32_t(crc) == entry.crc,
                "The extracted ZIP member failed its size or CRC check.");
            WinCheck(FlushFileBuffers(file), "Flush extracted file");
        }
    }
    void ExtractPackage(const fs::path& path, const fs::path& root, std::stop_token stop, const Report& report, const Feed* feed)
    {
        RejectReparseChain(path); RejectReparseChain(root);
        Require(!fs::exists(root), "The archive destination already exists. It was preserved.");
        Archive archive(path);
        if (feed) Require(archive.size == feed->size && HashEqual(HashHandle(archive.file), feed->hash), "The held archive does not match its signed feed.");
        const auto entries = Inspect(archive);
        uint64_t expanded = 0; for (const auto& entry : entries) expanded += entry.expanded;
        CreateDirectories(root.parent_path());
        ULARGE_INTEGER free{};
        WinCheck(GetDiskFreeSpaceExW(root.parent_path().c_str(), &free, nullptr, nullptr), "Check archive staging space");
        Require(free.QuadPart >= expanded + (1ull << 30), "The renderer package does not fit safely in staging.");
        CreateDirectories(root);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            CheckCancelled(stop); const auto& entry = entries[i];
            auto output = Descendant(root, entry.name);
            if (entry.directory) CreateDirectories(output); else Expand(archive, entry, output, stop);
            if (report) report({"Installing UVSR Engine", "Verifying and unpacking the signed renderer package.", int((i + 1) * 100 / entries.size())});
        }
    }
}
