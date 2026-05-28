#ifndef SCREEN_NFC_LINK_PROFILE_H
#define SCREEN_NFC_LINK_PROFILE_H

#include <string>

#include "BitmapText.h"
#include "InputEventPlus.h"
#include "ScreenWithMenuElements.h"

struct Message;

class ScreenNFCLinkProfile : public ScreenWithMenuElements {
 public:
  void Init() override;
  void Update(float fDeltaTime) override;
  void HandleMessage(const Message& msg) override;

  bool MenuStart(const InputEventPlus& input) override;
  bool MenuBack(const InputEventPlus& input) override;

  bool HasPendingLink() const;
  std::string GetPendingCardUID() const;
  bool PendingLinkRequiresMove() const;
  std::string GetPendingSourceProfileID() const;
  std::string GetPendingSourceProfileName() const;
  bool ConfirmPendingLink(bool bMoveExisting = true);
  void CancelPendingLink();

 private:
  void RefreshDisplay();
  void StageUIDForLink(const std::string& sUID);
  bool LinkUIDToProfile(const std::string& sUID, bool bMoveExisting);
  void ClearPendingLink();

  BitmapText m_textTitle;
  BitmapText m_textStatus;
  BitmapText m_textUID;
  BitmapText m_textReader;
  BitmapText m_textInstructions;

  std::string m_sProfileID;
  std::string m_sLastLinkedUID;
  std::string m_sErrorStatus;
  std::string m_sPendingUID;
  std::string m_sPendingSourceProfileID;
  std::string m_sPendingSourceProfileName;
  bool m_bTappedCard = false;
  bool m_bPendingMove = false;
};

#endif
