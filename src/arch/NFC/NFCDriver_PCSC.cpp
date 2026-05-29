#include "NFCDriver_PCSC.h"

#include <cstdint>
#include <cstring>

#include "RageLog.h"
#include "RageUtil.h"

// Platform-specific PC/SC headers.
#if defined(WIN32)
#include <winscard.h>
#elif defined(__APPLE__)
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
// Linux / BSD via pcsclite (headers are in the PCSC/ subdirectory)
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#endif

// GET DATA APDU for reading the card UID (ISO 14443 / NFC).
// Works on ACR122U and most other PC/SC NFC readers.
static const BYTE kGetUIDApdu[] = {0xFF, 0xCA, 0x00, 0x00, 0x00};
static const DWORD kGetUIDApduLen = static_cast<DWORD>(sizeof(kGetUIDApdu));

// Maximum UID size in bytes (extended UIDs can be 10 bytes).
static const size_t kMaxUIDBytes = 10;
static const unsigned char kReadPageCommand = 0x30;
static const unsigned char kDataMagic[4] = {'I', 'T', 'G', 'N'};
static const unsigned char kDataVersion = 1;
static const unsigned char kHeaderPage = 4;
static const unsigned char kPayloadStartPage = 6;
static const unsigned char kLastPayloadPage = 129;
static const int kCardPayloadBytes =
    (kLastPayloadPage - kPayloadStartPage + 1) * 4;

NFCDriver_PCSC::NFCDriver_PCSC()
    : m_hContext(0), m_bCardPresent(false), m_bInitialized(false) {}

NFCDriver_PCSC::~NFCDriver_PCSC() {
  if (m_hContext != 0) {
    SCardReleaseContext(static_cast<SCARDCONTEXT>(m_hContext));
    m_hContext = 0;
  }
}

bool NFCDriver_PCSC::Init() {
  SCARDCONTEXT hCtx = 0;
  LONG rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &hCtx);
  if (rv != SCARD_S_SUCCESS) {
    LOG->Warn(
        "NFCDriver_PCSC: SCardEstablishContext failed (0x%08lX). "
        "NFC card support will be unavailable.",
        static_cast<unsigned long>(rv));
    return false;
  }
  m_hContext = static_cast<uintptr_t>(hCtx);
  m_bInitialized = true;

  RefreshReaders();
  if (m_vReaderNames.empty()) {
    LOG->Info("NFCDriver_PCSC: No PC/SC readers found at startup.");
  } else {
    for (const auto& name : m_vReaderNames) {
      LOG->Info("NFCDriver_PCSC: Found reader: %s", name.c_str());
    }
  }
  return true;
}

bool NFCDriver_PCSC::RefreshReaders() {
  if (!m_bInitialized) {
    return false;
  }

  SCARDCONTEXT hCtx = static_cast<SCARDCONTEXT>(m_hContext);
  DWORD dwReaders = 0;

  LONG rv = SCardListReaders(hCtx, nullptr, nullptr, &dwReaders);
  if (rv == static_cast<LONG>(SCARD_E_NO_READERS_AVAILABLE) ||
      rv == static_cast<LONG>(SCARD_E_READER_UNAVAILABLE)) {
    std::vector<std::string> empty;
    bool changed = (m_vReaderNames != empty);
    m_vReaderNames.clear();
    return changed;
  }
  if (rv != SCARD_S_SUCCESS) {
    LOG->Warn(
        "NFCDriver_PCSC: SCardListReaders failed (0x%08lX).",
        static_cast<unsigned long>(rv));
    return false;
  }

  std::vector<char> readersBuffer(dwReaders);
  LPSTR pReaders = readersBuffer.data();
  rv = SCardListReaders(hCtx, nullptr, pReaders, &dwReaders);
  if (rv != SCARD_S_SUCCESS) {
    LOG->Warn(
        "NFCDriver_PCSC: SCardListReaders (data) failed (0x%08lX).",
        static_cast<unsigned long>(rv));
    return false;
  }

  std::vector<std::string> readers;
  LPSTR p = pReaders;
  while (p && *p != '\0') {
    readers.emplace_back(p);
    p += strlen(p) + 1;
  }

  bool changed = (readers != m_vReaderNames);
  m_vReaderNames = std::move(readers);
  return changed;
}

void NFCDriver_PCSC::Poll() {
  if (!m_bInitialized) {
    return;
  }

  // Re-enumerate readers occasionally to pick up hot-plug events.
  RefreshReaders();

  if (m_vReaderNames.empty()) {
    if (m_bCardPresent) {
      m_bCardPresent = false;
      m_sCurrentCardUID.clear();
    }
    return;
  }

  std::string uid;
  bool cardFound = ReadCardUID(uid);

  if (cardFound && !m_bCardPresent) {
    m_bCardPresent = true;
    m_sCurrentCardUID = uid;
  } else if (!cardFound && m_bCardPresent) {
    m_bCardPresent = false;
    m_sCurrentCardUID.clear();
  }
}

bool NFCDriver_PCSC::ReadCardUID(std::string& sUIDOut) {
  SCARDCONTEXT hCtx = static_cast<SCARDCONTEXT>(m_hContext);

  for (const auto& readerName : m_vReaderNames) {
    SCARDHANDLE hCard = 0;
    DWORD dwActiveProtocol = 0;

    LONG rv = SCardConnect(
        hCtx, readerName.c_str(), SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &hCard, &dwActiveProtocol);

    if (rv != SCARD_S_SUCCESS) {
      // No card in this reader – try the next one.
      continue;
    }

    // Select the appropriate PCI structure for the active protocol.
    const SCARD_IO_REQUEST* pioSendPCI =
        (dwActiveProtocol == SCARD_PROTOCOL_T0) ? SCARD_PCI_T0 : SCARD_PCI_T1;

    BYTE recvBuf[kMaxUIDBytes + 2];  // UID bytes + 2 status bytes (SW1 SW2)
    DWORD recvLen = sizeof(recvBuf);
    memset(recvBuf, 0, sizeof(recvBuf));

    rv = SCardTransmit(
        hCard, pioSendPCI, kGetUIDApdu, kGetUIDApduLen, nullptr, recvBuf,
        &recvLen);

    SCardDisconnect(hCard, SCARD_LEAVE_CARD);

    if (rv != SCARD_S_SUCCESS) {
      LOG->Warn(
          "NFCDriver_PCSC: SCardTransmit failed on '%s' (0x%08lX).",
          readerName.c_str(), static_cast<unsigned long>(rv));
      continue;
    }

    // Response is: UID bytes ... SW1 SW2
    // SW1==0x90, SW2==0x00 indicates success.
    if (recvLen < 2) {
      continue;
    }
    BYTE sw1 = recvBuf[recvLen - 2];
    BYTE sw2 = recvBuf[recvLen - 1];
    if (sw1 != 0x90 || sw2 != 0x00) {
      continue;
    }

    size_t uidLen = recvLen - 2;
    if (uidLen == 0 || uidLen > kMaxUIDBytes) {
      continue;
    }

    sUIDOut = BytesToHex(recvBuf, uidLen);
    return true;
  }

  return false;
}

int NFCDriver_PCSC::GetMaxCardDataBytes() const { return kCardPayloadBytes; }

bool NFCDriver_PCSC::ConnectToCard(
    uintptr_t hContext, uintptr_t& hCardOut, unsigned long& dwProtocolOut,
    std::string& sReaderNameOut, std::string& sErrorOut) {
  hCardOut = 0;
  dwProtocolOut = 0;
  sReaderNameOut.clear();

  if (!m_bInitialized) {
    sErrorOut = "NFC driver is not initialized.";
    return false;
  }

  if (m_vReaderNames.empty()) {
    RefreshReaders();
  }

  SCARDCONTEXT hCtx = static_cast<SCARDCONTEXT>(hContext);
  LONG rv = SCARD_E_UNKNOWN_READER;

  for (const auto& readerName : m_vReaderNames) {
    SCARDHANDLE hCard = 0;
    DWORD dwActiveProtocol = 0;
    rv = SCardConnect(
        hCtx, readerName.c_str(), SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &hCard, &dwActiveProtocol);
    if (rv != SCARD_S_SUCCESS) {
      continue;
    }

    hCardOut = static_cast<uintptr_t>(hCard);
    dwProtocolOut = static_cast<unsigned long>(dwActiveProtocol);
    sReaderNameOut = readerName;
    sErrorOut.clear();
    return true;
  }

  sErrorOut = ssprintf(
      "No NFC card is available for data access (0x%08lX).",
      static_cast<unsigned long>(rv));
  return false;
}

bool NFCDriver_PCSC::TransmitCardCommand(
    uintptr_t hCard, unsigned long dwProtocol, const unsigned char* pCommand,
    size_t iCommandSize, std::vector<unsigned char>& vPayloadOut,
    std::string& sErrorOut) {
  vPayloadOut.clear();

  SCARDHANDLE hCardHandle = static_cast<SCARDHANDLE>(hCard);
  const SCARD_IO_REQUEST* pioSendPCI =
      (dwProtocol == static_cast<unsigned long>(SCARD_PROTOCOL_T0))
          ? SCARD_PCI_T0
          : SCARD_PCI_T1;

  std::vector<BYTE> vSend(5 + iCommandSize);
  vSend[0] = 0xFF;
  vSend[1] = 0x00;
  vSend[2] = 0x00;
  vSend[3] = 0x00;
  vSend[4] = static_cast<BYTE>(iCommandSize);
  if (iCommandSize > 0) {
    memcpy(&vSend[5], pCommand, iCommandSize);
  }

  BYTE recvBuf[258];
  DWORD recvLen = sizeof(recvBuf);
  LONG rv = SCardTransmit(
      hCardHandle, pioSendPCI, vSend.data(), static_cast<DWORD>(vSend.size()),
      nullptr, recvBuf, &recvLen);
  if (rv != SCARD_S_SUCCESS) {
    sErrorOut = ssprintf(
        "SCardTransmit failed (0x%08lX).", static_cast<unsigned long>(rv));
    return false;
  }

  std::vector<unsigned char> vResponse(recvBuf, recvBuf + recvLen);
  if (vResponse.size() >= 2 && vResponse[vResponse.size() - 2] == 0x90 &&
      vResponse[vResponse.size() - 1] == 0x00) {
    vResponse.resize(vResponse.size() - 2);
  }

  if (vResponse.size() >= 3 && vResponse[0] == 0xD5 && vResponse[1] == 0x43) {
    if (vResponse[2] != 0x00) {
      sErrorOut = ssprintf(
          "Reader command failed (0x%02X).", static_cast<int>(vResponse[2]));
      return false;
    }
    vPayloadOut.assign(vResponse.begin() + 3, vResponse.end());
    return true;
  }

  vPayloadOut = std::move(vResponse);
  return true;
}

bool NFCDriver_PCSC::ReadUserPages(
    uintptr_t hCard, unsigned long dwProtocol, unsigned char iStartPage,
    std::vector<unsigned char>& vDataOut, std::string& sErrorOut) {
  const unsigned char cmd[] = {kReadPageCommand, iStartPage};
  if (!TransmitCardCommand(
          hCard, dwProtocol, cmd, sizeof(cmd), vDataOut, sErrorOut)) {
    return false;
  }

  if (vDataOut.size() < 16) {
    sErrorOut = "Card read returned an incomplete page block.";
    return false;
  }

  if (vDataOut.size() > 16) {
    vDataOut.resize(16);
  }
  return true;
}

bool NFCDriver_PCSC::ReadCardData(
    std::string& sDataOut, std::string& sErrorOut) {
  sDataOut.clear();

  uintptr_t hCard = 0;
  unsigned long dwProtocol = 0;
  std::string sReaderName;
  if (!ConnectToCard(m_hContext, hCard, dwProtocol, sReaderName, sErrorOut)) {
    return false;
  }
  (void)sReaderName;

  SCARDHANDLE hCardHandle = static_cast<SCARDHANDLE>(hCard);
  bool bSuccess = false;

  do {
    std::vector<unsigned char> vHeader;
    if (!ReadUserPages(hCard, dwProtocol, kHeaderPage, vHeader, sErrorOut)) {
      break;
    }

    if (memcmp(vHeader.data(), kDataMagic, sizeof(kDataMagic)) != 0) {
      sErrorOut.clear();
      bSuccess = true;
      break;
    }

    if (vHeader[4] != kDataVersion) {
      sErrorOut = "Unsupported NFC card data format version.";
      break;
    }

    const int iPayloadSize =
        (static_cast<int>(vHeader[6]) << 8) | static_cast<int>(vHeader[7]);
    if (iPayloadSize < 0 || iPayloadSize > kCardPayloadBytes) {
      sErrorOut = "Stored NFC card data length is invalid.";
      break;
    }

    std::vector<unsigned char> vPayload;
    vPayload.reserve(kCardPayloadBytes);
    for (unsigned char iPage = kPayloadStartPage; iPage <= kLastPayloadPage;
         iPage = static_cast<unsigned char>(iPage + 4)) {
      std::vector<unsigned char> vBlock;
      if (!ReadUserPages(hCard, dwProtocol, iPage, vBlock, sErrorOut)) {
        break;
      }
      vPayload.insert(vPayload.end(), vBlock.begin(), vBlock.end());
    }
    if (!sErrorOut.empty()) {
      break;
    }

    if (static_cast<int>(vPayload.size()) < iPayloadSize) {
      sErrorOut = "Stored NFC card data is truncated.";
      break;
    }

    sDataOut.assign(vPayload.begin(), vPayload.begin() + iPayloadSize);
    sErrorOut.clear();
    bSuccess = true;
  } while (false);

  SCardDisconnect(hCardHandle, SCARD_LEAVE_CARD);
  return bSuccess;
}

std::string NFCDriver_PCSC::BytesToHex(
    const unsigned char* pBytes, size_t nBytes) {
  std::string result;
  result.reserve(nBytes * 2);
  static const char kHexChars[] = "0123456789ABCDEF";
  for (size_t i = 0; i < nBytes; ++i) {
    result.push_back(kHexChars[(pBytes[i] >> 4) & 0xF]);
    result.push_back(kHexChars[pBytes[i] & 0xF]);
  }
  return result;
}

std::vector<std::string> NFCDriver_PCSC::GetReaderNames() const {
  return m_vReaderNames;
}

std::string NFCDriver_PCSC::GetCurrentCardUID() const {
  return m_sCurrentCardUID;
}

bool NFCDriver_PCSC::IsCardPresent() const { return m_bCardPresent; }

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
