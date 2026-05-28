#include "NFCDriver_PCSC.h"

#include <cstdio>
#include <cstring>

#include "RageLog.h"

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
static const DWORD kGetUIDApduLen =
    static_cast<DWORD>(sizeof(kGetUIDApdu));

// Maximum UID size in bytes (extended UIDs can be 10 bytes).
static const size_t kMaxUIDBytes = 10;

NFCDriver_PCSC::NFCDriver_PCSC()
    : m_hContext(nullptr),
      m_bCardPresent(false),
      m_bInitialized(false) {}

NFCDriver_PCSC::~NFCDriver_PCSC() {
  if (m_hContext != nullptr) {
    SCardReleaseContext(reinterpret_cast<SCARDCONTEXT>(m_hContext));
    m_hContext = nullptr;
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
  m_hContext = reinterpret_cast<void*>(hCtx);
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

  SCARDCONTEXT hCtx = reinterpret_cast<SCARDCONTEXT>(m_hContext);
  DWORD dwReaders = SCARD_AUTOALLOCATE;
  LPSTR pReaders = nullptr;

  LONG rv = SCardListReaders(
      hCtx, nullptr, reinterpret_cast<LPSTR>(&pReaders), &dwReaders);
  if (rv == SCARD_E_NO_READERS_AVAILABLE || rv == SCARD_E_READER_UNAVAILABLE) {
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

  std::vector<std::string> readers;
  LPSTR p = pReaders;
  while (p && *p != '\0') {
    readers.emplace_back(p);
    p += strlen(p) + 1;
  }
  SCardFreeMemory(hCtx, pReaders);

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
  SCARDCONTEXT hCtx = reinterpret_cast<SCARDCONTEXT>(m_hContext);

  for (const auto& readerName : m_vReaderNames) {
    SCARDHANDLE hCard = 0;
    DWORD dwActiveProtocol = 0;

    LONG rv = SCardConnect(
        hCtx,
        readerName.c_str(),
        SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
        &hCard,
        &dwActiveProtocol);

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
        hCard,
        pioSendPCI,
        kGetUIDApdu,
        kGetUIDApduLen,
        nullptr,
        recvBuf,
        &recvLen);

    SCardDisconnect(hCard, SCARD_LEAVE_CARD);

    if (rv != SCARD_S_SUCCESS) {
      LOG->Warn(
          "NFCDriver_PCSC: SCardTransmit failed on '%s' (0x%08lX).",
          readerName.c_str(),
          static_cast<unsigned long>(rv));
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

std::string NFCDriver_PCSC::BytesToHex(const unsigned char* pBytes,
                                        size_t nBytes) {
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

bool NFCDriver_PCSC::IsCardPresent() const {
  return m_bCardPresent;
}

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
