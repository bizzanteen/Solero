#include "PluginFlagEditor.h"
#include "core/Log.h"
#include <QFile>
#include <QFileInfo>

namespace solero {

namespace {

// Little-endian uint32 read from a 4-byte-safe buffer at offset o.
quint32 u32le(const char* b, int o) {
    return  (static_cast<quint32>(static_cast<quint8>(b[o])))
          | (static_cast<quint32>(static_cast<quint8>(b[o + 1])) << 8)
          | (static_cast<quint32>(static_cast<quint8>(b[o + 2])) << 16)
          | (static_cast<quint32>(static_cast<quint8>(b[o + 3])) << 24);
}

// A record/group header is 24 bytes in Skyrim's TES4 format (Oblivion was 20).
constexpr int kHeaderSize = 24;

bool sigIsRecord(const char* s) {
    // Record signatures are 4 ASCII chars in [A-Z0-9]; GRUP is handled separately.
    for (int i = 0; i < 4; ++i) {
        const char c = s[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!ok) return false;
    }
    return true;
}

} // namespace

bool PluginFlagEditor::isTes4(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.read(4);
    return head.size() == 4 && head[0] == 'T' && head[1] == 'E'
        && head[2] == 'S' && head[3] == '4';
}

bool PluginFlagEditor::isLight(const QString& path, bool* ok) {
    if (ok) *ok = false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.read(12);
    if (head.size() < 12) return false;
    if (head[0] != 'T' || head[1] != 'E' || head[2] != 'S' || head[3] != '4')
        return false;
    const quint32 flags = u32le(head.constData(), 8);
    if (ok) *ok = true;
    return (flags & kLightFlag) != 0;
}

EslEligibility PluginFlagEditor::checkEslEligible(const QString& path) {
    EslEligibility r;
    const auto refuse = [&](const QString& why) { r.eligible = false; r.reason = why; return r; };

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return refuse(QStringLiteral("Could not open the plugin file."));
    const qint64 size = f.size();

    // TES4 header
    QByteArray head = f.read(kHeaderSize);
    if (head.size() < kHeaderSize)
        return refuse(QStringLiteral("File is too small to be a valid plugin."));
    if (head[0] != 'T' || head[1] != 'E' || head[2] != 'S' || head[3] != '4')
        return refuse(QStringLiteral("Not a TES4 plugin (missing TES4 header)."));

    const quint32 tes4DataSize = u32le(head.constData(), 4);
    const qint64 tes4End = static_cast<qint64>(kHeaderSize) + tes4DataSize;
    if (tes4End > size)
        return refuse(QStringLiteral("Plugin header is truncated or corrupt."));

    // Count masters (MAST subrecords) in the TES4 data block. New records use the
    // master-index == masterCount; overrides use an index < masterCount.
    const QByteArray tes4Data = f.read(tes4DataSize);
    if (static_cast<quint32>(tes4Data.size()) < tes4DataSize)
        return refuse(QStringLiteral("Plugin header is truncated or corrupt."));
    quint32 masterCount = 0;
    {
        int pos = 0;
        const int n = tes4Data.size();
        const char* d = tes4Data.constData();
        while (pos + 6 <= n) {
            const char* t = d + pos;
            const quint16 sub = static_cast<quint8>(d[pos + 4])
                              | (static_cast<quint16>(static_cast<quint8>(d[pos + 5])) << 8);
            pos += 6;
            if (pos + sub > n) break; // truncated subrecord - stop counting masters
            if (t[0] == 'M' && t[1] == 'A' && t[2] == 'S' && t[3] == 'T') ++masterCount;
            pos += sub;
        }
    }

    // Walk every record, descending into every group
    // Groups are laid out contiguously with their contents immediately after the
    // 24-byte GRUP header, so a linear scan that steps 24 bytes into each group
    // (rather than skipping it) visits every record in the file without needing to
    // track group boundaries. FormIDs live in the record header, so compressed
    // records are read the same as any other.
    qint64 pos = tes4End;
    int newRecords = 0;
    while (pos + kHeaderSize <= size) {
        if (!f.seek(pos)) return refuse(QStringLiteral("Read error while scanning records."));
        const QByteArray h = f.read(kHeaderSize);
        if (h.size() < kHeaderSize)
            return refuse(QStringLiteral("Plugin is truncated (record header past EOF)."));
        const char* s = h.constData();

        if (s[0] == 'G' && s[1] == 'R' && s[2] == 'U' && s[3] == 'P') {
            const quint32 groupSize = u32le(s, 4);
            if (groupSize < static_cast<quint32>(kHeaderSize))
                return refuse(QStringLiteral("Corrupt group header (implausible size)."));
            pos += kHeaderSize; // descend into the group
            continue;
        }

        if (!sigIsRecord(s))
            return refuse(QStringLiteral("Unrecognized record signature - cannot safely analyze."));

        const quint32 dataSize = u32le(s, 4);
        const quint32 formId   = u32le(s, 12);
        const qint64 recEnd = pos + kHeaderSize + static_cast<qint64>(dataSize);
        if (recEnd > size)
            return refuse(QStringLiteral("Record runs past end of file - corrupt plugin."));

        const quint32 masterIndex = (formId >> 24) & 0xFFu;
        const quint32 objectIndex = formId & 0x00FFFFFFu;

        if (masterIndex < masterCount) {
            // Override of a master's record - not introduced by this plugin.
        } else if (masterIndex == masterCount) {
            // A new record introduced by this plugin: it must fit the ESL range.
            ++newRecords;
            if (objectIndex > kEslMaxObjectIndex)
                return refuse(QStringLiteral(
                    "Not ESL-eligible: a new record (FormID 0x%1) has object index "
                    "0x%2, beyond the ESL limit of 0x%3.")
                    .arg(formId, 8, 16, QLatin1Char('0'))
                    .arg(objectIndex, 0, 16)
                    .arg(kEslMaxObjectIndex, 0, 16));
        } else {
            // FormID references a master index that doesn't exist - fail safe.
            return refuse(QStringLiteral(
                "Not ESL-eligible: record FormID 0x%1 references an undefined master index.")
                .arg(formId, 8, 16, QLatin1Char('0')));
        }

        pos = recEnd;
    }

    if (newRecords > kEslMaxNewRecords)
        return refuse(QStringLiteral(
            "Not ESL-eligible: %1 new records exceeds the ESL limit of %2.")
            .arg(newRecords).arg(kEslMaxNewRecords));

    r.newRecordCount = newRecords;
    r.eligible = true;
    return r;
}

bool PluginFlagEditor::setLightFlag(const QString& path, bool set, QString* error) {
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        qCWarning(lcInstall) << "PluginFlagEditor: refusing to edit" << path << "-" << why;
        return false;
    };

    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
        return fail(QStringLiteral("The plugin file does not exist."));
    if (!isTes4(path))
        return fail(QStringLiteral("Not a TES4 plugin - refusing to edit the header."));

    // Hard safety gate: never SET the flag on an ineligible plugin.
    if (set) {
        const EslEligibility elig = checkEslEligible(path);
        if (!elig.eligible)
            return fail(elig.reason.isEmpty()
                ? QStringLiteral("Plugin is not ESL-eligible.") : elig.reason);
    }

    // Read the current flags first so a set/clear that is already a no-op skips the
    // (destructive-looking) backup + write entirely.
    bool ok = false;
    const bool already = isLight(path, &ok);
    if (!ok) return fail(QStringLiteral("Could not read the plugin header."));
    if (already == set) return true; // nothing to do

    // Always back up before writing. Overwrite a stale backup so the copy reflects
    // the file's state immediately before this edit.
    const QString backup = path + QStringLiteral(".bak-solero");
    if (QFile::exists(backup) && !QFile::remove(backup))
        return fail(QStringLiteral("Could not replace the existing backup file."));
    if (!QFile::copy(path, backup))
        return fail(QStringLiteral("Could not create a backup before editing."));

    QFile f(path);
    if (!f.open(QIODevice::ReadWrite))
        return fail(QStringLiteral("Could not open the plugin for writing."));
    if (!f.seek(8)) return fail(QStringLiteral("Could not seek to the flags field."));
    char buf[4];
    if (f.read(buf, 4) != 4) return fail(QStringLiteral("Could not read the flags field."));
    quint32 flags = u32le(buf, 0);
    if (set) flags |= kLightFlag; else flags &= ~kLightFlag;
    buf[0] = static_cast<char>(flags & 0xFF);
    buf[1] = static_cast<char>((flags >> 8) & 0xFF);
    buf[2] = static_cast<char>((flags >> 16) & 0xFF);
    buf[3] = static_cast<char>((flags >> 24) & 0xFF);
    if (!f.seek(8)) return fail(QStringLiteral("Could not seek to the flags field."));
    if (f.write(buf, 4) != 4) return fail(QStringLiteral("Could not write the flags field."));
    f.flush();
    f.close();

    qCInfo(lcInstall) << "PluginFlagEditor:" << (set ? "set" : "cleared")
                      << "ESL flag on" << path << "(backup at" << backup << ")";
    return true;
}

} // namespace solero
