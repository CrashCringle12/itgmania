#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#include <memory>
#include <string>
#include <vector>

#include "Preference.h"
#include "RageThreads.h"

struct lua_State;
class NFCDriver;

/**
 * @brief Manages NFC card readers and broadcasts tap/remove events.
 *
 * NFCMAN is a global singleton initialized alongside the other game managers.
 * It runs a background polling thread that detects card presence via the
 * platform PC/SC driver and broadcasts Message_NFCCardTapped /
 * Message_NFCCardRemoved messages via the MESSAGEMAN system so that screens
 * (e.g. ScreenSelectProfile) can react to them.
 *
 * The UID of the last tapped card is available via GetLastCardUID() and is
 * also passed as a parameter on the broadcast message.
 */
class NFCManager {
 public:
  NFCManager();
  ~NFCManager();

  /** @brief Preferences */
  static Preference<bool> m_bNFCEnabled;
  static Preference<float> m_fPollIntervalSeconds;

  /**
   * @brief True if NFC support was successfully initialized.
   *
   * Will be false if the pcscd daemon is not running, no reader is connected,
   * or NFC support was disabled in preferences.
   */
  bool IsEnabled() const { return m_bEnabled; }

  /**
   * @brief UID (uppercase hex) of the card currently on the reader.
   *
   * Returns an empty string when no card is present.
   */
  std::string GetCurrentCardUID() const;

  /**
   * @brief UID of the most recently tapped card.
   *
   * Persists until the next tap, unlike GetCurrentCardUID() which clears when
   * the card is removed.
   */
  std::string GetLastTappedUID() const;

  /** @brief True if a card is currently resting on a reader. */
  bool IsCardPresent() const;

  /** @brief Return a list of reader names visible to the driver. */
  std::vector<std::string> GetReaderNames() const;

  // Lua
  void PushSelf(lua_State* L);

 private:
  static int PollThread_Start(void* p);
  void PollThread();

  std::unique_ptr<NFCDriver> m_pDriver;
  RageThread m_PollThread;
  mutable RageMutex m_Mutex;

  bool m_bEnabled;
  bool m_bShutdown;

  // Protected by m_Mutex
  std::string m_sCurrentCardUID;
  std::string m_sLastTappedUID;
  bool m_bCardPresent;
};

extern NFCManager* NFCMAN;

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
