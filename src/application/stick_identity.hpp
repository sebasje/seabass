#pragma once

#include <cstdint>
#include <string>

namespace seabass::application
{

// Who a stick *is*, as opposed to where it currently sits (devicePath and
// mountPoint are reassigned on every replug). Two uses:
//
//  - libraryId(): the key for a library's edit-lock cookie and for "is this
//    the same library" questions. Same rule as StickHardwareInfo::
//    stickIdentifier (filesystem UUID, else label + capacity), so the
//    backup archive's manifest identifier and the lock cookie agree.
//  - isSameStick(): "the exact same physical stick is back", for the
//    stick-removed-while-editing dialog. A content fingerprint is
//    deliberately NOT part of this: the point is to notice a *different*
//    stick that happens to carry the same library.
//
// Every field is best-effort and may be empty; strength() says how much
// the answer can be trusted, so the UI can say "verified by serial number"
// vs "matched by label and size only".
struct StickIdentity
{
    std::string hardwareSerial;   // USB device serial (udev ID_SERIAL_SHORT, STORAGE_DEVICE_DESCRIPTOR); "" if unknown
    std::string filesystemUuid;   // ID_FS_UUID / Windows volume serial as 8 hex digits; "" if none
    std::string label;
    std::uint64_t capacityBytes = 0;

    enum class Strength {
        Hardware,    // a hardware serial is known
        Filesystem,  // only the filesystem UUID is known
        Weak,        // label + capacity only
        None,        // nothing at all (no label either)
    };

    Strength strength() const
    {
        if (!hardwareSerial.empty()) {
            return Strength::Hardware;
        }
        if (!filesystemUuid.empty()) {
            return Strength::Filesystem;
        }
        if (!label.empty()) {
            return Strength::Weak;
        }
        return Strength::None;
    }

    static const char *strengthName(Strength strength)
    {
        switch (strength) {
        case Strength::Hardware:
            return "hardware";
        case Strength::Filesystem:
            return "filesystem";
        case Strength::Weak:
            return "weak";
        case Strength::None:
            return "none";
        }
        return "none";
    }

    // Never empty for a stick that has at least a label; empty only when
    // there is genuinely nothing to key on (a blank, unlabelled drive).
    std::string libraryId() const
    {
        if (!filesystemUuid.empty()) {
            return filesystemUuid;
        }
        if (!label.empty()) {
            return label + "-" + std::to_string(capacityBytes);
        }
        return {};
    }

    // The "exact same stick" rule, strongest evidence first:
    //  1. both sides know a hardware serial: the serials must match, and
    //     if both also know a filesystem UUID those must match too (a
    //     reformatted stick is not "the same stick" for editing purposes:
    //     whatever was on it is gone).
    //  2. otherwise both sides know a filesystem UUID: those must match.
    //  3. otherwise label and capacity must match (weak; the caller should
    //     say so).
    bool isSameStick(const StickIdentity &other) const
    {
        if (!hardwareSerial.empty() && !other.hardwareSerial.empty()) {
            if (hardwareSerial != other.hardwareSerial) {
                return false;
            }
            if (!filesystemUuid.empty() && !other.filesystemUuid.empty()) {
                return filesystemUuid == other.filesystemUuid;
            }
            return true;
        }
        if (!filesystemUuid.empty() && !other.filesystemUuid.empty()) {
            return filesystemUuid == other.filesystemUuid;
        }
        if (label.empty() || other.label.empty()) {
            return false;
        }
        return label == other.label && capacityBytes == other.capacityBytes;
    }

    // A library id as a safe file name: letters, digits, '-', '_' and '.'
    // survive; everything else becomes '_'. Never empty for a non-empty
    // input.
    static std::string sanitizeForFileName(const std::string &id)
    {
        std::string out;
        out.reserve(id.size());
        for (unsigned char c : id) {
            bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
                || c == '_' || c == '.';
            out.push_back(keep ? static_cast<char>(c) : '_');
        }
        if (out == "." || out == "..") {
            out = "_" + out;
        }
        return out;
    }
};

}  // namespace seabass::application
