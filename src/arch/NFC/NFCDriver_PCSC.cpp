#include "NFCDriver_PCSC.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
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
static const unsigned char kWritePageCommand = 0xA2;
static const unsigned char kPcscEscapeCommand = 0xFF;
static const unsigned char kPn532HostToPn532 = 0xD4;
static const unsigned char kPn532InDataExchange = 0x40;
static const unsigned char kPn532ResponseInDataExchange = 0x41;
static const unsigned char kPn532TargetNumber = 0x01;
static const unsigned char kDataMagic[4] = {'I', 'T', 'G', 'N'};
static const unsigned char kDataVersion = 1;
static const unsigned char kHeaderPage = 4;
static const unsigned char kPayloadStartPage = 6;
static const unsigned char kLastPayloadPage = 129;
static const int kCardPayloadBytes =
    (kLastPayloadPage - kPayloadStartPage + 1) * 4;
static const size_t kLogPreviewBytes = 64;

static std::string HexPreview(const unsigned char* pBytes, size_t nBytes) {
  static const char kHexChars[] = "0123456789ABCDEF";
  if (pBytes == nullptr || nBytes == 0) {
    return "<empty>";
  }

  size_t previewBytes = (nBytes > kLogPreviewBytes) ? kLogPreviewBytes : nBytes;
  std::string out;
  out.reserve(previewBytes * 2 + 24);

  for (size_t i = 0; i < previewBytes; ++i) {
    out.push_back(kHexChars[(pBytes[i] >> 4) & 0xF]);
    out.push_back(kHexChars[pBytes[i] & 0xF]);
  }

  if (previewBytes < nBytes) {
    out += ssprintf("...(%zu bytes total)", nBytes);
  }

  return out;
}

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

  // ACR122-style escape APDU wrapping a PN532 InDataExchange command.
  std::vector<BYTE> vSend(8 + iCommandSize);
  vSend[0] = kPcscEscapeCommand;
  vSend[1] = 0x00;
  vSend[2] = 0x00;
  vSend[3] = 0x00;
  vSend[4] = static_cast<BYTE>(3 + iCommandSize);
  vSend[5] = kPn532HostToPn532;
  vSend[6] = kPn532InDataExchange;
  vSend[7] = kPn532TargetNumber;
  if (iCommandSize > 0) {
    memcpy(&vSend[8], pCommand, iCommandSize);
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

  if (vResponse.size() >= 3 && vResponse[0] == 0xD5 &&
      vResponse[1] == kPn532ResponseInDataExchange) {
    if (vResponse[2] != 0x00) {
      sErrorOut = ssprintf(
          "Reader command failed (0x%02X).", static_cast<int>(vResponse[2]));
      return false;
    }
    vPayloadOut.assign(vResponse.begin() + 3, vResponse.end());
    return true;
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
    sErrorOut = ssprintf(
        "Card read returned an incomplete page block (%zu bytes): %s",
        vDataOut.size(), HexPreview(vDataOut.data(), vDataOut.size()).c_str());
    return false;
  }

  if (vDataOut.size() > 16) {
    vDataOut.resize(16);
  }

  LOG->Info(
      "NFCDriver_PCSC: Read pages starting at %u (%zu bytes): %s",
      static_cast<unsigned int>(iStartPage), vDataOut.size(),
      HexPreview(vDataOut.data(), vDataOut.size()).c_str());

  return true;
}

bool NFCDriver_PCSC::WriteUserPage(
    uintptr_t hCard, unsigned long dwProtocol, unsigned char iPage,
    const unsigned char* pData, std::string& sErrorOut) {
  LOG->Info(
      "NFCDriver_PCSC: Writing page %u: %s", static_cast<unsigned>(iPage),
      HexPreview(pData, 4).c_str());

  const unsigned char cmd[] = {kWritePageCommand, iPage,    pData[0],
                               pData[1],          pData[2], pData[3]};
  std::vector<unsigned char> vResponse;
  if (!TransmitCardCommand(
          hCard, dwProtocol, cmd, sizeof(cmd), vResponse, sErrorOut)) {
    LOG->Warn(
        "NFCDriver_PCSC: Write page %u failed: %s",
        static_cast<unsigned>(iPage), sErrorOut.c_str());
    return false;
  }

  if (!vResponse.empty()) {
    LOG->Trace(
        "NFCDriver_PCSC: write page %u returned %zu data bytes.",
        static_cast<unsigned>(iPage), vResponse.size());
  }
  LOG->Info(
      "NFCDriver_PCSC: Write page %u succeeded.", static_cast<unsigned>(iPage));
  return true;
}

bool NFCDriver_PCSC::ReadCardData(
    std::string& sDataOut, std::string& sErrorOut) {
  sDataOut.clear();

  uintptr_t hCard = 0;
  unsigned long dwProtocol = 0;
  std::string sReaderName;
  if (!ConnectToCard(m_hContext, hCard, dwProtocol, sReaderName, sErrorOut)) {
    LOG->Info(
        "NFCDriver_PCSC: ReadCardData could not connect to a card: %s",
        sErrorOut.c_str());
    return false;
  }

  LOG->Info(
      "NFCDriver_PCSC: Connected to card on reader '%s' with protocol 0x%08lX.",
      sReaderName.c_str(), dwProtocol);

  SCARDHANDLE hCardHandle = static_cast<SCARDHANDLE>(hCard);
  bool bSuccess = false;

  do {
    std::vector<unsigned char> vHeader;
    if (!ReadUserPages(hCard, dwProtocol, kHeaderPage, vHeader, sErrorOut)) {
      LOG->Warn(
          "NFCDriver_PCSC: Failed to read NFC header pages: %s",
          sErrorOut.c_str());
      break;
    }

    LOG->Info(
        "NFCDriver_PCSC: Header bytes: %s",
        HexPreview(vHeader.data(), vHeader.size()).c_str());

    if (memcmp(vHeader.data(), kDataMagic, sizeof(kDataMagic)) != 0) {
      LOG->Info(
          "NFCDriver_PCSC: Card does not contain ITGN magic. Header prefix: %s",
          HexPreview(vHeader.data(), sizeof(kDataMagic)).c_str());
      sErrorOut.clear();
      bSuccess = true;
      break;
    }

    if (vHeader[4] != kDataVersion) {
      sErrorOut = "Unsupported NFC card data format version.";
      LOG->Warn(
          "NFCDriver_PCSC: Unsupported card data version %u (expected %u).",
          static_cast<unsigned int>(vHeader[4]),
          static_cast<unsigned int>(kDataVersion));
      break;
    }

    const int iPayloadSize =
        (static_cast<int>(vHeader[6]) << 8) | static_cast<int>(vHeader[7]);
    if (iPayloadSize < 0 || iPayloadSize > kCardPayloadBytes) {
      sErrorOut = "Stored NFC card data length is invalid.";
      LOG->Warn(
          "NFCDriver_PCSC: Invalid card payload size %d (max %d).",
          iPayloadSize, kCardPayloadBytes);
      break;
    }

    LOG->Info(
        "NFCDriver_PCSC: Card payload length field: %d bytes.", iPayloadSize);

    std::vector<unsigned char> vPayload;
    vPayload.reserve(kCardPayloadBytes);
    for (unsigned char iPage = kPayloadStartPage; iPage <= kLastPayloadPage;
         iPage = static_cast<unsigned char>(iPage + 4)) {
      std::vector<unsigned char> vBlock;
      if (!ReadUserPages(hCard, dwProtocol, iPage, vBlock, sErrorOut)) {
        LOG->Warn(
            "NFCDriver_PCSC: Failed while reading payload page %u: %s",
            static_cast<unsigned int>(iPage), sErrorOut.c_str());
        break;
      }
      vPayload.insert(vPayload.end(), vBlock.begin(), vBlock.end());
    }
    if (!sErrorOut.empty()) {
      break;
    }

    if (static_cast<int>(vPayload.size()) < iPayloadSize) {
      sErrorOut = "Stored NFC card data is truncated.";
      LOG->Warn(
          "NFCDriver_PCSC: Payload truncated, read %zu bytes but header says "
          "%d.",
          vPayload.size(), iPayloadSize);
      break;
    }

    sDataOut.assign(vPayload.begin(), vPayload.begin() + iPayloadSize);
    LOG->Info(
        "NFCDriver_PCSC: ReadCardData success (%d bytes). Payload preview: %s",
        iPayloadSize,
        HexPreview(
            reinterpret_cast<const unsigned char*>(sDataOut.data()),
            sDataOut.size())
            .c_str());
    sErrorOut.clear();
    bSuccess = true;
  } while (false);

  SCardDisconnect(hCardHandle, SCARD_LEAVE_CARD);

  if (!bSuccess && !sErrorOut.empty()) {
    LOG->Warn("NFCDriver_PCSC: ReadCardData failed: %s", sErrorOut.c_str());
  }

  return bSuccess;
}

bool NFCDriver_PCSC::WriteCardData(
    const std::string& sData, std::string& sErrorOut) {
  LOG->Info(
      "NFCDriver_PCSC: WriteCardData requested (%zu bytes).", sData.size());

  if (static_cast<int>(sData.size()) > kCardPayloadBytes) {
    sErrorOut = ssprintf(
        "NFC card data exceeds the %d-byte payload limit.", kCardPayloadBytes);
    LOG->Warn(
        "NFCDriver_PCSC: Refusing write because payload is too large (%zu "
        "bytes, max %d).",
        sData.size(), kCardPayloadBytes);
    return false;
  }

  uintptr_t hCard = 0;
  unsigned long dwProtocol = 0;
  std::string sReaderName;
  if (!ConnectToCard(m_hContext, hCard, dwProtocol, sReaderName, sErrorOut)) {
    LOG->Warn(
        "NFCDriver_PCSC: WriteCardData could not connect to a card: %s",
        sErrorOut.c_str());
    return false;
  }

  LOG->Info(
      "NFCDriver_PCSC: Writing to reader '%s' with protocol 0x%08lX.",
      sReaderName.c_str(), dwProtocol);

  SCARDHANDLE hCardHandle = static_cast<SCARDHANDLE>(hCard);
  bool bSuccess = false;

  do {
    const size_t iPageCount = (sData.size() + 3) / 4;
    LOG->Info("NFCDriver_PCSC: Writing %zu payload pages.", iPageCount);
    for (size_t i = 0; i < iPageCount; ++i) {
      unsigned char pPageData[4] = {0, 0, 0, 0};
      const size_t iOffset = i * 4;
      const size_t iChunkSize = std::min<size_t>(4, sData.size() - iOffset);
      if (iChunkSize > 0) {
        memcpy(pPageData, sData.data() + iOffset, iChunkSize);
      }

      const unsigned char iPage =
          static_cast<unsigned char>(kPayloadStartPage + i);
      LOG->Info(
          "NFCDriver_PCSC: Payload page %u data: %s",
          static_cast<unsigned>(iPage), HexPreview(pPageData, 4).c_str());
      if (!WriteUserPage(hCard, dwProtocol, iPage, pPageData, sErrorOut)) {
        break;
      }
    }
    if (!sErrorOut.empty()) {
      break;
    }

    const unsigned char pHeaderPage4[4] = {
        kDataMagic[0], kDataMagic[1], kDataMagic[2], kDataMagic[3]};
    const unsigned char pHeaderPage5[4] = {
        kDataVersion, 0, static_cast<unsigned char>((sData.size() >> 8) & 0xFF),
        static_cast<unsigned char>(sData.size() & 0xFF)};

    LOG->Info(
        "NFCDriver_PCSC: Writing header page %u: %s",
        static_cast<unsigned>(kHeaderPage),
        HexPreview(pHeaderPage4, sizeof(pHeaderPage4)).c_str());
    LOG->Info(
        "NFCDriver_PCSC: Writing header page %u: %s",
        static_cast<unsigned>(kHeaderPage + 1),
        HexPreview(pHeaderPage5, sizeof(pHeaderPage5)).c_str());

    if (!WriteUserPage(
            hCard, dwProtocol, kHeaderPage, pHeaderPage4, sErrorOut) ||
        !WriteUserPage(
            hCard, dwProtocol, static_cast<unsigned char>(kHeaderPage + 1),
            pHeaderPage5, sErrorOut)) {
      break;
    }

    sErrorOut.clear();
    LOG->Info(
        "NFCDriver_PCSC: WriteCardData succeeded (%zu bytes).", sData.size());
    bSuccess = true;
  } while (false);

  SCardDisconnect(hCardHandle, SCARD_LEAVE_CARD);

  if (!bSuccess && !sErrorOut.empty()) {
    LOG->Warn("NFCDriver_PCSC: WriteCardData failed: %s", sErrorOut.c_str());
  }
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
