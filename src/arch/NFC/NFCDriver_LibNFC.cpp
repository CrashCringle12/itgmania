#include "NFCDriver_LibNFC.h"

#include <nfc/nfc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "RageLog.h"
#include "RageUtil.h"

namespace {
static const size_t kMaxUIDBytes = 10;
static const unsigned char kReadPageCommand = 0x30;
static const unsigned char kWritePageCommand = 0xA2;
static const unsigned char kDataMagic[4] = {'I', 'T', 'G', 'N'};
static const unsigned char kDataVersion = 1;
static const unsigned char kHeaderPage = 4;
static const unsigned char kPayloadStartPage = 6;
static const unsigned char kLastPayloadPage = 129;
static const int kCardPayloadBytes =
    (kLastPayloadPage - kPayloadStartPage + 1) * 4;
}  // namespace

NFCDriver_LibNFC::NFCDriver_LibNFC()
    : m_pContext(nullptr),
      m_pDevice(nullptr),
      m_bCardPresent(false),
      m_bInitialized(false) {}

NFCDriver_LibNFC::~NFCDriver_LibNFC() {
  if (m_pDevice != nullptr) {
    nfc_close(m_pDevice);
    m_pDevice = nullptr;
  }
  if (m_pContext != nullptr) {
    nfc_exit(m_pContext);
    m_pContext = nullptr;
  }
}

bool NFCDriver_LibNFC::Init() {
  nfc_init(&m_pContext);
  if (m_pContext == nullptr) {
    LOG->Warn("NFCDriver_LibNFC: nfc_init failed.");
    return false;
  }

  std::string sError;
  if (!EnsureOpenDevice(sError)) {
    LOG->Info(
        "NFCDriver_LibNFC: No usable libnfc reader found at startup (%s).",
        sError.c_str());
  }

  m_bInitialized = true;
  return true;
}

void NFCDriver_LibNFC::Poll() {
  if (!m_bInitialized) {
    return;
  }

  RefreshReaders();

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

std::vector<std::string> NFCDriver_LibNFC::GetReaderNames() const {
  return m_vReaderNames;
}

std::string NFCDriver_LibNFC::GetCurrentCardUID() const {
  return m_sCurrentCardUID;
}

bool NFCDriver_LibNFC::IsCardPresent() const { return m_bCardPresent; }

int NFCDriver_LibNFC::GetMaxCardDataBytes() const { return kCardPayloadBytes; }

bool NFCDriver_LibNFC::RefreshReaders() {
  if (m_pContext == nullptr) {
    return false;
  }

  nfc_connstring connstrings[16];
  const size_t maxDevices = sizeof(connstrings) / sizeof(connstrings[0]);
  int iFound = nfc_list_devices(m_pContext, connstrings, maxDevices);
  if (iFound < 0) {
    m_vReaderNames.clear();
    return false;
  }

  m_vReaderNames.clear();
  for (int i = 0; i < iFound; ++i) {
    m_vReaderNames.emplace_back(connstrings[i]);
  }

  if (m_pDevice == nullptr && !m_vReaderNames.empty()) {
    m_pDevice = nfc_open(m_pContext, m_vReaderNames[0].c_str());
    if (m_pDevice != nullptr) {
      if (nfc_initiator_init(m_pDevice) < 0) {
        LOG->Warn(
            "NFCDriver_LibNFC: nfc_initiator_init failed for '%s': %s",
            m_vReaderNames[0].c_str(), nfc_strerror(m_pDevice));
        nfc_close(m_pDevice);
        m_pDevice = nullptr;
      } else {
        nfc_device_set_property_bool(m_pDevice, NP_EASY_FRAMING, true);
        nfc_device_set_property_bool(m_pDevice, NP_AUTO_ISO14443_4, false);
        LOG->Info(
            "NFCDriver_LibNFC: Opened reader '%s'.",
            nfc_device_get_name(m_pDevice));
      }
    }
  }

  if (m_vReaderNames.empty() && m_pDevice != nullptr) {
    nfc_close(m_pDevice);
    m_pDevice = nullptr;
  }

  return true;
}

bool NFCDriver_LibNFC::EnsureOpenDevice(std::string& sErrorOut) {
  if (m_pContext == nullptr) {
    sErrorOut = "libnfc context is unavailable.";
    return false;
  }

  RefreshReaders();
  if (m_pDevice != nullptr) {
    sErrorOut.clear();
    return true;
  }

  if (m_vReaderNames.empty()) {
    sErrorOut = "No libnfc readers found.";
    return false;
  }

  m_pDevice = nfc_open(m_pContext, m_vReaderNames[0].c_str());
  if (m_pDevice == nullptr) {
    sErrorOut =
        ssprintf("Could not open reader '%s'.", m_vReaderNames[0].c_str());
    return false;
  }

  if (nfc_initiator_init(m_pDevice) < 0) {
    sErrorOut = ssprintf(
        "nfc_initiator_init failed for '%s': %s", m_vReaderNames[0].c_str(),
        nfc_strerror(m_pDevice));
    nfc_close(m_pDevice);
    m_pDevice = nullptr;
    return false;
  }

  nfc_device_set_property_bool(m_pDevice, NP_EASY_FRAMING, true);
  nfc_device_set_property_bool(m_pDevice, NP_AUTO_ISO14443_4, false);

  sErrorOut.clear();
  return true;
}

std::string NFCDriver_LibNFC::BytesToHex(
    const unsigned char* pBytes, size_t nBytes) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(nBytes * 2);
  for (size_t i = 0; i < nBytes; ++i) {
    unsigned char c = pBytes[i];
    out.push_back(kHex[(c >> 4) & 0x0F]);
    out.push_back(kHex[c & 0x0F]);
  }
  return out;
}

bool NFCDriver_LibNFC::ReadCardUID(std::string& sUIDOut) {
  sUIDOut.clear();

  std::string sError;
  if (!EnsureOpenDevice(sError)) {
    return false;
  }

  nfc_modulation nm;
  nm.nmt = NMT_ISO14443A;
  nm.nbr = NBR_106;

  nfc_target nt;
  std::memset(&nt, 0, sizeof(nt));
  int rv = nfc_initiator_select_passive_target(m_pDevice, nm, nullptr, 0, &nt);
  if (rv <= 0) {
    return false;
  }

  const size_t uidLen = std::min(
      static_cast<size_t>(nt.nti.nai.szUidLen),
      static_cast<size_t>(kMaxUIDBytes));
  if (uidLen == 0) {
    nfc_initiator_deselect_target(m_pDevice);
    return false;
  }

  sUIDOut = BytesToHex(nt.nti.nai.abtUid, uidLen);
  nfc_initiator_deselect_target(m_pDevice);
  return true;
}

bool NFCDriver_LibNFC::TransmitCardCommand(
    const unsigned char* pCommand, size_t iCommandSize,
    std::vector<unsigned char>& vPayloadOut, std::string& sErrorOut) {
  vPayloadOut.clear();

  std::string sOpenError;
  if (!EnsureOpenDevice(sOpenError)) {
    sErrorOut = sOpenError;
    return false;
  }

  nfc_modulation nm;
  nm.nmt = NMT_ISO14443A;
  nm.nbr = NBR_106;

  nfc_target nt;
  std::memset(&nt, 0, sizeof(nt));
  int rv = nfc_initiator_select_passive_target(m_pDevice, nm, nullptr, 0, &nt);
  if (rv <= 0) {
    sErrorOut = "No NFC card is present on the libnfc reader.";
    return false;
  }

  unsigned char buf[258];
  rv = nfc_initiator_transceive_bytes(
      m_pDevice, pCommand, iCommandSize, buf, sizeof(buf), 0);

  nfc_initiator_deselect_target(m_pDevice);

  if (rv < 0) {
    sErrorOut = ssprintf(
        "nfc_initiator_transceive_bytes failed: %s", nfc_strerror(m_pDevice));
    return false;
  }

  vPayloadOut.assign(buf, buf + rv);
  sErrorOut.clear();
  return true;
}

bool NFCDriver_LibNFC::ReadUserPages(
    unsigned char iStartPage, std::vector<unsigned char>& vDataOut,
    std::string& sErrorOut) {
  const unsigned char cmd[] = {kReadPageCommand, iStartPage};
  if (!TransmitCardCommand(cmd, sizeof(cmd), vDataOut, sErrorOut)) {
    return false;
  }

  if (vDataOut.size() < 16) {
    sErrorOut = ssprintf(
        "Card read returned an incomplete page block (%zu bytes).",
        vDataOut.size());
    return false;
  }

  if (vDataOut.size() > 16) {
    vDataOut.resize(16);
  }
  return true;
}

bool NFCDriver_LibNFC::WriteUserPage(
    unsigned char iPage, const unsigned char* pData, std::string& sErrorOut) {
  const unsigned char cmd[] = {kWritePageCommand, iPage,    pData[0],
                               pData[1],          pData[2], pData[3]};
  std::vector<unsigned char> vResponse;
  if (!TransmitCardCommand(cmd, sizeof(cmd), vResponse, sErrorOut)) {
    return false;
  }

  if (vResponse.empty() || vResponse[0] != 0x0A) {
    sErrorOut = "Card write did not return a MIFARE ACK.";
    return false;
  }
  return true;
}

bool NFCDriver_LibNFC::ReadCardData(
    std::string& sDataOut, std::string& sErrorOut) {
  sDataOut.clear();

  std::vector<unsigned char> vHeader;
  if (!ReadUserPages(kHeaderPage, vHeader, sErrorOut)) {
    return false;
  }

  if (std::memcmp(vHeader.data(), kDataMagic, sizeof(kDataMagic)) != 0) {
    // Treat a non-ITGN tag as a successful read of empty data.
    sErrorOut.clear();
    return true;
  }

  if (vHeader[4] != kDataVersion) {
    sErrorOut = "Unsupported NFC card data format version.";
    return false;
  }

  const int iPayloadSize =
      (static_cast<int>(vHeader[6]) << 8) | static_cast<int>(vHeader[7]);
  if (iPayloadSize < 0 || iPayloadSize > kCardPayloadBytes) {
    sErrorOut = "Stored NFC card data length is invalid.";
    return false;
  }

  std::vector<unsigned char> vPayload;
  vPayload.reserve(kCardPayloadBytes);
  for (unsigned char iPage = kPayloadStartPage; iPage <= kLastPayloadPage;
       iPage += 4) {
    std::vector<unsigned char> vPageBlock;
    if (!ReadUserPages(iPage, vPageBlock, sErrorOut)) {
      return false;
    }
    vPayload.insert(vPayload.end(), vPageBlock.begin(), vPageBlock.end());
  }

  if (static_cast<int>(vPayload.size()) < iPayloadSize) {
    sErrorOut = "Stored NFC card data is truncated.";
    return false;
  }

  sDataOut.assign(
      reinterpret_cast<const char*>(vPayload.data()),
      reinterpret_cast<const char*>(vPayload.data()) + iPayloadSize);
  sErrorOut.clear();
  return true;
}

bool NFCDriver_LibNFC::WriteCardData(
    const std::string& sData, std::string& sErrorOut) {
  if (sData.size() > static_cast<size_t>(kCardPayloadBytes)) {
    sErrorOut = ssprintf(
        "Card data is too large (%zu > %d bytes).", sData.size(),
        kCardPayloadBytes);
    return false;
  }

  std::vector<unsigned char> vPayload(kCardPayloadBytes, 0);
  if (!sData.empty()) {
    std::memcpy(vPayload.data(), sData.data(), sData.size());
  }

  for (unsigned char iPage = kPayloadStartPage; iPage <= kLastPayloadPage;
       ++iPage) {
    const size_t iOffset = static_cast<size_t>(iPage - kPayloadStartPage) * 4;
    if (!WriteUserPage(iPage, &vPayload[iOffset], sErrorOut)) {
      return false;
    }
  }

  unsigned char vHeaderPage4[4];
  std::memcpy(vHeaderPage4, kDataMagic, sizeof(kDataMagic));
  if (!WriteUserPage(kHeaderPage, vHeaderPage4, sErrorOut)) {
    return false;
  }

  const unsigned short iSize = static_cast<unsigned short>(sData.size());
  unsigned char vHeaderPage5[4] = {
      kDataVersion, 0, static_cast<unsigned char>((iSize >> 8) & 0xFF),
      static_cast<unsigned char>(iSize & 0xFF)};
  if (!WriteUserPage(kHeaderPage + 1, vHeaderPage5, sErrorOut)) {
    return false;
  }

  sErrorOut.clear();
  return true;
}
