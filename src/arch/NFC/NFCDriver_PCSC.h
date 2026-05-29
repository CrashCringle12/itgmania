#ifndef NFC_DRIVER_PCSC_H
#define NFC_DRIVER_PCSC_H

#include <cstdint>
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
  bool SupportsCardDataIO() const override { return true; }
  bool SupportsCardDataWrite() const override { return true; }
  int GetMaxCardDataBytes() const override;
  bool ReadCardData(std::string& sDataOut, std::string& sErrorOut) override;
  bool WriteCardData(
      const std::string& sDataIn, std::string& sErrorOut) override;

 private:
  /** @brief Re-enumerate PC/SC readers. Returns true if the list changed. */
  bool RefreshReaders();

  /** @brief Attempt to read the UID from the first available reader. */
  bool ReadCardUID(std::string& sUIDOut);

  /** @brief Convert a byte buffer to an uppercase hex string. */
  static std::string BytesToHex(const unsigned char* pBytes, size_t nBytes);

  bool ConnectToCard(
      uintptr_t hContext, uintptr_t& hCardOut, unsigned long& dwProtocolOut,
      std::string& sReaderNameOut, std::string& sErrorOut);
  bool TransmitCardCommand(
      uintptr_t hCard, unsigned long dwProtocol, const unsigned char* pCommand,
      size_t iCommandSize, std::vector<unsigned char>& vPayloadOut,
      std::string& sErrorOut);
  bool ReadUserPages(
      uintptr_t hCard, unsigned long dwProtocol, unsigned char iStartPage,
      std::vector<unsigned char>& vDataOut, std::string& sErrorOut);
  bool WriteUserPage(
      uintptr_t hCard, unsigned long dwProtocol, unsigned char iPage,
      const unsigned char* pData4, std::string& sErrorOut);

  uintptr_t m_hContext;  // SCARDCONTEXT stored without platform headers here
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
