#ifndef NFC_DRIVER_H
#define NFC_DRIVER_H

#include <string>
#include <vector>

/**
 * @brief Abstract base for NFC reader drivers.
 *
 * Implementations poll for card presence, read card UIDs, and report events to
 * NFCManager via the virtual callbacks below.
 */
class NFCDriver {
 public:
  virtual ~NFCDriver() {}

  /** @brief Initialize the driver. Returns true on success. */
  virtual bool Init() = 0;

  /**
   * @brief Poll for card events.
   *
   * Should be called periodically from a background thread.
   * Implementations call OnCardTapped / OnCardRemoved as appropriate.
   */
  virtual void Poll() = 0;

  /** @brief Return a list of reader names discovered by this driver. */
  virtual std::vector<std::string> GetReaderNames() const = 0;

  /**
   * @brief The UID of the most recently tapped card, empty if none present.
   *
   * The UID is represented as an uppercase hex string, e.g. "A1B2C3D4".
   */
  virtual std::string GetCurrentCardUID() const = 0;

  /**
   * @brief True if a card is currently present on the reader.
   */
  virtual bool IsCardPresent() const = 0;

  /** @brief True if the driver supports theme-managed card data reads. */
  virtual bool SupportsCardDataIO() const { return false; }

  /** @brief Maximum payload bytes supported by ReadCardData. */
  virtual int GetMaxCardDataBytes() const { return 0; }

  /** @brief Read theme-managed payload data from the current card. */
  virtual bool ReadCardData(std::string&, std::string& sErrorOut) {
    sErrorOut = "Card data I/O is unavailable.";
    return false;
  }
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
