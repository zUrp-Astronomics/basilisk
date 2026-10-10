/*
    Pinefeat EF / EF-S Lens Controller – Canon® Lens Compatible
    Copyright (C) 2025 Pinefeat LLP (support@pinefeat.co.uk)

    Based on Moonlite focuser
    Copyright (C) 2013-2019 Jasem Mutlaq (mutlaqja@ikarustech.com)

    Basilisk E-mount lens controller: extended set (when `t` reports ext=1),
    board major-version guard and a 15-character reply terminated, over indilib/indi 1dd9b34f24df6fd74ed45be17823fc937e192b47
    drivers/focuser/pinefeat_cef.{h,cpp}, changed 2026-10-04; the reply's form given to sendCommand, the letters
    not served remembered, the base properties seen without a cast under libindi 1.9.9, changed 2026-10-08
    Copyright (C) 2026 zUrp Astronomics

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#pragma once

#include "indifocuser.h"
#include <chrono>
#include <string>

class PinefeatCEF : public INDI::Focuser
{
    public:
        PinefeatCEF();
        ~PinefeatCEF() override = default;

        bool initProperties() override;
        bool updateProperties() override;
        bool ISNewNumber(const char * dev, const char * name, double values[], char * names[], int n) override;
        bool ISNewSwitch(const char * dev, const char * name, ISState * states, char * names[], int n) override;
        void TimerHit() override;
        bool saveConfigItems(FILE * fp) override;

    protected:
        const char * getDefaultName() override;
        bool Handshake() override;
        IPState MoveAbsFocuser(uint32_t targetTicks) override;
        IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks) override;
        bool SetFocuserSpeed(int speed) override;
        bool AbortFocuser() override;

    private:
        void updateProperties(const int32_t pos, const int32_t max, const std::string dist, const std::string aper);
        bool readFocusPosition(int32_t &pos);
        bool readFocusMaxPosition(int32_t &pos);
        bool readFocusDistance(std::string &result);
        bool readApertureRange(std::string &result);
        bool readFirmwareVersion();
        bool isNotMoving();
        bool moveFocusAbs(uint32_t position);
        bool moveFocusRel(FocusDirection dir, uint32_t offset);
        bool setSpeed(int speed);
        bool setApertureAbs(double value);
        bool setApertureRel(double value);
        bool calibrate();
        void readExtended(bool now);
        void readLed(IPState state);
        std::string readMoveResult();
        IPState moveEnd(bool positionRead);
        void watchStop();
        void moveStarted(bool rel);
        std::string extValue(const char * res, IPState &state);

        /**
         * @brief sendCommand Send a string command to the controller.
         * @param cmd Command to be sent, must already have the necessary delimiter ('\n')
         * @param res If not nullptr, the reply: read up to the delimiter ('\n'), kept, then a terminating zero.
         *        Must hold size + 1 bytes (CEF_RES for the default size). If nullptr, no read back is done and the
         *        function returns true.
         * @param size At most size bytes are read, the delimiter included.
         * @param isReply If not nullptr, the form of the reply: a line it refuses (given without its line end) is
         *        skipped, as a `* …` line is, until one it takes or CEF_TIMEOUT after the command.
         * @return True if successful, false otherwise.
         */
        bool sendCommand(const char * cmd, char * res = nullptr, int size = CEF_BUF,
                         bool (*isReply)(const std::string &) = nullptr);

        INDI::PropertySwitch CalibrateSP {1};

        INDI::PropertyNumber ApertureRelNP {1};

        INDI::PropertyNumber ApertureAbsNP {1};

        INDI::PropertyText ApertureRangeTP {1};

        INDI::PropertyText FocusDistanceTP {1};

        INDI::PropertyText FirmwareTP {1};

        // Extended set, defined only when `t` reports ext=1
        INDI::PropertyNumber ApertureNP {1};    // `o`
        INDI::PropertyText MarkTP {1};          // `j`
        INDI::PropertySwitch MarkSP {3};        // `js` `jg` `jx`
        INDI::PropertyNumber LedNP {1};         // `k`
        INDI::PropertySwitch RebootSP {1};      // `b`
        INDI::PropertyText LensTP {3};          // `i` `w` `n`

        // CEF Buffer Size
        static const uint8_t CEF_BUF { 16 };

        // A reply of CEF_BUF bytes ('\n' included: 15 characters, PROTOCOL.md) and its terminating zero
        static const uint8_t CEF_RES { CEF_BUF + 1 };

        // CEF Command Delimiter
        static const char CEF_DEL { '\n' };

        // CEF Command Timeout
        static const uint8_t CEF_TIMEOUT { 3 };

        // Buffer for the extended replies longer than CEF_BUF (`t`, `i`)
        static const int EXT_BUF { 256 };

        std::chrono::steady_clock::time_point lastUpdate;
        std::chrono::steady_clock::time_point extUpdate;

        int firmwareMajor {-1};
        bool ext {false};

        // Extended set: a BUSY it set, ended in TimerHit once `e` answers n
        bool tracked {false};       // a move the driver launched (absolute, relative, `jg`): its end is read by `g`
        bool relMove {false};       // that move was relative: FocusRelPosNP takes its end too
        bool rebooting {false};     // `b`: its end is `e` at n and `f` answering

        // `i` `w` `n` of LensTP that answered `er nocap` in this connection: not served, not asked again
        bool lensNocap[3] {};

        // After `stall` or `aborted`, `g` is read again until stopWatchEnd: the stop may turn `unconfirmed`
        bool stopWatch {false};
        std::string stopWatchFrom;
        std::chrono::steady_clock::time_point stopWatchEnd;

#if INDI_VERSION_MAJOR < 2
        // libindi 1.9.9: INDI::Focuser's own properties are plain INumberVectorProperty, without the
        // accessors this file uses. Same names, same structs, seen through those accessors. A field is reached
        // through NumberField, which holds the INumber itself: an INumber is not a WidgetView<INumber>, and a cast
        // to one would be undefined behaviour (audit R9).
        struct NumberField
        {
            INumber &n;
            void setMinMax(double min, double max) { n.min = min; n.max = max; }
            void setStep(double step) { n.step = step; }
            void setValue(double value) { n.value = value; }
            double getValue() const { return n.value; }
            const char * getName() const { return n.name; }
        };
        struct NumberView
        {
            INumberVectorProperty &p;
            NumberField operator[](size_t i) const
            {
                return NumberField { p.np[i] };
            }
            void setState(IPState s) { p.s = s; }
            IPState getState() const { return p.s; }
            void apply() const { IDSetNumber(&p, nullptr); }
            const char * getName() const { return p.name; }
        };
        NumberView FocusSpeedNP { INDI::FocuserInterface::FocusSpeedNP };
        NumberView FocusAbsPosNP { INDI::FocuserInterface::FocusAbsPosNP };
        NumberView FocusRelPosNP { INDI::FocuserInterface::FocusRelPosNP };
        NumberView FocusMaxPosNP { INDI::FocuserInterface::FocusMaxPosNP };
#endif
};
