#ifndef BMCDATAPARSING_H
#define BMCDATAPARSING_H

#include <QDate>
#include <QDataStream>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

#include "SleepLib/card_archive.h"

constexpr int kBmcLegacyWaveformSamples = 25;
constexpr int kBmcExtendedWaveformSamples = 50;

/// @name G2S pressure wave
/// The legacy waveform packet's PressureWave[25] (offset 0x08) is tenths of cmH2O on the
/// G3 and E5, but on the G2S it is a raw sensor reading that fits
/// `raw = 22.84 × cmH2O + 111.5`.  Imported at the G3 scale it read about 20 cmH2O high
/// (GitLab #393).  The fit is empirical: packet-mean samples against the set pressure in
/// the same packet, on two G2S A20 machines.  It has not been checked against a measured
/// reference, so the intercept may be out by about ±0.5 cmH2O.
/// See Notes/Formats/BMC/Luna/BMC_Legacy_File_Formats.md, section 3.2.1.
/// @{

/// Gain of the samples returned by BmcG2sPressureWaveToHundredths(): hundredths of cmH2O.
constexpr double kBmcG2sPressureWaveGain = 0.01;

/// @brief Whether a legacy BMC model stores its pressure wave in G2S raw sensor units
/// @param model Model name as read from the .USR file, e.g. "G2S A20"
/// @return true for the G2S platform, whatever the therapy variant after the name
///
/// Only the G2S A20 has been seen.  Other G2S variants are assumed to share its sensor
/// scale, as the G3 A20, G3 B20A and G3 B25A share theirs.
bool BmcModelUsesG2sPressureWave(const QString& model);

/// @brief Convert one G2S pressure-wave sample to hundredths of cmH2O
/// @param raw Sample as stored in the waveform packet
/// @return Pressure in hundredths of cmH2O, held to the range 0 to the largest qint16.
///         A reading below the sensor's zero (including a zero-filled sample) is
///         returned as 0 rather than as a negative pressure.
qint16 BmcG2sPressureWaveToHundredths(qint16 raw);

/// @}

//The raw I:E Ratio value recorded by BMC is transformed using a function that maps the raw
//value to a percentage: `InspirationPercentage = (100 * rawValue) / (rawValue + 10)`
//Instead of performing this calculation for every sample, since we have discrete values
//from 0 to 100, we create a mapping table of all the valid values and simply do a look up
const static float IERatioLookup[] = {
        0, 9.1, 16.7, 23.1, 28.6, 33.3, 37.5, 41.2, 44.4, 47.4, 50, 52.4, 54.5, 56.5, 58.3,
        60, 61.5, 63, 64.3, 65.5, 66.7, 67.7, 68.8, 69.7, 70.6, 71.4, 72.2, 73, 73.7, 74.4, 75, 75.6, 76.2,
        76.7, 77.3, 77.8, 78.3, 78.7, 79.2, 79.6, 80, 80.4, 80.8, 81.1, 81.5, 81.8, 82.1, 82.5, 82.8, 83.1,
        83.3, 83.6, 83.9, 84.1, 84.4, 84.6, 84.8, 85.1, 85.3, 85.5, 85.7, 85.9, 86.1, 86.3, 86.5, 86.7,
        86.8, 87, 87.2, 87.3, 87.5, 87.7, 87.8, 88, 88.1, 88.2, 88.4, 88.5, 88.6, 88.8, 88.9, 89, 89.1, 89.2,
        89.4, 89.5, 89.6, 89.7, 89.8, 89.9, 90, 90.1, 90.2, 90.3, 90.4, 90.5, 90.6, 90.7, 90.7, 90.8
        };

class MessageItemBase {};

class MessageItem32 : public MessageItemBase
{
public:
    int MessageType;
    quint32 Data;

    MessageItem32(int msgType, quint32 data) : MessageType(msgType), Data(data) {};
};

class MessageItem24 : public MessageItemBase
{
public:
    int MessageType;
    quint8 Data1;
    quint8 Data2;
    quint8 Data3;

    MessageItem24(int msgType, quint16 data1, quint8 data2, quint8 data3) :
        MessageType(msgType),
        Data1(data1),
        Data2(data2),
        Data3(data3)
        {};
};

class MessageItem16 : public MessageItemBase
{
public:
    int MessageType;
    quint16 Data;

    MessageItem16(int msgType, quint16 data) : MessageType(msgType), Data(data) {};
};

class BmcEncodedDate{
public:
    static QDateTime DecodeDate(quint16);
};

enum class BmcRespiratoryEventType
{
    HYP = 0,    ///< Hypopnea the device did not classify by mechanism
    OSA,
    CSA,
    UA,
    PB,         ///< Periodic breathing / Cheyne-Stokes respiration episode
    RERA,       ///< Respiratory Effort Related Arousal
    OH,         ///< Obstructive hypopnea (G3X only; legacy cards never report one)
    CH,         ///< Central hypopnea (G3X only)
    Snore,      ///< Snore, one per snoring breath (G3X only; EVT type 0x0B)
    Unknown
};

class BmcRespiratoryEvent
{
public:
    BmcRespiratoryEventType EventType = BmcRespiratoryEventType::Unknown;
    QDateTime StartTime;
    QDateTime EndTime;
    int DurationSeconds = 0;
};

/// @brief A single flow-limitation point event with a severity grade.
/// Grade 1 = Mild, 2 = Moderate, 3 = Severe.
/// DurationMs is the device-reported breath duration in milliseconds (EVT value2).
class BmcFlowLimitEvent
{
public:
    QDateTime Timestamp;
    int Grade = 0;      ///< 1 = Mild, 2 = Moderate, 3 = Severe
    int DurationMs = 0; ///< Device-reported breath duration in milliseconds (EVT value2)
};

/// @brief A pressure snapshot from an EVT 0x42 record.
/// Used for EVT-only sessions where no waveform packets are present.
class BmcPressureSnapshot
{
public:
    QDateTime Timestamp;
    int EpapHundredths = 0; ///< EPAP in hundredths of cmH2O
    int IpapHundredths = 0; ///< IPAP in hundredths of cmH2O
};

/// @brief The longest plausible single BMC recording session, in minutes (48 hours).
///        Durations beyond this are treated as corrupt and discarded: a bogus value
///        propagates into Session::really_set_last() and makes calcAHIGraph() iterate
///        once per minute of the reported span, freezing the UI and exhausting memory.
constexpr int kBmcMaxSessionDurationMinutes = 48 * 60;

/// @brief The largest backward step between consecutive waveform packets, in seconds,
///        that is still treated as the same recording (a DST "fall back" moves the clock
///        back one hour).  A larger step means the newest data has run into the oldest
///        data of the circular .nnn buffer.
constexpr qint64 kBmcMaxBackwardJumpSecs = 7200;

class BmcUsrSession
{
public:
    QDateTime StartTimestamp;
    QDateTime EndTimestamp;
    /// Session length in minutes as reported by the USR record.  Zero when unknown --
    /// ReadInProgressSession() has no duration field to read, so it must stay 0 rather
    /// than holding indeterminate stack data.
    int DurationMinutes = 0;
    QList<BmcRespiratoryEvent> RespiratoryEvents;

    BmcUsrSession();
    BmcUsrSession(QDataStream*, bool inProgressSession);

    /// The record's 'next session' pointer if it lies after @p pos and within @p usr, else 0.
    static quint32 NextHistoricSessionOffset(const QByteArray& usr, qint64 pos);
    /// True if @p pos holds a historic session header: 0xE1 and a decodable start date.
    static bool IsHistoricSessionStart(const QByteArray& usr, qint64 pos);
    /// Offset of the first session header at or after @p from that also has a valid
    /// 'next session' pointer, or usr.size() if there is none.
    static qint64 FindHistoricSession(const QByteArray& usr, qint64 from);

protected:
    QList<MessageItem32> MessagesOffset45;
    QList<MessageItem32> DataMessages32;
    QList<MessageItem24> DataMessages24;
    QList<MessageItem16> DataMessages16;

    void ReadInProgressSession(QDataStream*);
    void ReadHistoricSession(QDataStream*);
};

class BmcIdxEntry{
public:
    QDateTime Timestamp;
    int Index = 0;
    quint16 StartOffsetPacket = 0;
    quint16 StartFileIndex = 0;
    quint16 NextOffsetPacket = 0;
    bool HasValidNext = false;
    quint8 NextFileIndex = 0xff;

    QDateTime StartWaveformPacketTimestamp;

    BmcIdxEntry();
    BmcIdxEntry(QDataStream*);

    size_t StartOffsetByte();
    QString StartFileExtension();
    size_t NextOffsetByte();
    QString NextFileExtension();

    
};

enum class BmcMode
{
    CPAP = 0,
    AutoCPAP,
    S,
    ST,
    T,
    Titration,
    AutoS
};

enum class BmcMaskType
{
    FullFace = 0,
    Nasal,
    NasalPillow,
    Other
};

/// Air tube sizes as BMC names them: the standard hose is "19mm" on the device's own
/// settings screen and in its manuals (inner diameter), even though it carries the same
/// 22mm end connectors as everyone else's and other vendors call it 22mm.  OSCAR reports
/// what the device reports — see GitLab #256.
enum class BmcAirTubeType
{
    Unheated19mm = 0,
    Unheated15mm,
    Heated19mm,
    Heated15mm
};

class BmcMachineSettings
{
public:
    QDate Timestamp;

    // All fields carry default initializers: ReadDateSession() default-constructs a
    // BmcMachineSettings and uses it as-is when AllMachineSettings is empty, so
    // indeterminate values would otherwise reach the session's Device Settings.
    quint8 Reslex = 0;
    bool ReslexPatient = false;

    quint8 RampTimeMinutes = 0;

    quint8 HumidifierLevel = 0;

    float APAP_IntialP = 0.0f;
    float APAP_MinAPAP = 0.0f;
    float APAP_MaxAPAP = 0.0f;
    quint8 APAP_Sensitivity = 0;
    bool APAP_SmartA = false;


    float CPAP_InitialP = 0.0f;
    float CPAP_TreatP = 0.0f;
    float CPAP_ManualP = 0.0f;
    bool CPAP_SmartC = false;


    float S_InitialEPAP = 0.0f;
    float S_EPAP = 0.0f;
    float S_IPAP = 0.0f;
    /// Trigger sensitivities: -1 = not known (do not display), 0 = Auto, 1–7 = Very Low
    /// through Very High.  Zero is a real value here, so -1 rather than 0 is the "unset"
    /// sentinel — see BMC_ISENS / BMC_ESENS channel options.
    int S_ISENS = -1;
    int S_ESENS = -1;
    /// Rise time in seconds.  -1 = not known; 0 is a real setting ("Min").
    float S_RiseTime = -1.0f;
    float S_TiMin = 0.0f;
    float S_TiMax = 0.0f;
    bool S_BackupRR = false;

    float AutoS_InitialEPAP = 0.0f;
    float AutoS_MinEPAP = 0.0f;
    float AutoS_MinIPAP = 0.0f;
    float AutoS_MaxIPAP = 0.0f;
    /// Fixed pressure support (IPAP − EPAP) in cmH2O.  AutoS is an auto-EPAP mode with
    /// *fixed* PS, so this is a single value rather than a range.  Zero means "not known"
    /// — the legacy BMC parser does not currently populate it.
    float AutoS_PS = 0.0f;

    /// See S_ISENS / S_ESENS: -1 = not known, 0 = Auto, 1–7 = Very Low … Very High.
    int AutoS_ISENS = -1;
    int AutoS_ESENS = -1;
    /// Rise time in **seconds** — the unit BMC_RISE_TIME is declared with.  Float because
    /// the G3X TS block stores it in milliseconds at sub-second resolution (e.g. 300 ms
    /// → 0.3).  -1 = not known; 0 is a real setting ("Min").  Note the legacy IDX parser
    /// puts a 1–4 index here rather than a duration; that predates this field being
    /// decoded properly and is left alone, since no reference readout exists to convert it.
    float AutoS_RiseTime = -1.0f;

    bool AutoS_SmartB = false;



    bool LeakAlert = false;
    bool AutoOn = false;
    bool AutoOff = false;

    /// Pressure response ramp shape: -1 = not known, 1 = Standard, 2 = Soft, 3 = Fast.
    /// Applies to the auto modes (AutoCPAP, AutoS).
    int PresResponse = -1;

    BmcMode Mode = BmcMode::CPAP;

    BmcMaskType MaskType = BmcMaskType::Other;
    BmcAirTubeType AirTubeType = BmcAirTubeType::Unheated19mm;
    /// False when no air-tube field has been decoded for this device.  The G3X TS block
    /// layout has no known air-tube offset, and AirTubeType's zero default ("Normal 19mm")
    /// is a real option — publishing it unconditionally would present a plausible but
    /// unverified value as though it were read from the device.
    bool AirTubeTypeKnown = false;
    int HeatedTubeLevel = 0;

    BmcMachineSettings();
    BmcMachineSettings(QDataStream*);
};

class BmcWaveformCrumb
{
public:
    QString Filepath;
    quint16 FileIndex = 0;
    quint64 ByteOffset = 0;
    quint16 PacketOffset = 0;
    QDateTime Timestamp;
};

class BmcDataLink
{
public:
    BmcUsrSession UsrSession;
    BmcIdxEntry IdxEntry;
    BmcWaveformCrumb WaveformCrumb;
};

#pragma pack(push, 1)
struct BmcWaveformPacketStruct{
    uint16_t Header; //00
    int16_t Offset0x02; //02
    int16_t EPAP; //04
    int16_t IPAP; //06
    int16_t PressureWave[kBmcLegacyWaveformSamples]; //08
    int16_t FlowAbnormality[kBmcLegacyWaveformSamples]; //3a
    int16_t Flow[kBmcLegacyWaveformSamples]; //6c
    int16_t Offset0x9E;
    int16_t Offset0xA0;
    int16_t Offset0xA2;
    int16_t Offset0xA4;
    int16_t Offset0xA6;
    int16_t Offset0xA8;
    int16_t Offset0xAA;
    int16_t Offset0xAC;
    int16_t Offset0xAE;
    int16_t Offset0xB0;
    int16_t Offset0xB2;
    int16_t Offset0xB4;
    int16_t Offset0xB6;
    int16_t Offset0xB8;
    int16_t Offset0xBA;
    int16_t Offset0xBC;
    int16_t Offset0xBE;
    int16_t Offset0xC0;
    int16_t Offset0xC2;
    int16_t Leak; //C4
    int16_t TidalVolume; //C6
    int16_t Offset0xC8;
    int16_t MinuteVentilation; //CA
    int16_t SpO2Pct;      // 0xCC – oxygen saturation %
    int16_t PulseRate;    // 0xCE – pulse rate in beats per minute
    int16_t RespiratoryRate; //D0
    int16_t IERatio; //D2
    int16_t Offset0xD4;
    int16_t Offset0xD6;
    int16_t Offset0xD8;
    int16_t Offset0xDA;
    int16_t Offset0xDC;
    int16_t Offset0xDE;
    int16_t Offset0xE0;
    int16_t Offset0xE2;
    int16_t Offset0xE4;
    int16_t Offset0xE6;
    int16_t Offset0xE8;
    int16_t Offset0xEA;
    int16_t Offset0xEC;
    int16_t Offset0xEE;
    int16_t Offset0xF0;
    int16_t Offset0xF2;
    int16_t Offset0xF4;
    int16_t Offset0xF6;
    uint16_t Year; //F8
    uint8_t Month; //FA
    uint8_t Day;  //FB
    uint8_t Hour;  //FC
    uint8_t Minute;  //FD
    uint8_t Second;  //FE
    uint8_t Terminator;
};
#pragma pack(pop)


class BmcWaveformPacketRaw
{
public:
    QDateTime Timestamp;
    qint16 IPAP;
    qint16 EPAP;
    qint16 PressureWave[kBmcExtendedWaveformSamples];
    qint16 MaskPressure[kBmcExtendedWaveformSamples];
    qint16 FlowAbnormality[kBmcExtendedWaveformSamples];
    qint16 Flow[kBmcExtendedWaveformSamples];
    quint16 Leak;
    qint16 TidalVolume;
    qint16 MinuteVentilation;
    quint16 SpO2Pct;
    quint16 PulseRate;
    quint16 RespiratoryRate;
    qint16 IERatioMapped;
    /// @brief EPAP pressure trend (slow-moving target/smoothed pressure), raw hundredths cmH2O.
    ///        Sourced from waveform packet offset 0x76C. Used by session-start detection.
    quint16 PressureTrend = 0;
};


class BmcWaveformPacket
{
public:
    QDateTime Timestamp;
    float IPAP;
    float EPAP;
    qint16 PressureWave[kBmcExtendedWaveformSamples];
    qint16 MaskPressure[kBmcExtendedWaveformSamples];
    quint16 FlowAbnormality[kBmcExtendedWaveformSamples];
    float Flow[kBmcExtendedWaveformSamples];
    float Leak;
    int TidalVolume;
    float MinuteVentilation;
    quint16 RespiratoryRate;
    float IERatio;
    BmcWaveformPacketRaw Raw;

    BmcWaveformPacket(char* buffer);
};

class BmcMachineInfo
{
public:
    QString SerialNumber;
    QString Model;
    QString FirmwareVersion; ///< User-facing version from .log 0x0420, e.g. "G3-2.11.02.33" or "G3-2.12.54.13".
                             ///< Falls back to IDX 0x0345 SC build string if .log unavailable.
};


//We break the data for an entire day up into actual sessions
//by looking for gaps in the waveform data
class BmcSession{
public:
    QDateTime StartTimestamp;
    QDateTime EndTimestamp;

    QList<BmcWaveformPacket> Waveforms;
    QList<BmcRespiratoryEvent> RespiratoryEvents;
    QList<BmcFlowLimitEvent> FlowLimitEvents;
    QList<BmcPressureSnapshot> PressureSnapshots; ///< From EVT 0x42; populated for EVT-only sessions (waveLen==0).
};

class BmcDateSession
{
public:
    QDateTime StartTime;
    int DurationMinutes = 0;
    BmcMachineInfo MachineInfo;
    BmcMachineSettings MacineSettings;
    QList<BmcRespiratoryEvent> RespiratoryEvents;
    QList<BmcFlowLimitEvent> FlowLimitEvents;
    QList<BmcWaveformPacket> Waveforms;


    QList<BmcSession*> Sessions;
    ~BmcDateSession();
};

/// @brief One record (night) of a BMC card as offered to a CardArchive.
struct BmcArchiveRecord
{
    CardArchiveRecord info;   ///< what the archive needs to know about the record
    int handle = -1;          ///< the parser's own reference to the record; -1 = none
    /// Set by ReadArchiveRecordBytes() when the record's data is there but could not be
    /// read (a ring file would not open, or a read came up short).  That is an error, not
    /// a night without waveform: nothing may be concluded from the empty result.
    bool readFailed = false;
};

/// @brief Capture-rule version of the legacy BMC parser, stored with every archived night.
///
/// Raise it when the logic that decides which waveform packets belong to a night changes
/// (the crumb search, the session window, the wrap handling in ReadWaveformBytes()).
/// Nights archived under a lower number are then captured again from any card that still
/// holds them.  This is not the loader's data version and forces no rebuild.
constexpr int kBmcLegacyCaptureRule = 1;

/// @brief Common interface of the legacy and G3X BMC card parsers.
///
/// Besides importing, a parser serves the night archive (CardArchive, GitLab #380): it
/// lists the card's records, hands over a record's raw waveform packets for storing, and
/// in archive mode reads those packets back from an archive in place of the card's ring
/// files.  The same code path defines a night's packets for import and for the archive,
/// so the two cannot disagree.
class BmcDataParser
{
public:
    virtual ~BmcDataParser() = default;
    virtual void ReadData() = 0;
    virtual BmcMachineInfo ReadMachineInfo() = 0;
    virtual BmcDateSession ReadDateSession(QDate aDate) = 0;
    virtual const QList<BmcDataLink>& GetSessionLinks() const = 0;

    /// @brief Read waveforms from @p archive in place of the card's ring files.
    /// Call before ReadData().  Null (the default) reads the card.  The parser only ever
    /// reads the archive; it never changes it.
    virtual void SetArchive(CardArchive* archive) = 0;

    /// @brief The records (nights) the parsed index describes.  Call after ReadData().
    virtual QList<BmcArchiveRecord> ArchiveRecords() = 0;

    /// @brief A record's waveform packets, in recording order, with the ring's wrap removed.
    /// @param rec     A record from ArchiveRecords(); its first, last, bytes and complete
    ///                fields are filled in from what was read.
    /// @param aborted Polled while reading; may be empty.  A true result abandons the read.
    /// @return The packets, or an empty array if the record has none, the read was abandoned,
    ///         or it failed (then @p rec.readFailed is set).
    virtual QByteArray ReadArchiveRecordBytes(BmcArchiveRecord& rec,
                                              const std::function<bool()>& aborted) = 0;

    /// @brief Size of one waveform packet in bytes.
    virtual int PacketSize() const = 0;

    /// @brief First and last packet times of a buffer of this parser's waveform packets.
    /// @return false if @p packets is empty, not whole packets, or carries no valid times.
    virtual bool DescribePackets(const QByteArray& packets, QDateTime& first, QDateTime& last) const = 0;

    /// @brief The card's file base name (the part its data files share).
    virtual QString BaseName() const = 0;

    /// @brief Names of the index-class files present: the small files that describe every
    ///        night and only grow, which the archive keeps verbatim.
    virtual QStringList IndexFileNames() const = 0;

    /// @brief Earliest and latest date the parsed index describes.
    /// @return false if it describes none.
    virtual bool IndexDateRange(QDate* first, QDate* last) const = 0;

    /// @brief True if @p fileName is one of the card's ring-buffer waveform files, which
    ///        the archive never copies as files.
    virtual bool IsRingFileName(const QString& fileName) const = 0;

    /// @brief Archive mode: dates whose segment is listed in the archive but could not be
    ///        read during this import.  Safe to call while import tasks are running.
    virtual QList<QDate> UnreadableNights() const = 0;
};

class BmcData
    : public BmcDataParser
{
public:
    QList<BmcUsrSession> AllUsrSessions;
    QList<BmcMachineSettings> AllMachineSettings;
    QList<BmcIdxEntry> AllIdxEntries;

    QList<BmcIdxEntry> ValidIdxEntries;

    QList<BmcDataLink> SessionLinks;

    QList<BmcWaveformCrumb> WaveformCrumbs;

    static bool DirectoryHasBmcData(const QString& path);

    BmcData();
    BmcData(const QString& path);

    int ReadDataCount();
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
#ifdef UNITTEST_MODE
    friend class BmcArchiveTests;
#endif

    QString dirPath;
    QString usrFilePath;

    /// Archive to read waveforms from in place of the .nnn ring files; null reads the card.
    CardArchive* archive = nullptr;
    /// Nights whose archive segment failed to read; see UnreadableNights().
    QList<QDate> unreadableNights;
    mutable QMutex unreadableMutex;

    /// Timestamp of the 256-byte waveform packet at @p packet (offsets 0xF8-0xFE).
    static QDateTime PacketTimestamp(const char* packet);
    /// The archive entry holding @p session's waveform, or null if there is none.
    const CardArchiveEntry* ArchiveEntryFor(const BmcUsrSession& session) const;
    // The raw 256-byte packets ReadWaveforms() decodes for a session: read from the card's
    // ring files, or in archive mode from the session's archive segment.
    QByteArray ReadWaveformBytes(BmcDataLink& link, const std::function<bool()>& aborted,
                                 bool* complete = nullptr, bool* readFailed = nullptr);
    /// Opens one .nnn ring file for reading; null if it cannot be opened.  Virtual so that
    /// tests can stand in a file that fails.
    virtual std::unique_ptr<QIODevice> OpenRingFile(const QString& path) const;
    /// Decode a buffer of 256-byte waveform packets.
    static QList<BmcWaveformPacket> DecodeWaveformBytes(const QByteArray& packets);

    static QString ChangeFileExtension(const QString& path, QString newExtensionWithDot);
    static QString GetUsrFilePath(const QString& path);

    // Returns 0xFF if the file begins with a 255-byte legacy tail packet followed by
    // standard 256-byte packets (detected by 0xAAAA at bytes 0xFF–0x100), or 0 for
    // the standard all-256-byte layout.
    static int DetectFileDataOffset(const QString& path);

    QDateTime ReadWaveformPacketTimestamp(const QString& path, quint64 packetStartByte);

    void ReadIdxFile();
    void ReadAllSessions();
    void BuildWaveformCrumbs();
    // Adds a crumb where the circular .nnn buffer wraps from newest to oldest data, so
    // the oldest surviving night is not read from the next fixed-grid crumb (GitLab #337).
    void AddWrapPointCrumb();
    QList<BmcWaveformPacket> ReadWaveforms(BmcDataLink & link);
    void FindValidSessions();
};


#endif // BMCDATAPARSING_H
