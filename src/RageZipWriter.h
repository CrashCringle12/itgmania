/* RageZipWriter - Minimal ZIP file writer using zlib DEFLATE. */

#ifndef RAGE_ZIP_WRITER_H
#define RAGE_ZIP_WRITER_H

#include <cstdint>
#include <string>
#include <vector>

#include "RageFile.h"

/**
 * @brief Writes ZIP archives compatible with PKZIP, 7-Zip, Windows Explorer,
 *        and ITGmania's own RageFileDriverZip reader.
 *
 * Uses streaming DEFLATE (or STORE for already-compressed media) and ZIP
 * data descriptors so individual file sizes do not need to be known in
 * advance.  Suitable for producing .smzip packages from the VFS.
 */
class RageZipWriter {
 public:
  RageZipWriter();
  ~RageZipWriter();

  RageZipWriter(const RageZipWriter&) = delete;
  RageZipWriter& operator=(const RageZipWriter&) = delete;

  /** Open the output file for writing.  Returns false on failure. */
  bool Open(const std::string& sOutputPath);

  /** Add a file read from the VFS at sSourcePath, stored in the archive as
   *  sInternalName.  sInternalName uses forward slashes, no leading slash. */
  bool AddFile(
      const std::string& sSourcePath, const std::string& sInternalName);

  /** Add a file whose contents are provided in memory.  Useful for small
   *  generated files such as a Manifest.ini. */
  bool AddFileFromMemory(
      const std::string& sInternalName, const std::string& sContents);

  /** Finalize the archive (writes the central directory) and close the file. */
  bool Close();

  /** Last error message, or empty string on success. */
  const std::string& GetError() const { return m_sError; }

  /** Number of files successfully added. */
  std::size_t GetEntryCount() const { return m_Entries.size(); }

 private:
  struct Entry {
    std::string name;
    std::uint16_t method;
    std::uint16_t dosTime;
    std::uint16_t dosDate;
    std::uint32_t crc32;
    std::uint32_t compressedSize;
    std::uint32_t uncompressedSize;
    std::uint32_t localHeaderOffset;
  };

  bool WriteRaw(const void* data, std::size_t bytes);
  bool WriteLocalHeader(const Entry& e);
  bool WriteDataDescriptor(const Entry& e);
  bool WriteCentralDirectory();
  void SetError(const std::string& s);

  RageFile m_File;
  std::vector<Entry> m_Entries;
  std::uint32_t m_iOffset;
  std::string m_sError;
  bool m_bClosed;
};

#endif

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
