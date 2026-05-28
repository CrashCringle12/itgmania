#ifndef NFC_DRIVER_PCSC_H
#define NFC_DRIVER_PCSC_H

#include <string>
#include <vector>

#include "NFCDriver.h"

/**
 * @brief NFC driver using the PC/SC (Smart Card) API.
 *
 * Works with any PC/SC-compatible NFC reader, including the ACR122U.
 * On Linux this requires pcsclite; on Windows it uses the built-in WinSCard
 * API; on macOS it uses the built-in PCSC framework.
 */
class NFCDriver_PCSC : public NFCDriver {
 public:
  NFCDriver_PCSC();
  ~NFCDriver_PCSC() override;

  bool Init() override;
  void Poll() override;
  std::vector<std::string> GetReaderNames() const override;
  std::string GetCurrentCardUID() const override;
  bool IsCardPresent() const override;

 private:
  /** @brief Re-enumerate PC/SC readers. Returns true if the list changed. */
  bool RefreshReaders();

  /** @brief Attempt to read the UID from the first available reader. */
  bool ReadCardUID(std::string& sUIDOut);

  /** @brief Convert a byte buffer to an uppercase hex string. */
  static std::string BytesToHex(const unsigned char* pBytes, size_t nBytes);

  void* m_hContext;  // SCARDCONTEXT (opaque to avoid platform headers here)
  std::vector<std::string> m_vReaderNames;
  std::string m_sCurrentCardUID;
  bool m_bCardPresent;
  bool m_bInitialized;
};

#endif

/*
 * (c) 2024 ITGmania contributors
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
