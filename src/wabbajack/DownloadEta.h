#pragma once
#include <QString>
#include <QtGlobal>

namespace solero {

// Rolling estimate of how long a byte-counted transfer has left.
//
// jackify-engine prints its own "20.3MB/s", but that figure is instantaneous and
// swings hard enough that an ETA built on it jumps between "2m" and "40m" between
// redraws. This smooths a rate out of our own readings of the remaining figure and
// stays quiet until there is enough history for the number to mean something.
class EtaEstimator {
public:
    // A "<remaining> bytes still to go" reading taken at elapsedMs since the run began.
    void sample(double remainingBytes, qint64 elapsedMs) {
        if (remainingBytes < 0) return;
        if (m_lastMs < 0) {                       // first reading is the baseline only
            m_lastRemaining = remainingBytes;
            m_lastMs = elapsedMs;
            return;
        }
        const qint64 dt = elapsedMs - m_lastMs;
        if (dt < kMinGapMs) return;               // too close together to divide by

        const double delta = m_lastRemaining - remainingBytes;
        m_lastRemaining = remainingBytes;
        m_lastMs = elapsedMs;

        // Remaining went up: the engine found more work, not negative throughput.
        // Re-baseline and leave the rate alone.
        if (delta < 0) return;

        const double instant = delta / (dt / 1000.0);
        m_rate = (m_rate < 0) ? instant : kAlpha * instant + (1 - kAlpha) * m_rate;
        ++m_accepted;
    }

    // Seconds left at the smoothed rate, or -1 while there is no usable estimate.
    double secondsRemaining() const {
        if (m_accepted < kMinSamples || m_rate <= 0 || m_lastRemaining < 0) return -1;
        return m_lastRemaining / m_rate;
    }

    // Smoothed rate in bytes/sec, or -1.
    double bytesPerSecond() const { return m_rate; }

    void reset() { *this = EtaEstimator(); }

private:
    static constexpr double kAlpha = 0.25;     // weight of each new reading
    static constexpr qint64 kMinGapMs = 1500;  // readings closer than this are noise
    static constexpr int kMinSamples = 3;      // show nothing until the rate settles

    double m_lastRemaining = -1;
    qint64 m_lastMs = -1;
    double m_rate = -1;
    int m_accepted = 0;
};

// "45s", "6m 20s", "2h 5m". Empty for a negative duration. Seconds are dropped past
// an hour and minutes past a day - false precision on a figure this rough.
inline QString formatDuration(double seconds) {
    if (seconds < 0) return {};
    const qint64 s = qint64(seconds + 0.5);
    if (s < 60) return QStringLiteral("%1s").arg(s);
    if (s < 3600) {
        const qint64 m = s / 60, rem = s % 60;
        return rem ? QStringLiteral("%1m %2s").arg(m).arg(rem) : QStringLiteral("%1m").arg(m);
    }
    if (s < 86400) {
        const qint64 h = s / 3600, m = (s % 3600) / 60;
        return m ? QStringLiteral("%1h %2m").arg(h).arg(m) : QStringLiteral("%1h").arg(h);
    }
    const qint64 d = s / 86400, h = (s % 86400) / 3600;
    return h ? QStringLiteral("%1d %2h").arg(d).arg(h) : QStringLiteral("%1d").arg(d);
}

} // namespace solero
