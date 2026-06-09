#ifndef NFC_DRIVER_LIBNFC_H
#define NFC_DRIVER_LIBNFC_H

#include <string>
#include <vector>

#include "NFCDriver.h"

struct nfc_context;
struct nfc_device;

/**
 * @brief NFC driver using libnfc (PN53x direct mode).
 *
 * This backend is primarily intended for Linux/BSD systems where PN53x
 * readers are available through libnfc but not exposed through PC/SC.
 */
class NFCDriver_LibNFC : public NFCDriver {
 public:
  NFCDriver_LibNFC();
  ~NFCDriver_LibNFC() override;

  bool Init() override;
  void Poll() override;
  std::vector<std::string> GetReaderNames() const override;
  std::string GetCurrentCardUID() const override;
  bool IsCardPresent() const override;
  bool SupportsCardDataIO() const override { return true; }
  int GetMaxCardDataBytes() const override;
  bool ReadCardData(std::string& sDataOut, std::string& sErrorOut) override;
  bool WriteCardData(const std::string& sData, std::string& sErrorOut) override;

 private:
  bool RefreshReaders();
  bool EnsureOpenDevice(std::string& sErrorOut);
  bool ReadCardUID(std::string& sUIDOut);
  bool TransmitCardCommand(
      const unsigned char* pCommand, size_t iCommandSize,
      std::vector<unsigned char>& vPayloadOut, std::string& sErrorOut);
  bool ReadUserPages(
      unsigned char iStartPage, std::vector<unsigned char>& vDataOut,
      std::string& sErrorOut);
  bool WriteUserPage(
      unsigned char iPage, const unsigned char* pData, std::string& sErrorOut);
  static std::string BytesToHex(const unsigned char* pBytes, size_t nBytes);

  nfc_context* m_pContext;
  nfc_device* m_pDevice;
  std::vector<std::string> m_vReaderNames;
  std::string m_sCurrentCardUID;
  bool m_bCardPresent;
  bool m_bInitialized;
};

#endif
