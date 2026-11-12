#include "SfzParser.h"
#include <map>

namespace gp
{

using OpcodeMap = std::map<juce::String, juce::String>;

int sfzNoteNumber (const juce::String& textIn)
{
    auto text = textIn.trim().toLowerCase();
    if (text.isEmpty())
        return -1;

    if (text.containsOnly ("-0123456789"))
        return text.getIntValue();

    static const int pcs[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    auto c = text[0];
    if (c < 'a' || c > 'g')
        return -1;

    int pc = pcs[c - 'a'];
    int i = 1;
    if (text[i] == '#') { ++pc; ++i; }
    else if (text[i] == 'b') { --pc; ++i; }

    auto octaveText = text.substring (i);
    if (octaveText.isEmpty() || ! octaveText.containsOnly ("-0123456789"))
        return -1;

    return (octaveText.getIntValue() + 1) * 12 + pc;
}

static juce::String stripComments (const juce::String& src)
{
    juce::String out;
    out.preallocateBytes ((size_t) src.getNumBytesAsUTF8());

    auto p = src.getCharPointer();
    while (! p.isEmpty())
    {
        auto c = *p;
        if (c == '/' && p[1] == '/')
        {
            while (! p.isEmpty() && *p != '\n') ++p;
            continue;
        }
        if (c == '/' && p[1] == '*')
        {
            p += 2;
            while (! p.isEmpty() && ! (*p == '*' && p[1] == '/')) ++p;
            if (! p.isEmpty()) p += 2;
            continue;
        }
        out << juce::String::charToString (c == '\r' || c == '\t' ? (juce::juce_wchar) ' ' : c);
        ++p;
    }
    return out;
}

/** Splits the text following a header into opcode=value pairs. Values may contain
    spaces (e.g. sample paths) – a value ends where the next "name=" begins. */
static void parseOpcodes (const juce::String& body, OpcodeMap& target)
{
    // find every "identifier=" occurrence
    struct Pos { int nameStart, eq; };
    std::vector<Pos> positions;

    const int len = body.length();
    for (int i = 0; i < len; ++i)
    {
        if (body[i] != '=')
            continue;

        int j = i - 1;
        while (j >= 0 && (juce::CharacterFunctions::isLetterOrDigit (body[j]) || body[j] == '_'))
            --j;

        if (j + 1 < i && (j < 0 || juce::CharacterFunctions::isWhitespace (body[j])))
            positions.push_back ({ j + 1, i });
    }

    for (size_t k = 0; k < positions.size(); ++k)
    {
        auto name = body.substring (positions[k].nameStart, positions[k].eq).trim().toLowerCase();
        int valueEnd = k + 1 < positions.size() ? positions[k + 1].nameStart : len;
        auto value = body.substring (positions[k].eq + 1, valueEnd).trim();

        if (name != "sample")
            value = value.upToFirstOccurrenceOf (" ", false, false);

        target[name] = value;
    }
}

static void applyOpcodes (const OpcodeMap& ops, SfzRegion& r, const juce::String& defaultPath)
{
    auto getF = [&] (const char* k, float& v) { auto it = ops.find (k); if (it != ops.end()) v = it->second.getFloatValue(); };
    auto getI = [&] (const char* k, int& v)   { auto it = ops.find (k); if (it != ops.end()) v = it->second.getIntValue(); };
    auto getN = [&] (const char* k, int& v)
    {
        auto it = ops.find (k);
        if (it != ops.end()) { auto n = sfzNoteNumber (it->second); if (n >= -1) v = n; }
    };

    if (auto it = ops.find ("sample"); it != ops.end())
        r.samplePath = (defaultPath + it->second).replaceCharacter ('\\', '/');

    if (auto it = ops.find ("key"); it != ops.end())
    {
        auto n = sfzNoteNumber (it->second);
        r.loKey = r.hiKey = r.keyCenter = n;
    }

    getN ("lokey", r.loKey);
    getN ("hikey", r.hiKey);
    getN ("pitch_keycenter", r.keyCenter);
    getI ("lovel", r.loVel);
    getI ("hivel", r.hiVel);
    getF ("pitch_keytrack", r.pitchKeytrack);
    getF ("tune", r.tuneCents);
    getI ("transpose", r.transpose);
    getF ("volume", r.volumeDb);
    getF ("amp_veltrack", r.ampVeltrack);
    getF ("ampeg_release", r.releaseTime);
    getF ("rt_decay", r.rtDecay);
    getF ("pan", r.pan);
    getF ("lorand", r.loRand);
    getF ("hirand", r.hiRand);
    getI ("group", r.group);
    getI ("off_by", r.offBy);

    if (auto it = ops.find ("trigger"); it != ops.end())
        r.trigger = it->second.equalsIgnoreCase ("release") ? Trigger::Release : Trigger::Attack;

    for (auto& [name, value] : ops)
    {
        if (name.startsWith ("on_locc") || name.startsWith ("on_hicc"))
        {
            r.ccNumber = name.substring (7).getIntValue();
            if (name.startsWith ("on_locc")) r.ccLo = value.getIntValue();
            else                             r.ccHi = value.getIntValue();
            r.trigger = Trigger::Controller;
        }
    }
}

bool parseSfz (const juce::File& file, SfzInstrument& out, juce::String& error)
{
    out = {};
    out.sfzFile = file;

    if (! file.existsAsFile())
    {
        error = "SFZ file not found: " + file.getFullPathName();
        return false;
    }

    auto text = stripComments (file.loadFileAsString());

    OpcodeMap control, global, master, group;
    juce::String currentHeader;
    juce::String defaultPath;

    // Walk the text header by header.
    int pos = 0;
    const int len = text.length();

    auto flushRegion = [&] (const OpcodeMap& regionOps)
    {
        OpcodeMap merged = global;
        for (auto& kv : master) merged[kv.first] = kv.second;
        for (auto& kv : group)  merged[kv.first] = kv.second;
        for (auto& kv : regionOps) merged[kv.first] = kv.second;

        SfzRegion r;
        applyOpcodes (merged, r, defaultPath);
        if (r.samplePath.isNotEmpty() && r.loKey <= r.hiKey)
            out.regions.push_back (r);
    };

    while (pos < len)
    {
        int open = text.indexOfChar (pos, '<');
        if (open < 0)
            break;
        int close = text.indexOfChar (open, '>');
        if (close < 0)
            break;

        auto header = text.substring (open + 1, close).trim().toLowerCase();
        int next = text.indexOfChar (close, '<');
        if (next < 0) next = len;
        auto body = text.substring (close + 1, next);

        OpcodeMap ops;
        parseOpcodes (body, ops);

        if (header == "control")
        {
            control = ops;
            if (auto it = control.find ("default_path"); it != control.end())
                defaultPath = it->second.replaceCharacter ('\\', '/');
        }
        else if (header == "global") { global = ops; master.clear(); group.clear(); }
        else if (header == "master") { master = ops; group.clear(); }
        else if (header == "group")  { group = ops; }
        else if (header == "region") { flushRegion (ops); }

        pos = next;
    }

    if (out.regions.empty())
    {
        error = "No playable regions in " + file.getFileName();
        return false;
    }

    for (auto& r : out.regions)
        out.samplePaths.addIfNotAlreadyThere (r.samplePath);
    out.samplePaths.sort (true);

    for (auto& r : out.regions)
        r.sampleIndex = out.samplePaths.indexOf (r.samplePath);

    return true;
}

} // namespace gp
