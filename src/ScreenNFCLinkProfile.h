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

 private:
  void RefreshDisplay();
  void LinkUIDToProfile(const std::string& sUID);

  BitmapText m_textTitle;
  BitmapText m_textStatus;
  BitmapText m_textUID;
  BitmapText m_textReader;
  BitmapText m_textInstructions;

  std::string m_sProfileID;
  std::string m_sLastLinkedUID;
  std::string m_sErrorStatus;
  bool m_bTappedCard = false;
};

#endif
