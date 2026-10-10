/*
    Pinefeat EF / EF-S Lens Controller – Canon® Lens Compatible
    Copyright (C) 2025 Pinefeat LLP (support@pinefeat.co.uk)

    Based on Moonlite focuser
    Copyright (C) 2013-2019 Jasem Mutlaq (mutlaqja@ikarustech.com)

    Basilisk E-mount lens controller: extended set (when `t` reports ext=1),
    board major-version guard and a 15-character reply terminated, over indilib/indi 1dd9b34f24df6fd74ed45be17823fc937e192b47
    drivers/focuser/pinefeat_cef.{h,cpp}, changed 2026-10-04; `a` read whole, the version reply judged by its form,
    `nc` compared whole, unsigned moves, replies shown without their line end, the state read once a second,
    `w` and `n` asked once a connection, changed 2026-10-08
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

#include "pinefeat_cef.h"

#include <connectionplugins/connectionserial.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <sstream>
#include <sys/ioctl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

// Driver 1.0. Its major must equal the board's (`v`).
#define DRIVER_MAJOR 1
#define DRIVER_MINOR 0

// The board's reply, without the line end that tty_nread_section keeps
static std::string chomp(const char * res)
{
    std::string s(res);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

// A reply, as the log shows it: `nc` (PROTOCOL.md § 1: no lens) said in words, anything else as it came, without its
// line end. The whole reply is compared: upstream's strcmp(res, "nc") > 0 also took any reply sorted after `nc`.
// The temporary lives to the end of the full expression, the log call that uses it.
#define ERR_NC(res) (chomp(res) == "nc" ? std::string("lens is not attached") : chomp(res)).c_str()

// The form of a reply to `v`, `<maj>.<min>` (PROTOCOL.md § 2), line end apart. A line of another form is not the
// reply: opening the port can restart the board, and the lines of its ROM can arrive after `v` was sent (audit R5).
static bool isVersion(const std::string &line)
{
    size_t dot = line.find('.');
    auto digits = [&](size_t from, size_t to)
    {
        return to > from
               && std::all_of(line.begin() + from, line.begin() + to, [](char c) { return c >= '0' && c <= '9'; });
    };
    return dot != std::string::npos && digits(0, dot) && digits(dot + 1, line.size());
}

static std::unique_ptr<PinefeatCEF> pinefeatCEF(new PinefeatCEF());

PinefeatCEF::PinefeatCEF()
{
    setVersion(DRIVER_MAJOR, DRIVER_MINOR);

    FI::SetCapability(FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_HAS_VARIABLE_SPEED);

    lastUpdate = std::chrono::steady_clock::now();
}

const char * PinefeatCEF::getDefaultName()
{
    return "Basilisk E-mount Lens";
}

bool PinefeatCEF::initProperties()
{
    INDI::Focuser::initProperties();

    FocusSpeedNP[0].setMinMax(1, 4);
    FocusSpeedNP[0].setStep(1);
    FocusSpeedNP[0].setValue(1);

    FocusMaxPosNP[0].setMinMax(0, 32767);
    FocusMaxPosNP[0].setStep(1);
    FocusMaxPosNP[0].setValue(0);

    FocusRelPosNP[0].setMinMax(0, 32767);
    FocusRelPosNP[0].setStep(1);
    FocusRelPosNP[0].setValue(0);

    FocusAbsPosNP[0].setMinMax(0, 32767);
    FocusAbsPosNP[0].setStep(1);
    FocusAbsPosNP[0].setValue(0);

    CalibrateSP[0].fill("CALIBRATE", "Calibrate", ISS_OFF);
    CalibrateSP.fill(m_defaultDevice->getDeviceName(), "CALIBRATE", "Calibrate", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 60,
                     IPS_OK);

    ApertureAbsNP[0].fill("APERTURE_ABSOLUTE", "f-stop", "%.f", 0.0, 327.67, 0.0, 0.0);
    ApertureAbsNP.fill(getDeviceName(), "ABS_APERTURE", "Absolute Aperture", MAIN_CONTROL_TAB, IP_WO,
                       60, IPS_OK);

    ApertureRelNP[0].fill("APERTURE_RELATIVE", "f-stop", "%.f", -327.68, 327.67, 0.0, 0.0);
    ApertureRelNP.fill(getDeviceName(), "REL_APERTURE", "Relative Aperture", MAIN_CONTROL_TAB, IP_WO,
                       60, IPS_OK);

    ApertureRangeTP[0].fill("APERTURE_RANGE", "f-stop", nullptr);
    ApertureRangeTP.fill(getDeviceName(), "RANGE_APERTURE", "Aperture range", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    FocusDistanceTP[0].fill("FOCUS_DISTANCE", "meter", nullptr);
    FocusDistanceTP.fill(getDeviceName(), "FOCUS_DISTANCE", "Focus Distance", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    FirmwareTP[0].fill("FIRMWARE_VERSION", "Version", "Unknown");
    FirmwareTP.fill(getDeviceName(), "FIRMWARE_VERSION", "Firmware", INFO_TAB, IP_RO, 0, IPS_IDLE);

    ApertureNP[0].fill("FNUMBER", "f-stop", "%.1f", 0.0, 327.67, 0.0, 0.0);
    ApertureNP.fill(getDeviceName(), "APERTURE", "Aperture", MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    MarkTP[0].fill("MARK", "steps", nullptr);
    MarkTP.fill(getDeviceName(), "FOCUS_MARK", "Focus mark", MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    MarkSP[0].fill("MARK_SET", "Set mark here", ISS_OFF);
    MarkSP[1].fill("MARK_GOTO", "Go to mark", ISS_OFF);
    MarkSP[2].fill("MARK_CLEAR", "Clear mark", ISS_OFF);
    MarkSP.fill(getDeviceName(), "FOCUS_MARK_CTL", "Mark", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 60, IPS_OK);

    LedNP[0].fill("LEVEL", "%", "%.f", 0, 100, 1, 100);
    LedNP.fill(getDeviceName(), "LED_BRIGHTNESS", "Status LED", OPTIONS_TAB, IP_RW, 60, IPS_IDLE);

    RebootSP[0].fill("REBOOT", "Reboot lens", ISS_OFF);
    RebootSP.fill(getDeviceName(), "LENS_REBOOT", "Lens", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 60, IPS_OK);

    LensTP[0].fill("LENS_NAME", "Name", nullptr);
    LensTP[1].fill("LENS_FIRMWARE", "Firmware", nullptr);
    LensTP[2].fill("LENS_MODULE", "Module", nullptr);
    LensTP.fill(getDeviceName(), "LENS_INFO", "Lens", "Lens", IP_RO, 0, IPS_IDLE);

    serialConnection->setDefaultBaudRate(Connection::Serial::B_115200);

    setDefaultPollingPeriod(50);

    return true;
}

bool PinefeatCEF::updateProperties()
{
    INDI::Focuser::updateProperties();

    if (isConnected())
    {
        defineProperty(FirmwareTP);
        defineProperty(FocusDistanceTP);
        defineProperty(CalibrateSP);
        defineProperty(ApertureRangeTP);
        defineProperty(ApertureAbsNP);
        defineProperty(ApertureRelNP);

        int32_t pos, max;
        std::string dist, aper;
        if (readFocusPosition(pos)
                && (max = pos, 1)
                && readFocusMaxPosition(max)
                && readFocusDistance(dist)
                && readApertureRange(aper))
        {
            updateProperties(pos, max, dist, aper);
            LOG_INFO("Parameters updated, the controller is ready for use.");
        }

        if (ext)
        {
            defineProperty(ApertureNP);
            defineProperty(MarkTP);
            defineProperty(MarkSP);
            defineProperty(RebootSP);
            defineProperty(LensTP);
            defineProperty(LedNP);

            // The board keeps no LED level (100 at each start): the saved one is sent again
            loadConfig(true, LedNP.getName());
            readLed(IPS_OK);
            readExtended(true);
        }
    }
    else
    {
        deleteProperty(FirmwareTP);
        deleteProperty(FocusDistanceTP);
        deleteProperty(CalibrateSP);
        deleteProperty(ApertureRangeTP);
        deleteProperty(ApertureAbsNP);
        deleteProperty(ApertureRelNP);

        if (ext)
        {
            deleteProperty(ApertureNP);
            deleteProperty(MarkTP);
            deleteProperty(MarkSP);
            deleteProperty(RebootSP);
            deleteProperty(LensTP);
            deleteProperty(LedNP);
        }
    }

    return true;
}

void PinefeatCEF::updateProperties(const int32_t pos, const int32_t max, const std::string dist, const std::string aper)
{
    FocusAbsPosNP[0].setValue(pos);
    // Extended set: the state a move ended in (TimerHit) stays until the next one; only a BUSY ends here
    if (!ext || FocusAbsPosNP.getState() == IPS_BUSY)
        FocusAbsPosNP.setState(IPS_OK);
    FocusAbsPosNP.apply();

    if (FocusRelPosNP.getState() == IPS_BUSY)
    {
        FocusRelPosNP.setState(IPS_OK);
        FocusRelPosNP.apply();
    }

    if (FocusMaxPosNP.getState() == IPS_BUSY)
    {
        FocusMaxPosNP[0].setValue(max);
        FocusMaxPosNP.setState(IPS_IDLE);
        FocusMaxPosNP.apply();

        double values[] = { FocusMaxPosNP[0].getValue() };
        char *names[] = { (char*)FocusMaxPosNP[0].getName() };
        ISNewNumber(getDeviceName(), FocusMaxPosNP.getName(), values, names, 1);
    }

    FocusDistanceTP[0].setText(dist);
    FocusDistanceTP.apply();

    ApertureRangeTP[0].setText(aper);
    ApertureRangeTP.apply();
}

// Three tries of `v`. A `v` sent while the board restarts (opening the port can restart it) is lost: its read ends at
// CEF_TIMEOUT, and the next try goes; the lines of its ROM are not taken for the reply (readFirmwareVersion).
bool PinefeatCEF::Handshake()
{
    for (int i = 0; i < 3; i++)
    {
        if (readFirmwareVersion())
        {
            if (firmwareMajor != DRIVER_MAJOR)
            {
                LOGF_ERROR("Board firmware %s does not match driver %d.%d: their major versions must be equal.",
                           chomp(FirmwareTP[0].getText()).c_str(), DRIVER_MAJOR, DRIVER_MINOR);
                return false;
            }

            // A Pinefeat controller answers `er` to `t`: only a board that reports ext=1 gets the extended set
            char res[EXT_BUF] = {0};
            ext = false;
            if (sendCommand("t\n", res, EXT_BUF - 1))
            {
                std::istringstream line(chomp(res));
                std::string token;
                while (line >> token)
                    ext = ext || token == "ext=1";
            }

            uint32_t cap = FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_HAS_VARIABLE_SPEED;
            FI::SetCapability(ext ? cap | FOCUSER_CAN_ABORT : cap);

            tracked = relMove = rebooting = stopWatch = false;
            std::fill(std::begin(lensNocap), std::end(lensNocap), false);
            if (ext)
            {
                LOG_INFO("Extended set enabled (ext=1).");
                // A move's end state stays until the next one (updateProperties): none from a past connection
                FocusAbsPosNP.setState(IPS_OK);
            }

            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(getCurrentPollingPeriod()));
    }

    LOG_ERROR("Can't detect the controller, please ensure the device is powered and the port is correct.");
    return false;
}

// `v`: the first line of the form `<maj>.<min>` (isVersion) that comes within CEF_TIMEOUT, read into EXT_BUF so that a
// longer line of the ROM is skipped whole; none, and the board is not detected (Handshake tries again).
bool PinefeatCEF::readFirmwareVersion()
{
    char res[EXT_BUF] = {0};

    if (!sendCommand("v\n", res, EXT_BUF - 1, isVersion))
        return false;

    std::string version = chomp(res);
    int major, minor;
    firmwareMajor = sscanf(version.c_str(), "%d.%d", &major, &minor) == 2 ? major : -1;

    FirmwareTP[0].setText(version);
    FirmwareTP.setState(IPS_OK);

    LOGF_INFO("Detected firmware version %s.", version.c_str());

    return true;
}

bool PinefeatCEF::readFocusPosition(int32_t &pos)
{
    char res[CEF_RES] = {0};

    if (!sendCommand("f\n", res))
        return false;

    int rc = sscanf(res, "%d", &pos);
    if (rc <= 0)
    {
        LOGF_ERROR("Can't read focus position: %s.", ERR_NC(res));
        return false;
    }

    return true;
}

bool PinefeatCEF::readFocusMaxPosition(int32_t &pos)
{
    char res[CEF_RES] = {0};

    if (!sendCommand("r\n", res))
        return false;

    int rc = sscanf(res, "%*d-%d", &pos);
    if (rc <= 0)
    {
        LOGF_ERROR("Can't read max focus position: %s.", ERR_NC(res));
        return false;
    }

    return true;
}

bool PinefeatCEF::readFocusDistance(std::string &result)
{
    char res[CEF_RES] = {0};

    if (!sendCommand("d\n", res))
        return false;

    result = chomp(res);
    return true;
}

// `a` is read into EXT_BUF, as `t`: a reply longer than 15 characters (an older board's `er nocap aperture`) is read
// whole, not cut by an overflow that fails the connection and every refresh (audit S2)
bool PinefeatCEF::readApertureRange(std::string &result)
{
    char res[EXT_BUF] = {0};

    if (!sendCommand("a\n", res, EXT_BUF - 1))
        return false;

    result = chomp(res);
    return true;
}

bool PinefeatCEF::isNotMoving()
{
    char res[CEF_RES] = {0};
    return sendCommand("e\n", res) && strstr(res, "n");
}

bool PinefeatCEF::moveFocusAbs(uint32_t position)
{
    char cmd[CEF_BUF] = {0};
    char res[CEF_RES] = {0};
    snprintf(cmd, CEF_BUF, "f%u\n", position);

    if (!sendCommand(cmd, res))
        return false;

    if (!strstr(res, "ok"))
    {
        LOGF_ERROR("Can't focus: %s.", ERR_NC(res));
        return false;
    }

    return true;
}

bool PinefeatCEF::moveFocusRel(FocusDirection dir, uint32_t offset)
{
    char cmd[CEF_BUF] = {0};
    char res[CEF_RES] = {0};
    snprintf(cmd, CEF_BUF, "f%s%u\n", (dir == FOCUS_INWARD) ? "-" : "+", offset);

    if (!sendCommand(cmd, res))
        return false;

    if (!strstr(res, "ok"))
    {
        LOGF_ERROR("Can't focus: %s.", ERR_NC(res));
        return false;
    }

    return true;
}

bool PinefeatCEF::setSpeed(int speed)
{
    char cmd[CEF_BUF] = {0};
    char res[CEF_RES] = {0};
    snprintf(cmd, CEF_BUF, "s%d\n", speed);

    if (!sendCommand(cmd, res))
        return false;

    if (!strstr(res, "ok"))
    {
        LOGF_ERROR("Can't set speed: %s.", ERR_NC(res));
        return false;
    }

    return true;
}

bool PinefeatCEF::setApertureAbs(double value)
{
    char cmd[CEF_BUF] = {0};
    char res[CEF_RES] = {0};
    snprintf(cmd, CEF_BUF, "a%.6g\n", value);

    if (!sendCommand(cmd, res))
        return false;

    if (!strstr(res, "ok"))
    {
        LOGF_ERROR("Can't set aperture: %s.", ERR_NC(res));
        return false;
    }

    LOGF_INFO("Aperture is set to f/%.6g.", value);

    return true;
}

bool PinefeatCEF::setApertureRel(double value)
{
    char cmd[CEF_BUF] = {0};
    char res[CEF_RES] = {0};
    snprintf(cmd, CEF_BUF, "a%s%.6g\n", (value < 0) ? "" : "+", value);

    if (!sendCommand(cmd, res))
        return false;

    if (!strstr(res, "ok"))
    {
        LOGF_ERROR("Can't set aperture: %s.", ERR_NC(res));
        return false;
    }

    LOGF_INFO("Iris is %s by f/%.6g further.", (value > 0) ? "closed" : "opened", abs(value));

    return true;
}

bool PinefeatCEF::calibrate()
{
    char res[CEF_RES] = {0};

    if (!sendCommand("c\n", res))
        return false;

    if (!strstr(res, "ok"))
    {
        LOGF_ERROR("Can't calibrate: %s.", ERR_NC(res));
        return false;
    }

    return true;
}

// `o`, `j`, `i`, `w`, `n`: once a second while the focuser is still, or now
void PinefeatCEF::readExtended(bool now)
{
    auto t = std::chrono::steady_clock::now();
    if (!now && std::chrono::duration_cast<std::chrono::seconds>(t - extUpdate).count() < 1)
        return;
    extUpdate = t;

    char res[EXT_BUF] = {0};

    if (!sendCommand("o\n", res, EXT_BUF - 1))
        return;
    double fnum;
    if (sscanf(res, "%lf", &fnum) == 1)
    {
        ApertureNP[0].setValue(fnum);
        ApertureNP.setState(IPS_OK);
    }
    else
        ApertureNP.setState(IPS_IDLE);     // `er nocap ap`: the lens serves no aperture
    ApertureNP.apply();

    memset(res, 0, sizeof(res));
    if (!sendCommand("j\n", res, EXT_BUF - 1))
        return;
    IPState state = IPS_IDLE;
    MarkTP[0].setText(extValue(res, state));
    MarkTP.setState(state);
    MarkTP.apply();

    // A letter that answers `er nocap` is not served, in every state (PROTOCOL.md § 2: `w` and `n` today): it is not
    // asked again in this connection, its field stays empty and weighs nothing in the state, as its `er nocap` did
    const char * cmds[] = { "i\n", "w\n", "n\n" };
    state = IPS_IDLE;
    for (int i = 0; i < 3; i++)
    {
        if (lensNocap[i])
            continue;
        memset(res, 0, sizeof(res));
        if (!sendCommand(cmds[i], res, EXT_BUF - 1))
            return;
        LensTP[i].setText(extValue(res, state));
        lensNocap[i] = chomp(res).compare(0, 8, "er nocap") == 0;
    }
    LensTP.setState(state);
    LensTP.apply();
}

// A reply of the extended set, as a field: an `er …` is never shown as a value, the field is empty.
// The property's state, over its fields: Alert if one is refused for now (`er nolens`, `er busy boot`,
// `er fault`: the value exists, it can't be read), else Ok if one holds a value, else Idle (`er nocap`:
// the board doesn't serve it, as `w` and `n` today).
std::string PinefeatCEF::extValue(const char * res, IPState &state)
{
    std::string s = chomp(res);
    if (s.compare(0, 3, "er ") != 0 && s != "er")
    {
        if (state == IPS_IDLE)
            state = IPS_OK;
        return s;
    }
    if (s.compare(0, 8, "er nocap") != 0)
        state = IPS_ALERT;
    return "";
}

// `k`: the value shown is the one the board reads back
void PinefeatCEF::readLed(IPState state)
{
    char res[EXT_BUF] = {0};
    int level;

    if (sendCommand("k\n", res, EXT_BUF - 1) && sscanf(res, "%d", &level) == 1)
        LedNP[0].setValue(level);
    else
        state = IPS_ALERT;

    LedNP.setState(state);
    LedNP.apply();
}

bool PinefeatCEF::ISNewSwitch(const char * dev, const char * name, ISState * states, char * names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (CalibrateSP.isNameMatch(name))
        {
            CalibrateSP.reset();

            if (calibrate())
            {
                // Delay further update until calibration starter
                lastUpdate = std::chrono::steady_clock::now();

                FocusAbsPosNP.setState(IPS_BUSY);
                FocusAbsPosNP.apply();

                FocusMaxPosNP.setState(IPS_BUSY);
                FocusMaxPosNP.apply();

                CalibrateSP.setState(IPS_OK);
            }
            else
                CalibrateSP.setState(IPS_ALERT);

            CalibrateSP.apply();
            return true;
        }

        if (ext && MarkSP.isNameMatch(name))
        {
            MarkSP.update(states, names, n);
            int i = MarkSP.findOnSwitchIndex();
            MarkSP.reset();

            const char * cmd = i == 0 ? "js\n" : i == 1 ? "jg\n" : i == 2 ? "jx\n" : nullptr;
            char res[EXT_BUF] = {0};
            if (cmd != nullptr && sendCommand(cmd, res, EXT_BUF - 1) && strstr(res, "ok"))
            {
                if (i == 1)
                {
                    moveStarted(false);
                    FocusAbsPosNP.setState(IPS_BUSY);
                    FocusAbsPosNP.apply();
                }
                MarkSP.setState(IPS_OK);
            }
            else
            {
                if (cmd != nullptr)
                    LOGF_ERROR("Can't %s: %s.", i == 0 ? "set the mark" : i == 1 ? "go to the mark" : "clear the mark",
                               chomp(res).c_str());
                MarkSP.setState(IPS_ALERT);
            }

            MarkSP.apply();
            readExtended(true);
            return true;
        }

        if (ext && RebootSP.isNameMatch(name))
        {
            RebootSP.reset();

            char res[EXT_BUF] = {0};
            if (sendCommand("b\n", res, EXT_BUF - 1) && strstr(res, "ok"))
            {
                LOG_INFO("Lens reboot requested.");
                rebooting = true;
                stopWatch = false;
                FocusAbsPosNP.setState(IPS_BUSY);
                FocusAbsPosNP.apply();
                RebootSP.setState(IPS_OK);
            }
            else
            {
                LOGF_ERROR("Can't reboot the lens: %s.", chomp(res).c_str());
                RebootSP.setState(IPS_ALERT);
            }

            RebootSP.apply();
            return true;
        }
    }

    return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);
}

bool PinefeatCEF::ISNewNumber(const char * dev, const char * name, double values[], char * names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (ApertureAbsNP.isNameMatch(name))
        {
            ApertureAbsNP.update(values, names, n);

            bool res = setApertureAbs(ApertureAbsNP[0].getValue());
            if (res)
                ApertureAbsNP.setState(IPS_OK);
            else
                ApertureAbsNP.setState(IPS_ALERT);

            ApertureAbsNP.apply();
            return res;
        }

        if (ApertureRelNP.isNameMatch(name))
        {
            ApertureRelNP.update(values, names, n);

            bool res = setApertureRel(ApertureRelNP[0].getValue());
            if (res)
                ApertureRelNP.setState(IPS_OK);
            else
                ApertureRelNP.setState(IPS_ALERT);

            ApertureRelNP.apply();
            return res;
        }

        if (ext && LedNP.isNameMatch(name))
        {
            LedNP.update(values, names, n);

            char cmd[CEF_BUF] = {0};
            char res[EXT_BUF] = {0};
            snprintf(cmd, CEF_BUF, "k%ld\n", lround(LedNP[0].getValue()));

            bool ok = sendCommand(cmd, res, EXT_BUF - 1) && strstr(res, "ok");
            if (!ok)
                LOGF_ERROR("Can't set the LED brightness: %s.", chomp(res).c_str());

            readLed(ok ? IPS_OK : IPS_ALERT);
            return ok;
        }
    }

    return INDI::Focuser::ISNewNumber(dev, name, values, names, n);
}

bool PinefeatCEF::SetFocuserSpeed(int speed)
{
    return setSpeed(speed);
}

bool PinefeatCEF::AbortFocuser()
{
    // FOCUS_ABORT_MOTION reaches this by its name even where it is not defined: no `q` without ext=1
    if (!ext)
        return false;

    char res[EXT_BUF] = {0};
    if (!sendCommand("q\n", res, EXT_BUF - 1))
        return false;

    if (!strstr(res, "ok"))
    {
        LOGF_ERROR("Can't abort: %s.", chomp(res).c_str());
        return false;
    }

    return true;
}

bool PinefeatCEF::saveConfigItems(FILE * fp)
{
    INDI::Focuser::saveConfigItems(fp);

    if (ext)
        LedNP.save(fp);

    return true;
}

IPState PinefeatCEF::MoveAbsFocuser(uint32_t targetTicks)
{
    if (!moveFocusAbs(targetTicks))
        return IPS_ALERT;

    moveStarted(false);
    return IPS_BUSY;
}

IPState PinefeatCEF::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    if (!moveFocusRel(dir, ticks))
        return IPS_ALERT;

    moveStarted(true);
    return IPS_BUSY;
}

// Extended set: a move the board accepted (absolute, relative, `jg`) is tracked to its end, read by `g`;
// it ends the watch of the previous stop
void PinefeatCEF::moveStarted(bool rel)
{
    if (!ext)
        return;
    tracked = true;
    relMove = rel;
    stopWatch = false;
}

// Extended set, once `e` answers n after a tracked move: its end, as the state of the focuser position.
// `g` (PROTOCOL.md § 2): `ok` Ok; `aborted`, the user's own `q` (AbortFocuser: the barrel button sends none),
// is no failure: Idle, the state INDI::Focuser gives a successful abort; anything else Alert, logged. After
// `stall` or `aborted`, the stop is watched (watchStop). A position `f` can't read is Alert whatever `g` says.
IPState PinefeatCEF::moveEnd(bool positionRead)
{
    std::string g = readMoveResult();
    IPState state = IPS_ALERT;

    if (g == "ok")
        state = IPS_OK;
    else if (g == "aborted")
        state = IPS_IDLE;
    else if (g == "unconfirmed")
        LOG_ERROR("Stop not confirmed: the motor state is unknown.");
    else
        LOGF_ERROR("Focus move failed: %s.", g.empty() ? "no reply to g" : g.c_str());

    if (g == "stall" || g == "aborted")
    {
        // `unconfirmed` comes at most 4.5 s after the stop was first sent (PROTOCOL.md § 3): 5 s, a margin
        stopWatch = true;
        stopWatchFrom = g;
        stopWatchEnd = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    }

    return positionRead ? state : IPS_ALERT;
}

// Extended set: `g` read at each timer pass while the stop is watched. `unconfirmed` turns the position
// to Alert; any other change of `g` (a move the barrel button launched has ended) or the end of the window
// ends the watch, and the state of the move's end stays.
void PinefeatCEF::watchStop()
{
    if (std::chrono::steady_clock::now() >= stopWatchEnd)
    {
        stopWatch = false;
        return;
    }

    std::string g = readMoveResult();
    if (g.empty() || g == stopWatchFrom)
        return;

    stopWatch = false;
    if (g != "unconfirmed")
        return;

    LOG_ERROR("Stop not confirmed: the motor state is unknown.");
    FocusAbsPosNP.setState(IPS_ALERT);
    FocusAbsPosNP.apply();
    if (relMove)
    {
        FocusRelPosNP.setState(IPS_ALERT);
        FocusRelPosNP.apply();
    }
}

// `g`: the result of the last tracked move, without its line end; empty if the board did not reply
std::string PinefeatCEF::readMoveResult()
{
    char res[EXT_BUF] = {0};
    if (!sendCommand("g\n", res, EXT_BUF - 1))
        return "";
    return chomp(res);
}

void PinefeatCEF::TimerHit()
{
    if (!isConnected())
        return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate).count();

    if (ext && stopWatch)
        watchStop();

    // The state is read once a second: lastUpdate moves at each read (upstream set it only at CALIBRATE, and read
    // `e f r d a` at every pass of the timer, 50 ms, once the first second was gone)
    if (elapsed >= 1)
        lastUpdate = now;

    if (elapsed >= 1 && isNotMoving())
    {
        int32_t pos, max;
        std::string dist, aper;
        bool positionRead = readFocusPosition(pos);
        bool read = positionRead
                && (max = pos, 1)
                && readFocusMaxPosition(max)
                && readFocusDistance(dist)
                && readApertureRange(aper);

        // Extended set: a BUSY it set always ends here, `e` at n. A tracked move by `g`, `b` by `f` answering;
        // Alert if `f` doesn't (no lens, `er fault`)
        if (ext && (tracked || rebooting))
        {
            IPState state = tracked ? moveEnd(positionRead) : positionRead ? IPS_OK : IPS_ALERT;
            tracked = rebooting = false;

            FocusAbsPosNP.setState(state);
            if (!read)
                FocusAbsPosNP.apply();
            if (FocusRelPosNP.getState() == IPS_BUSY)
            {
                FocusRelPosNP.setState(state);
                FocusRelPosNP.apply();
            }
        }

        if (read)
        {
            updateProperties(pos, max, dist, aper);
        }

        if (ext)
            readExtended(false);
    }

    SetTimer(getCurrentPollingPeriod());
}

bool PinefeatCEF::sendCommand(const char * cmd, char * res, int size, bool (*isReply)(const std::string &))
{
    int nbytes_written = 0, nbytes_read = 0, rc = -1;

    // The pending input is discarded, as tcflush did, but read: a line of the board's log arrives in pieces
    // (a `* rx` line of LOG ALL is longer than a USB packet), and if the last piece read is not a line's end,
    // the rest of that line, still to come, is not the reply either
    bool midLine = false;
    int pending = 0;
    while (ioctl(PortFD, FIONREAD, &pending) == 0 && pending > 0)
    {
        char old[256];
        ssize_t n = read(PortFD, old, std::min(pending, int(sizeof(old))));
        if (n <= 0)
            break;
        midLine = old[n - 1] != CEF_DEL;
    }
    tcflush(PortFD, TCOFLUSH);

    LOGF_DEBUG("CMD <%s>", cmd);

    if ((rc = tty_write_string(PortFD, cmd, &nbytes_written)) != TTY_OK)
    {
        char errstr[MAXRBUF] = {0};
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("Serial write error: %s.", errstr);
        return false;
    }

    if (res == nullptr)
    {
        tcdrain(PortFD);
        return true;
    }

    // A `* …` line of the board's log (LOG ON or LOG ALL, left by the bench page) is never the reply: it is
    // consumed up to its '\n', however long, and the reply is the next line; so is the rest of a line cut
    // above, and, when the caller gives the reply's form (isReply), a line of another form. Skipping stops
    // CEF_TIMEOUT after the command: a board that keeps logging and never replies fails as a silent one does.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(int(CEF_TIMEOUT));
    char c = 0;
    auto readByte = [&]()
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            LOG_ERROR("Serial read error: no reply among the board's log lines.");
            return false;
        }
        if ((rc = tty_read(PortFD, &c, 1, CEF_TIMEOUT, &nbytes_read)) != TTY_OK)
        {
            char errstr[MAXRBUF] = {0};
            tty_error_msg(rc, errstr, MAXRBUF);
            LOGF_ERROR("Serial read error: %s.", errstr);
            return false;
        }
        return true;
    };

    if (!readByte())
        return false;
    for (;;)
    {
        while (c == '*' || midLine)
        {
            midLine = false;
            while (c != CEF_DEL)
                if (!readByte())
                    return false;
            if (!readByte())
                return false;
        }
        res[0] = c;

        // At most size bytes, '\n' kept, then a zero, which tty_nread_section does not write when the reply fills
        // its size: res holds size + 1.
        // A 15-character reply and its '\n' (`er range limits`) fill CEF_BUF, its zero is res[CEF_BUF].
        nbytes_read = 0;
        if (c != CEF_DEL && (rc = tty_nread_section(PortFD, res + 1, size - 1, CEF_DEL, CEF_TIMEOUT, &nbytes_read)) != TTY_OK)
        {
            char errstr[MAXRBUF] = {0};
            tty_error_msg(rc, errstr, MAXRBUF);
            LOGF_ERROR("Serial read error: %s.", errstr);
            return false;
        }
        res[1 + nbytes_read] = '\0';

        if (isReply == nullptr || isReply(chomp(res)))
            break;
        LOGF_DEBUG("Not a reply to %s, skipped: <%s>", chomp(cmd).c_str(), chomp(res).c_str());
        if (!readByte())
            return false;
    }

    LOGF_DEBUG("RES <%s>", res);

    // What follows the reply is left to the next command's read above, which discards it knowing where its
    // lines end: flushed here, a log line could be cut and its rest taken for the next reply
    tcflush(PortFD, TCOFLUSH);

    return true;
}
