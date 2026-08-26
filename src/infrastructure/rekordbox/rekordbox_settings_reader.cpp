#include "infrastructure/rekordbox/rekordbox_settings_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>

namespace djconvert::infrastructure::rekordbox
{

namespace
{

// Header is always len_strings(4) + brand(32) + software(32) + version(32)
// + len_data(4) = 104 bytes, regardless of file type (the strings are
// fixed-width, padded with zeros).
constexpr size_t HeaderSize = 104;
constexpr size_t FooterSize = 4;  // checksum(2) + unknown(2)

std::string decodeEnum(uint8_t value, std::initializer_list<std::pair<uint8_t, const char *>> mapping)
{
    for (const auto &[v, name] : mapping) {
        if (v == value) {
            return name;
        }
    }
    return "unknown (0x" + std::to_string(value) + ")";
}

std::vector<std::pair<std::string, std::string>> decodeMySettingBody(const uint8_t *b)
{
    return {
        {"On-air display", decodeEnum(b[8], {{0x80, "off"}, {0x81, "on"}})},
        {"LCD brightness", decodeEnum(b[9], {{0x81, "1"}, {0x82, "2"}, {0x83, "3"}, {0x84, "4"}, {0x85, "5"}})},
        {"Quantize", decodeEnum(b[10], {{0x80, "off"}, {0x81, "on"}})},
        {"Auto cue level", decodeEnum(b[11], {{0x80, "-36dB"},
                                               {0x81, "-42dB"},
                                               {0x82, "-48dB"},
                                               {0x83, "-54dB"},
                                               {0x84, "-60dB"},
                                               {0x85, "-66dB"},
                                               {0x86, "-72dB"},
                                               {0x87, "-78dB"},
                                               {0x88, "memory"}})},
        {"Language", decodeEnum(b[12], {{0x81, "English"},
                                         {0x82, "French"},
                                         {0x83, "German"},
                                         {0x84, "Italian"},
                                         {0x85, "Dutch"},
                                         {0x86, "Spanish"},
                                         {0x87, "Russian"},
                                         {0x88, "Korean"},
                                         {0x89, "Chinese (simplified)"},
                                         {0x8A, "Chinese (traditional)"},
                                         {0x8B, "Japanese"},
                                         {0x8C, "Portuguese"},
                                         {0x8D, "Swedish"},
                                         {0x8E, "Czech"},
                                         {0x8F, "Hungarian"},
                                         {0x90, "Danish"},
                                         {0x91, "Greek"},
                                         {0x92, "Turkish"}})},
        {"Jog ring brightness", decodeEnum(b[14], {{0x80, "off"}, {0x81, "dark"}, {0x82, "bright"}})},
        {"Jog ring indicator", decodeEnum(b[15], {{0x80, "off"}, {0x81, "on"}})},
        {"Slip flashing", decodeEnum(b[16], {{0x80, "off"}, {0x81, "on"}})},
        {"Disc slot illumination", decodeEnum(b[20], {{0x80, "off"}, {0x81, "dark"}, {0x82, "bright"}})},
        {"Eject lock", decodeEnum(b[21], {{0x80, "unlock"}, {0x81, "lock"}})},
        {"Sync", decodeEnum(b[22], {{0x80, "off"}, {0x81, "on"}})},
        {"Play mode", decodeEnum(b[23], {{0x80, "continue"}, {0x81, "single"}})},
        {"Quantize beat value",
         decodeEnum(b[24], {{0x80, "1"}, {0x81, "1/2"}, {0x82, "1/4"}, {0x83, "1/8"}})},
        {"Hot cue autoload", decodeEnum(b[25], {{0x80, "off"}, {0x81, "on"}, {0x82, "rekordbox setting"}})},
        {"Hot cue color", decodeEnum(b[26], {{0x80, "off"}, {0x81, "on"}})},
        {"Needle lock", decodeEnum(b[29], {{0x80, "unlock"}, {0x81, "lock"}})},
        {"Time mode", decodeEnum(b[32], {{0x80, "elapsed"}, {0x81, "remaining"}})},
        {"Jog mode", decodeEnum(b[33], {{0x80, "CDJ"}, {0x81, "vinyl"}})},
        {"Auto cue", decodeEnum(b[34], {{0x80, "off"}, {0x81, "on"}})},
        {"Master tempo", decodeEnum(b[35], {{0x80, "off"}, {0x81, "on"}})},
        {"Tempo range", decodeEnum(b[36], {{0x80, "±6%"}, {0x81, "±10%"}, {0x82, "±16%"}, {0x83, "wide"}})},
        {"Phase meter", decodeEnum(b[37], {{0x80, "type 1"}, {0x81, "type 2"}})},
    };
}

std::vector<std::pair<std::string, std::string>> decodeMySetting2Body(const uint8_t *b)
{
    return {
        {"Vinyl speed adjust", decodeEnum(b[0], {{0x80, "touch & release"}, {0x81, "touch"}, {0x82, "release"}})},
        {"Jog display mode", decodeEnum(b[1], {{0x80, "auto"}, {0x81, "info"}, {0x82, "simple"}, {0x83, "artwork"}})},
        {"Pad button brightness", decodeEnum(b[2], {{0x81, "1"}, {0x82, "2"}, {0x83, "3"}, {0x84, "4"}})},
        {"Jog LCD brightness", decodeEnum(b[3], {{0x81, "1"}, {0x82, "2"}, {0x83, "3"}, {0x84, "4"}, {0x85, "5"}})},
        {"Waveform divisions", decodeEnum(b[4], {{0x80, "time scale"}, {0x81, "phrase"}})},
        {"Waveform", decodeEnum(b[10], {{0x80, "waveform"}, {0x81, "phase meter"}})},
        {"Beat jump beat value", decodeEnum(b[12], {{0x80, "1/2"},
                                                      {0x81, "1"},
                                                      {0x82, "2"},
                                                      {0x83, "4"},
                                                      {0x84, "8"},
                                                      {0x85, "16"},
                                                      {0x86, "32"},
                                                      {0x87, "64"}})},
    };
}

std::vector<std::pair<std::string, std::string>> decodeDjmMySettingBody(const uint8_t *b)
{
    return {
        {"Channel fader curve", decodeEnum(b[12], {{0x80, "steep top"}, {0x81, "linear"}, {0x82, "steep bottom"}})},
        {"Crossfader curve", decodeEnum(b[13], {{0x80, "constant"}, {0x81, "slow cut"}, {0x82, "fast cut"}})},
        {"Headphones pre EQ", decodeEnum(b[14], {{0x80, "post EQ"}, {0x81, "pre EQ"}})},
        {"Headphones mono split", decodeEnum(b[15], {{0x80, "stereo"}, {0x81, "mono split"}})},
        {"Beat FX quantize", decodeEnum(b[16], {{0x80, "off"}, {0x81, "on"}})},
        {"Mic low cut", decodeEnum(b[17], {{0x80, "off"}, {0x81, "on"}})},
        {"Talk over mode", decodeEnum(b[18], {{0x80, "advanced"}, {0x81, "normal"}})},
        {"Talk over level",
         decodeEnum(b[19], {{0x80, "-24dB"}, {0x81, "-18dB"}, {0x82, "-12dB"}, {0x83, "-6dB"}})},
        {"MIDI channel", decodeEnum(b[20], {{0x80, "1"},
                                             {0x81, "2"},
                                             {0x82, "3"},
                                             {0x83, "4"},
                                             {0x84, "5"},
                                             {0x85, "6"},
                                             {0x86, "7"},
                                             {0x87, "8"},
                                             {0x88, "9"},
                                             {0x89, "10"},
                                             {0x8A, "11"},
                                             {0x8B, "12"},
                                             {0x8C, "13"},
                                             {0x8D, "14"},
                                             {0x8E, "15"},
                                             {0x8F, "16"}})},
        {"MIDI button type", decodeEnum(b[21], {{0x80, "toggle"}, {0x81, "trigger"}})},
        {"Display brightness",
         decodeEnum(b[22], {{0x80, "white"}, {0x81, "1"}, {0x82, "2"}, {0x83, "3"}, {0x84, "4"}, {0x85, "5"}})},
        {"Indicator brightness", decodeEnum(b[23], {{0x80, "1"}, {0x81, "2"}, {0x82, "3"}})},
        {"Channel fader curve (long)",
         decodeEnum(b[24], {{0x80, "exponential"}, {0x81, "smooth"}, {0x82, "linear"}})},
    };
}

// Reads one settings file if it exists and is the expected size, decoding
// its body with decodeBody. Returns nullopt (not an exception) for a
// missing or unexpectedly-sized file -- this is supplementary display
// data, never worth failing over.
template<typename DecodeBody>
std::optional<SettingsFile> readOne(const std::string &path, const std::string &fileName, const std::string &title,
                                     size_t expectedTotalSize, DecodeBody &&decodeBody)
{
    std::ifstream ifs(path, std::ifstream::binary);
    if (!ifs.is_open()) {
        return std::nullopt;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (data.size() != expectedTotalSize) {
        return std::nullopt;
    }

    SettingsFile result;
    result.fileName = fileName;
    result.title = title;
    result.fields = decodeBody(data.data() + HeaderSize);
    return result;
}

}  // namespace

std::vector<SettingsFile> readDeviceSettings(const std::string &pioneerRoot)
{
    std::vector<SettingsFile> results;

    if (auto f = readOne(pioneerRoot + "/MYSETTING.DAT", "MYSETTING.DAT", "Player settings",
                          HeaderSize + 40 + FooterSize, decodeMySettingBody)) {
        results.push_back(std::move(*f));
    }
    if (auto f = readOne(pioneerRoot + "/MYSETTING2.DAT", "MYSETTING2.DAT", "Player settings (2)",
                          HeaderSize + 40 + FooterSize, decodeMySetting2Body)) {
        results.push_back(std::move(*f));
    }
    if (auto f = readOne(pioneerRoot + "/DJMMYSETTING.DAT", "DJMMYSETTING.DAT", "Mixer settings",
                          HeaderSize + 52 + FooterSize, decodeDjmMySettingBody)) {
        results.push_back(std::move(*f));
    }

    return results;
}

}  // namespace djconvert::infrastructure::rekordbox
