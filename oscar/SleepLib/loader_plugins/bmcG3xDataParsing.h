#ifndef BMCG3XDATAPARSING_H
#define BMCG3XDATAPARSING_H

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QMutex>
#include <QString>

#include <memory>
#include <vector>

#include "SleepLib/loader_plugins/bmcDataParsing.h"

/// @brief Capture-rule version of the G3X parser, stored with every archived night.
///
/// Raise it when the logic that decides which waveform bytes belong to a night changes
/// (the IDX range, its unwrapping, the surviving-tail search, CardWaveRanges()).  Nights
/// archived under a lower number are then captured again from any card that still holds
/// them.  This is not the loader's data version and forces no rebuild.
constexpr int kBmcG3xCaptureRule = 1;

class BmcG3xData : public BmcDataParser
{
public:
    BmcG3xData();
    explicit BmcG3xData(const QString& path);

    static bool DirectoryHasBmcG3xData(const QString& path);

    void ReadData() override;
    BmcMachineInfo ReadMachineInfo() override;
    BmcDateSession ReadDateSession(QDate aDate) override;
    const QList<BmcDataLink>& GetSessionLinks() const override;

    void SetArchive(CardArchive* archive) override;
    QList<BmcArchiveRecord> ArchiveRecords() override;
    QByteArray ReadArchiveRecordBytes(BmcArchiveRecord& rec,
                                      const std::function<bool()>& aborted) override;
    int PacketSize() const override;
    bool DescribePackets(const QByteArray& packets, QDateTime& first, QDateTime& last) const override;
    QString BaseName() const override;
    QStringList IndexFileNames() const override;
    bool IndexDateRange(QDate* first, QDate* last) const override;
    bool IsRingFileName(const QString& fileName) const override;
    QList<QDate> UnreadableNights() const override;

protected:
    /// Opens one .00x ring file for reading; null if it cannot be opened.  Virtual so that
    /// tests can stand in a file that fails.
    virtual std::unique_ptr<QIODevice> OpenRingFile(const QString& path) const;

private:
#ifdef UNITTEST_MODE
    friend class BmcArchiveTests;
    friend class BmcG3xSettingsTests;
#endif

    struct G3xDayEntry
    {
        QDate Date;
        QDateTime StartTimestamp;
        QDateTime EndTimestamp;
        /// Waveform range as offsets into the virtual .00x address space.  The ring
        /// wraps, so a night recorded across the wrap point has an end below its start
        /// in the IDX; ParseIdxRecords unwraps it by adding the ring size, so here
        /// WaveEndOffset can exceed the ring.  Map to a file with WaveformFileFor().
        quint64 WaveStartOffset = 0;
        quint64 WaveEndOffset = 0;
        quint32 WaveLength = 0;
        /// True when the start of the night had been overwritten on the card and the
        /// range above was trimmed to the part that survives.
        bool WaveTailOnly = false;
        /// Archive mode: the night's waveform is the archive segment below, and the
        /// range above is [0, Segment.bytes) within it instead of a ring offset.
        bool HasSegment = false;
        CardArchiveEntry Segment;
        quint32 EventStartOffset = 0;
        quint32 EventEndOffset = 0;
        quint32 EventLength = 0;
        quint32 LogStartOffset = 0;
        quint32 LogEndOffset = 0;
        quint32 LogLength = 0;

        int ItDurationSeconds = 0;
        int ItSessionCount = 0;
        int ItPressureMinHundredths = 0;
        int ItPressureMaxHundredths = 0;
        int ItPressureP95Hundredths = 0;
        int ItPressureEPAPHundredths = 0;
        int ItAhiX100 = 0;
        int ItAiX100 = 0;
        int ItHiX100 = 0;
        int ItOaiX100 = 0;
        int ItCaiX100 = 0;
        int ItReraIndexX100 = 0;
        int ItEventTotalCount = 0;
        int ItEventObstructiveCount = 0;
        int ItEventCentralCount = 0;
        int ItEventHypopneaCount = 0;
        int ItEventReraCount = 0;
        int ItEventOtherCount = 0;

        int TsPressureMinHundredths = 0;
        int TsPressureMaxHundredths = 0;

        /// Raw 256-byte TS (therapy settings) block from this day's IDX record, captured
        /// at parse time.  This is the authoritative per-night settings record — it is
        /// what PAP-Link reports from, and unlike the `.set` file it reflects the settings
        /// actually in force on this date rather than the device's current configuration.
        /// Empty when the record carried no TS block.
        QByteArray TsBlock;
    };

    QString dirPath;
    QString idxFilePath;
    QString fileBasePath;

    QList<BmcDataLink> sessionLinks;
    QList<G3xDayEntry> dayEntries;
    BmcMachineInfo machineInfo;

    /// Waveform offset the leak channel is read from (0x52A or 0x568).  Which field a
    /// device populates is a property of its firmware, not of any individual night, so
    /// this is resolved once per device and reused for every day of the import.
    /// -1 means "not yet resolved".
    int leakFieldOffsetCache = -1;

    /// Number of .00x files in the waveform ring, from the IDX header.
    int waveFileCount = 63;

    int ResolveLeakFieldOffset();

    static bool DecodeTsBlock(const QByteArray& ts, BmcMachineSettings& settings);
    bool ApplySetFileSettings(BmcMachineSettings& settings) const;

    bool ResolveIdxFile();
    void ParseMachineInfo(const QByteArray& idxBytes);
    void ParseWaveformRingGeometry(const QByteArray& idxBytes);
    void ParseIdxRecords(const QByteArray& idxBytes);

    /// One stretch of a night's waveform within a single ring file.
    struct WaveRange
    {
        QString path;             ///< the ring file
        qint64 start = 0;         ///< first byte, on a packet boundary
        qint64 end = 0;           ///< end of the stretch (exclusive)
        quint64 virtualBase = 0;  ///< virtual offset of the file's first byte
    };
    /// An open device holding part of a night's waveform, and the bytes of it to read.
    struct WaveSpan
    {
        std::unique_ptr<QIODevice> device;
        qint64 start = 0;
        qint64 end = 0;
        quint64 virtualBase = 0;  ///< for diagnostics: virtual offset of device position 0
    };

    /// Archive to read waveforms from in place of the .00x ring files; null reads the card.
    CardArchive* archive = nullptr;
    /// Nights whose archive segment failed to read; see UnreadableNights().
    mutable QList<QDate> unreadableNights;
    mutable QMutex unreadableMutex;

    std::vector<WaveRange> CardWaveRanges(const G3xDayEntry& entry, qint64* indexedBytes = nullptr) const;
    std::vector<WaveSpan> OpenWaveSpans(const G3xDayEntry& entry, bool* readFailed = nullptr) const;
    const G3xDayEntry* EntryForHandle(int handle) const;

    QString WaveformFileFor(quint64 virtualByteOffset, qint64* localOffset) const;
    quint64 FindFirstPacketInDayWindow(quint64 startOffset, quint64 endOffset, const QDate& date) const;
    QDateTime ReadWaveformPacketTimestamp(quint64 virtualByteOffset) const;
    QList<QDateTime> ReadWaveformPacketTimestamps(quint32 startOffset, quint32 endOffset) const;
    QByteArray ReadVirtualWaveformBytes(quint64 virtualByteOffset, int byteCount) const;

    static bool IsG3xIdxHeader(const QByteArray& headerBytes);
    static QString ReadAscii(const QByteArray& bytes, int offset, int length);
    static quint16 ReadUInt16LE(const QByteArray& bytes, int offset);
    static quint32 ReadUInt32LE(const QByteArray& bytes, int offset);
};

#endif // BMCG3XDATAPARSING_H
