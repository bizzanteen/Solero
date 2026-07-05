#include "SaveFile.h"
#include "core/Log.h"
#include <QFile>
#include <QSet>
#include <QtEndian>
#include <cstring>

#ifdef SOLERO_HAVE_ZLIB
#include <zlib.h>
#endif
#ifdef SOLERO_HAVE_LZ4
#include <lz4.h>
#endif

namespace solero {

namespace {

// The magic that prefixes every Skyrim savegame, 13 bytes, no terminator.
constexpr char kMagic[] = "TESV_SAVEGAME";
constexpr int  kMagicLen = 13;

// Cap the buffer we are willing to allocate for a decompressed body, so a bogus
// (corrupt) "uncompressed length" field can't drive a huge allocation. The plugin
// list sits at the very front of the body, so a sane cap never truncates it.
constexpr quint32 kMaxDecompressedBytes = 128u * 1024u * 1024u; // 128 MiB

// A bounds-checked forward reader over an in-memory byte buffer. Every accessor
// validates the read against the remaining bytes; on overrun it latches an error
// flag and returns a zero/empty value so callers can bail without ever reading
// out of range. All multi-byte integers are little-endian (the .ess on-disk form).
class Reader {
public:
    Reader(const char* data, int size) : m_data(data), m_size(size) {}

    bool ok() const { return !m_error; }
    int  pos() const { return m_pos; }
    void seek(int p) { if (p < 0 || p > m_size) m_error = true; else m_pos = p; }

    quint8 u8() {
        if (!ensure(1)) return 0;
        return static_cast<quint8>(m_data[m_pos++]);
    }
    quint16 u16() {
        if (!ensure(2)) return 0;
        quint16 v = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(m_data + m_pos));
        m_pos += 2;
        return v;
    }
    quint32 u32() {
        if (!ensure(4)) return 0;
        quint32 v = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(m_data + m_pos));
        m_pos += 4;
        return v;
    }
    quint64 u64() {
        if (!ensure(8)) return 0;
        quint64 v = qFromLittleEndian<quint64>(
            reinterpret_cast<const uchar*>(m_data + m_pos));
        m_pos += 8;
        return v;
    }
    void skip(int n) { if (n < 0 || !ensure(n)) return; m_pos += n; }

    // A .ess wstring: uint16 length prefix + that many bytes (no terminator).
    // Names use the Windows codepage; Latin-1 is a safe, lossless-for-ASCII decode
    // (matching PluginScanner's master-name handling).
    QString wstring() {
        const quint16 len = u16();
        if (!ensure(len)) return {};
        QString s = QString::fromLatin1(m_data + m_pos, len);
        m_pos += len;
        return s;
    }

    QByteArray bytes(int n) {
        if (n < 0 || !ensure(n)) return {};
        QByteArray b(m_data + m_pos, n);
        m_pos += n;
        return b;
    }

private:
    bool ensure(int n) {
        if (m_error) return false;
        if (n < 0 || m_pos + n > m_size) { m_error = true; return false; }
        return true;
    }
    const char* m_data;
    int m_size;
    int m_pos = 0;
    bool m_error = false;
};

// Read the plugin-info block that begins a save's body (compressed-and-decompressed
// or raw): formVersion u8, pluginInfoSize u32, pluginCount u8, then N wstrings.
// Returns true and fills `out` when the whole block reads cleanly.
bool readPluginBlock(const char* data, int size, QStringList& out) {
    Reader r(data, size);
    r.u8();               // formVersion (unused here)
    r.u32();              // pluginInfoSize (informational; we walk the strings directly)
    const quint8 count = r.u8();
    QStringList plugins;
    for (quint8 i = 0; i < count; ++i) {
        const QString name = r.wstring();
        if (!r.ok()) return false;
        plugins << name;
    }
    if (!r.ok()) return false;
    out = plugins;
    return true;
}

// Decompress `compressed` (compressionType 1=zlib, 2=LZ4) into a buffer of the
// declared uncompressed length. Returns an empty array when the decompressor is
// not linked in or the data is invalid - the caller then degrades gracefully.
QByteArray decompressBody(quint16 type, const QByteArray& compressed,
                          quint32 uncompressedLen) {
    if (uncompressedLen == 0 || uncompressedLen > kMaxDecompressedBytes)
        return {};
    QByteArray out;
    out.resize(static_cast<int>(uncompressedLen));

    if (type == 1) { // zlib
#ifdef SOLERO_HAVE_ZLIB
        uLongf destLen = uncompressedLen;
        const int rc = uncompress(reinterpret_cast<Bytef*>(out.data()), &destLen,
                                  reinterpret_cast<const Bytef*>(compressed.constData()),
                                  static_cast<uLong>(compressed.size()));
        if (rc != Z_OK) return {};
        out.resize(static_cast<int>(destLen));
        return out;
#else
        return {};
#endif
    }
    if (type == 2) { // LZ4 (block format, as Skyrim SE writes it)
#ifdef SOLERO_HAVE_LZ4
        const int n = LZ4_decompress_safe(compressed.constData(), out.data(),
                                          compressed.size(),
                                          static_cast<int>(uncompressedLen));
        if (n < 0) return {};
        out.resize(n);
        return out;
#else
        return {};
#endif
    }
    return {};
}

} // namespace

SaveHeader parseSaveHeader(const QString& path) {
    SaveHeader h;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return h;
    // Saves are a few MB; read fully so the (post-screenshot) plugin block is in
    // range. read-only: the file is opened ReadOnly and never modified.
    const QByteArray data = f.readAll();
    f.close();

    const char* d = data.constData();
    const int n = data.size();
    if (n < kMagicLen + 4) return h;
    if (std::memcmp(d, kMagic, kMagicLen) != 0) return h;

    Reader r(d, n);
    r.skip(kMagicLen);
    const quint32 headerSize = r.u32();      // size of the header block that follows
    const int headerBlockStart = r.pos();    // == kMagicLen + 4

    // Parse the header fields. The screenshot begins immediately after the header
    // block (headerSize bytes), which we also use as an authoritative offset below.
    h.version    = r.u32();
    h.saveNumber = r.u32();
    h.characterName = r.wstring();
    h.level      = r.u32();
    h.location   = r.wstring();
    h.gameDate   = r.wstring();
    h.raceEditorId = r.wstring();
    h.sex        = r.u16();
    r.skip(4);                               // playerCurExp   (f32, unused)
    r.skip(4);                               // playerLvlUpExp (f32, unused)
    h.playtimeOrFiletime = r.u64();          // Windows FILETIME
    h.screenshotWidth  = r.u32();
    h.screenshotHeight = r.u32();
    if (h.version >= 12)                      // SSE adds a compression-type field
        h.compressionType = r.u16();
    if (!r.ok()) return h;                    // header didn't parse cleanly

    // Locate the screenshot from the authoritative header-block size (robust to any
    // field we mis-sized) and bound it against the pixel dimensions.
    const qint64 screenshotStart =
        static_cast<qint64>(headerBlockStart) + static_cast<qint64>(headerSize);
    const int bpp = (h.version >= 12) ? 4 : 3; // SSE screenshots are RGBA, LE are RGB
    const qint64 shotBytes = static_cast<qint64>(h.screenshotWidth)
                           * static_cast<qint64>(h.screenshotHeight)
                           * static_cast<qint64>(bpp);
    if (screenshotStart < headerBlockStart || screenshotStart > n
        || shotBytes < 0 || screenshotStart + shotBytes > n)
        return h;                             // dimensions/offset out of range

    h.screenshotBytesPerPixel = bpp;
    h.screenshotRgb = data.mid(static_cast<int>(screenshotStart),
                               static_cast<int>(shotBytes));

    // Everything above (name/level/location/date/thumbnail) is the guaranteed value
    // and parsed cleanly - mark the header valid regardless of the plugin block.
    h.ok = true;

    // Plugin list. For uncompressed saves (LE, or SSE compressionType 0) it follows
    // the screenshot directly. For compressed SSE saves it is inside the compressed
    // body, which we decompress only when a matching decompressor is linked in.
    const qint64 bodyStart = screenshotStart + shotBytes;
    if (bodyStart > n) return h;

    if (h.version >= 12 && h.compressionType != 0) {
        Reader br(d, n);
        br.seek(static_cast<int>(bodyStart));
        const quint32 uncompressedLen = br.u32();
        const quint32 compressedLen   = br.u32();
        const QByteArray compressed   = br.bytes(static_cast<int>(compressedLen));
        if (!br.ok()) return h;               // truncated compressed block - degrade
        const QByteArray body = decompressBody(h.compressionType, compressed, uncompressedLen);
        if (body.isEmpty()) {
            qCDebug(lcApp) << "SaveFile: compressed body (type" << h.compressionType
                           << ") not decoded for" << path;
            return h;                         // pluginsReadable stays false
        }
        QStringList plugins;
        if (readPluginBlock(body.constData(), body.size(), plugins)) {
            h.plugins = plugins;
            h.pluginsReadable = true;
        }
        return h;
    }

    // Uncompressed body - read the plugin block in place.
    QStringList plugins;
    if (readPluginBlock(d + static_cast<int>(bodyStart),
                        n - static_cast<int>(bodyStart), plugins)) {
        h.plugins = plugins;
        h.pluginsReadable = true;
    }
    return h;
}

QStringList missingPlugins(const SaveHeader& save, const QStringList& loadOrder) {
    if (!save.pluginsReadable || save.plugins.isEmpty()) return {};
    QSet<QString> present;
    present.reserve(loadOrder.size());
    for (const QString& p : loadOrder)
        present.insert(p.toLower());
    QStringList missing;
    for (const QString& p : save.plugins)
        if (!present.contains(p.toLower()))
            missing << p;
    return missing;
}

} // namespace solero
