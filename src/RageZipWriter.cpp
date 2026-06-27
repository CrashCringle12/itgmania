#include "RageZipWriter.h"

#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "RageUtil.h"
#include "StdString.h"
#include "global.h"

#if defined(_WIN32) || defined(MACOSX)
#include "zlib.h"
#else
#include <zlib.h>
#endif

namespace {

constexpr std::uint16_t kMethodStore = 0;
constexpr std::uint16_t kMethodDeflate = 8;

// General purpose bit flags.
//   bit 3 (0x0008): CRC32 and sizes are zero in the local header and instead
//                   appear in a data descriptor after the file data.
//   bit 11 (0x0800): file name is encoded as UTF-8.
constexpr std::uint16_t kGpFlagDataDescriptor = 0x0008;
constexpr std::uint16_t kGpFlagUtf8 = 0x0800;

// File extensions whose contents are already compressed; DEFLATE on these
// just burns CPU and slightly inflates the output.
bool IsAlreadyCompressed(const std::string& sName) {
  std::string ext = GetExtension(sName);
  if (ext.empty()) {
    return false;
  }
  MakeLower(ext);
  static const char* const kCompressed[] = {
      "mp3",  "ogg", "oga", "opus", "m4a", "aac",  "flac", "wma",
      "jpg",  "jpeg","png", "gif",  "webp","avif", "heic",
      "mp4",  "m4v", "mov", "mkv",  "webm","avi",  "mpg",  "mpeg", "wmv",
      "zip",  "7z",  "rar", "gz",   "xz",  "bz2",  "zst",  "smzip"};
  for (const char* p : kCompressed) {
    if (ext == p) {
      return true;
    }
  }
  return false;
}

void DosTimeFromTm(
    const std::tm& t, std::uint16_t& dosTime, std::uint16_t& dosDate) {
  // ZIP/DOS date: bits 0-4 day (1-31), bits 5-8 month (1-12), bits 9-15 year-1980.
  // ZIP/DOS time: bits 0-4 sec/2 (0-29), bits 5-10 min (0-59), bits 11-15 hr (0-23).
  int year = t.tm_year + 1900;
  if (year < 1980) {
    year = 1980;
  }
  dosDate = static_cast<std::uint16_t>(
      ((year - 1980) << 9) | ((t.tm_mon + 1) << 5) | (t.tm_mday & 0x1F));
  dosTime = static_cast<std::uint16_t>(
      (t.tm_hour << 11) | (t.tm_min << 5) | ((t.tm_sec / 2) & 0x1F));
}

void GetDosNow(std::uint16_t& dosTime, std::uint16_t& dosDate) {
  std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  std::tm* lt = std::localtime(&now);
  if (lt != nullptr) {
    local = *lt;
  }
#endif
  DosTimeFromTm(local, dosTime, dosDate);
}

void Put16LE(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void Put32LE(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

std::string NormalizeInternalName(std::string name) {
  for (char& c : name) {
    if (c == '\\') {
      c = '/';
    }
  }
  while (!name.empty() && name.front() == '/') {
    name.erase(name.begin());
  }
  return name;
}

}  // namespace

RageZipWriter::RageZipWriter() : m_iOffset(0), m_bClosed(false) {}

RageZipWriter::~RageZipWriter() {
  if (m_File.IsOpen() && !m_bClosed) {
    Close();
  }
}

void RageZipWriter::SetError(const std::string& s) {
  if (m_sError.empty()) {
    m_sError = s;
  }
}

bool RageZipWriter::Open(const std::string& sOutputPath) {
  if (!m_File.Open(sOutputPath, RageFile::WRITE)) {
    SetError(ssprintf(
        "Could not open '%s' for writing: %s", sOutputPath.c_str(),
        m_File.GetError().c_str()));
    return false;
  }
  m_iOffset = 0;
  m_Entries.clear();
  m_sError.clear();
  m_bClosed = false;
  return true;
}

bool RageZipWriter::WriteRaw(const void* data, std::size_t bytes) {
  if (bytes == 0) {
    return true;
  }
  int written = m_File.Write(data, bytes);
  if (written < 0 || static_cast<std::size_t>(written) != bytes) {
    SetError(ssprintf("Write failed: %s", m_File.GetError().c_str()));
    return false;
  }
  m_iOffset += static_cast<std::uint32_t>(bytes);
  return true;
}

bool RageZipWriter::WriteLocalHeader(const Entry& e) {
  std::vector<std::uint8_t> hdr;
  hdr.reserve(30 + e.name.size());
  Put32LE(hdr, 0x04034b50u);  // Local file header signature
  Put16LE(hdr, 20);           // Version needed to extract (2.0)
  Put16LE(hdr, kGpFlagDataDescriptor | kGpFlagUtf8);
  Put16LE(hdr, e.method);
  Put16LE(hdr, e.dosTime);
  Put16LE(hdr, e.dosDate);
  Put32LE(hdr, 0);  // CRC32 (data descriptor)
  Put32LE(hdr, 0);  // Compressed size (data descriptor)
  Put32LE(hdr, 0);  // Uncompressed size (data descriptor)
  Put16LE(hdr, static_cast<std::uint16_t>(e.name.size()));
  Put16LE(hdr, 0);  // Extra length
  hdr.insert(hdr.end(), e.name.begin(), e.name.end());
  return WriteRaw(hdr.data(), hdr.size());
}

bool RageZipWriter::WriteDataDescriptor(const Entry& e) {
  std::vector<std::uint8_t> dd;
  dd.reserve(16);
  Put32LE(dd, 0x08074b50u);  // Optional but widely expected signature
  Put32LE(dd, e.crc32);
  Put32LE(dd, e.compressedSize);
  Put32LE(dd, e.uncompressedSize);
  return WriteRaw(dd.data(), dd.size());
}

bool RageZipWriter::AddFile(
    const std::string& sSourcePath, const std::string& sInternalName) {
  if (!m_File.IsOpen() || m_bClosed) {
    SetError("RageZipWriter::AddFile called on a closed writer");
    return false;
  }

  std::string name = NormalizeInternalName(sInternalName);
  if (name.empty()) {
    SetError("Empty internal name");
    return false;
  }

  RageFile in;
  if (!in.Open(sSourcePath, RageFile::READ)) {
    SetError(ssprintf(
        "Could not open '%s' for reading: %s", sSourcePath.c_str(),
        in.GetError().c_str()));
    return false;
  }

  Entry entry;
  entry.name = name;
  entry.method = IsAlreadyCompressed(name) ? kMethodStore : kMethodDeflate;
  entry.crc32 = 0;
  entry.compressedSize = 0;
  entry.uncompressedSize = 0;
  entry.localHeaderOffset = m_iOffset;
  GetDosNow(entry.dosTime, entry.dosDate);

  if (!WriteLocalHeader(entry)) {
    return false;
  }

  constexpr std::size_t kBufSize = 64 * 1024;
  std::vector<char> inBuf(kBufSize);
  std::vector<char> outBuf(kBufSize);
  uLong crc = crc32(0L, Z_NULL, 0);

  if (entry.method == kMethodStore) {
    for (;;) {
      int got = in.Read(inBuf.data(), inBuf.size());
      if (got < 0) {
        SetError(ssprintf(
            "Read error on '%s': %s", sSourcePath.c_str(),
            in.GetError().c_str()));
        return false;
      }
      if (got == 0) {
        break;
      }
      crc = crc32(
          crc, reinterpret_cast<const Bytef*>(inBuf.data()),
          static_cast<uInt>(got));
      if (!WriteRaw(inBuf.data(), got)) {
        return false;
      }
      entry.uncompressedSize += static_cast<std::uint32_t>(got);
      entry.compressedSize += static_cast<std::uint32_t>(got);
    }
  } else {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    int err = deflateInit2(
        &zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    if (err != Z_OK) {
      SetError(ssprintf("deflateInit2 failed: %d", err));
      return false;
    }

    bool inputDone = false;
    while (!inputDone) {
      int got = in.Read(inBuf.data(), inBuf.size());
      if (got < 0) {
        deflateEnd(&zs);
        SetError(ssprintf(
            "Read error on '%s': %s", sSourcePath.c_str(),
            in.GetError().c_str()));
        return false;
      }
      if (got == 0) {
        inputDone = true;
      } else {
        crc = crc32(
            crc, reinterpret_cast<const Bytef*>(inBuf.data()),
            static_cast<uInt>(got));
        entry.uncompressedSize += static_cast<std::uint32_t>(got);
      }

      zs.next_in = reinterpret_cast<Bytef*>(inBuf.data());
      zs.avail_in = static_cast<uInt>(got);
      int flush = inputDone ? Z_FINISH : Z_NO_FLUSH;

      do {
        zs.next_out = reinterpret_cast<Bytef*>(outBuf.data());
        zs.avail_out = static_cast<uInt>(outBuf.size());
        err = deflate(&zs, flush);
        if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR) {
          deflateEnd(&zs);
          SetError(ssprintf("deflate failed: %d", err));
          return false;
        }
        std::size_t produced = outBuf.size() - zs.avail_out;
        if (produced > 0) {
          if (!WriteRaw(outBuf.data(), produced)) {
            deflateEnd(&zs);
            return false;
          }
          entry.compressedSize += static_cast<std::uint32_t>(produced);
        }
        if (err == Z_STREAM_END) {
          break;
        }
      } while (zs.avail_in > 0 || (inputDone && err != Z_STREAM_END));
    }
    deflateEnd(&zs);
  }

  entry.crc32 = static_cast<std::uint32_t>(crc);
  if (!WriteDataDescriptor(entry)) {
    return false;
  }

  m_Entries.push_back(std::move(entry));
  return true;
}

bool RageZipWriter::AddFileFromMemory(
    const std::string& sInternalName, const std::string& sContents) {
  if (!m_File.IsOpen() || m_bClosed) {
    SetError("RageZipWriter::AddFileFromMemory called on a closed writer");
    return false;
  }

  std::string name = NormalizeInternalName(sInternalName);
  if (name.empty()) {
    SetError("Empty internal name");
    return false;
  }

  Entry entry;
  entry.name = name;
  entry.method = IsAlreadyCompressed(name) ? kMethodStore : kMethodDeflate;
  entry.crc32 = 0;
  entry.compressedSize = 0;
  entry.uncompressedSize = 0;
  entry.localHeaderOffset = m_iOffset;
  GetDosNow(entry.dosTime, entry.dosDate);

  if (!WriteLocalHeader(entry)) {
    return false;
  }

  uLong crc = crc32(0L, Z_NULL, 0);
  const Bytef* src = reinterpret_cast<const Bytef*>(sContents.data());
  const uInt srcSize = static_cast<uInt>(sContents.size());

  if (entry.method == kMethodStore) {
    if (srcSize > 0) {
      crc = crc32(crc, src, srcSize);
      if (!WriteRaw(src, srcSize)) {
        return false;
      }
    }
    entry.uncompressedSize = srcSize;
    entry.compressedSize = srcSize;
  } else {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    int err = deflateInit2(
        &zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    if (err != Z_OK) {
      SetError(ssprintf("deflateInit2 failed: %d", err));
      return false;
    }
    if (srcSize > 0) {
      crc = crc32(crc, src, srcSize);
    }
    entry.uncompressedSize = srcSize;

    constexpr std::size_t kBufSize = 64 * 1024;
    std::vector<char> outBuf(kBufSize);
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = srcSize;
    int flush = Z_FINISH;
    do {
      zs.next_out = reinterpret_cast<Bytef*>(outBuf.data());
      zs.avail_out = static_cast<uInt>(outBuf.size());
      err = deflate(&zs, flush);
      if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR) {
        deflateEnd(&zs);
        SetError(ssprintf("deflate failed: %d", err));
        return false;
      }
      std::size_t produced = outBuf.size() - zs.avail_out;
      if (produced > 0) {
        if (!WriteRaw(outBuf.data(), produced)) {
          deflateEnd(&zs);
          return false;
        }
        entry.compressedSize += static_cast<std::uint32_t>(produced);
      }
    } while (err != Z_STREAM_END);
    deflateEnd(&zs);
  }

  entry.crc32 = static_cast<std::uint32_t>(crc);
  if (!WriteDataDescriptor(entry)) {
    return false;
  }

  m_Entries.push_back(std::move(entry));
  return true;
}

bool RageZipWriter::WriteCentralDirectory() {
  std::uint32_t cdOffset = m_iOffset;

  for (const Entry& e : m_Entries) {
    std::vector<std::uint8_t> rec;
    rec.reserve(46 + e.name.size());
    Put32LE(rec, 0x02014b50u);  // Central directory file header signature
    Put16LE(rec, (0 << 8) | 20);  // Version made by: 2.0, OS=DOS/FAT
    Put16LE(rec, 20);             // Version needed to extract
    Put16LE(rec, kGpFlagDataDescriptor | kGpFlagUtf8);
    Put16LE(rec, e.method);
    Put16LE(rec, e.dosTime);
    Put16LE(rec, e.dosDate);
    Put32LE(rec, e.crc32);
    Put32LE(rec, e.compressedSize);
    Put32LE(rec, e.uncompressedSize);
    Put16LE(rec, static_cast<std::uint16_t>(e.name.size()));
    Put16LE(rec, 0);  // Extra length
    Put16LE(rec, 0);  // Comment length
    Put16LE(rec, 0);  // Disk number start
    Put16LE(rec, 0);  // Internal attrs
    Put32LE(rec, 0);  // External attrs (regular file)
    Put32LE(rec, e.localHeaderOffset);
    rec.insert(rec.end(), e.name.begin(), e.name.end());
    if (!WriteRaw(rec.data(), rec.size())) {
      return false;
    }
  }

  std::uint32_t cdSize = m_iOffset - cdOffset;

  std::vector<std::uint8_t> eocd;
  eocd.reserve(22);
  Put32LE(eocd, 0x06054b50u);  // End of central dir signature
  Put16LE(eocd, 0);            // Number of this disk
  Put16LE(eocd, 0);            // Disk with start of central directory
  Put16LE(eocd, static_cast<std::uint16_t>(m_Entries.size()));
  Put16LE(eocd, static_cast<std::uint16_t>(m_Entries.size()));
  Put32LE(eocd, cdSize);
  Put32LE(eocd, cdOffset);
  Put16LE(eocd, 0);  // Comment length
  return WriteRaw(eocd.data(), eocd.size());
}

bool RageZipWriter::Close() {
  if (m_bClosed) {
    return m_sError.empty();
  }
  m_bClosed = true;

  bool ok = true;
  if (m_File.IsOpen()) {
    ok = WriteCentralDirectory();
    if (m_File.Flush() < 0) {
      SetError(ssprintf("Flush failed: %s", m_File.GetError().c_str()));
      ok = false;
    }
    m_File.Close();
  }
  return ok && m_sError.empty();
}

/*
 * (c) 2026 ITGmania
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
