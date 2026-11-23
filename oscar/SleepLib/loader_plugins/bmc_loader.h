#ifndef BMCLOADER_H
#define BMCLOADER_H

#include <QVector>
#include <QFile>
#include <memory>
#include "SleepLib/machine.h" // Base class: MachineLoader
#include "SleepLib/machine_loader.h"
#include "SleepLib/profiles.h"
#include "SleepLib/loader_plugins/bmcDataParsing.h"



class EventList;

const int bmc_version = 1;
const QString bmc_class_name = "BMC";

class BmcLoader;
class BmcDataLink;
class BmcDataParser;

/*! \struct BmcSessionSpan
    \brief One session's time span and whether it holds waveform, for BmcPlanNightReplacement().
 */
struct BmcSessionSpan {
    SessionID id = 0;           //!< OSCAR session id
    qint64 startMs = 0;         //!< first moment, ms since the epoch
    qint64 endMs = 0;           //!< last moment, ms since the epoch
    bool hasWaveform = false;   //!< true when the session holds flow waveform
};

/*! \struct BmcReplacementPlan
    \brief What the import of one night leaves alone. Empty for an ordinary import.
 */
struct BmcReplacementPlan {
    QSet<SessionID> keepStored;     //!< stored sessions that are not removed
    QSet<SessionID> skipIncoming;   //!< sessions on the card that are not imported
};

/*! \brief Decides whether a night on the card may replace what is stored for the same hours.

    An import re-reads the last imported day and replaces what is stored for it. That is
    right when the card still holds the night's waveform. A card that was formatted has the
    device's recent nights written back to it as events only; importing one of those must
    not replace a stored night that has waveform (GitLab #419).

    When any session in \a incoming has waveform the plan is empty and the night is replaced
    as before. Otherwise every session in \a stored that has waveform is kept, and each
    session in \a incoming that overlaps one of them is skipped. A session on the card that
    overlaps none of them (a later one, recorded after the stored ones) is still imported.

    \param incoming The night's sessions as read from the card.
    \param stored   The device's stored sessions around the same time.
 */
BmcReplacementPlan BmcPlanNightReplacement(const QList<BmcSessionSpan> & incoming,
                                           const QList<BmcSessionSpan> & stored);

class BmcLoaderTask: public ImportTask
{
public:
    BmcLoaderTask(BmcLoader* machineLoader, Machine* machine, BmcDataParser* bmcData, BmcDataLink *dataLink, int totalLinkCount, int currentLinkIdx):
        bmcLoader(machineLoader),
        mach(machine),
        bmc(bmcData),
        bmcLink(dataLink),
        totalLinksToImport(totalLinkCount),
        currentLinkIndex(currentLinkIdx)
    {}

    BmcLoader* bmcLoader;
    Machine* mach;
    BmcDataParser* bmc;
    BmcDataLink* bmcLink;
    int totalLinksToImport;
    int currentLinkIndex;

    virtual void run();
};



class BmcLoader : public CPAPLoader
{    
public:
    BmcLoader();

    void setSessionMachineSettings(BmcDateSession*, Session*);
    void setSessionRespiratoryEvents(BmcSession*, Session*);
    void setSessionWaveforms(BmcSession*, Session*);
        

    virtual bool Detect(const QString & path);

    virtual MachineInfo PeekInfo(const QString & path);

    virtual void initChannels();

    virtual int Open(const QString &);
    /// Says, for the rebuild prompts, which nights with detailed graphs this device's
    /// backup cannot restore.  The G3X loader inherits this.
    virtual QString rebuildLossWarning(Machine *mach) override;

    static void Register();

    virtual const QString &loaderName() { return bmc_class_name; }

    virtual int Version() { return bmc_version; }

    virtual MachineInfo newInfo() {
            return MachineInfo(MT_CPAP, 0, bmc_class_name, QObject::tr("BMC"), QString(),
            QString(), QString(), QObject::tr("BMC"), QDateTime::currentDateTime(), bmc_version);
        }

    

    

    virtual QString PresReliefLabel();
    virtual ChannelID PresReliefMode();
    virtual ChannelID PresReliefLevel();
    virtual ChannelID CPAPModeChannel();

    virtual double FlowWaveformGain() const { return 0.1; }
    virtual double PressureWaveformGain() const { return 0.1; }
    /// Returns true when @p model stores its pressure wave in G2S raw sensor units, in
    /// which case setSessionWaveforms() converts each sample with
    /// BmcG2sPressureWaveToHundredths() and stores it at kBmcG2sPressureWaveGain instead
    /// of PressureWaveformGain().  Legacy BMC: decided by the model name.  Override to
    /// false on families whose pressure wave comes from a different packet layout.
    /// See GitLab #393.
    virtual bool UsesG2sPressureWave(const QString& model) const {
        return BmcModelUsesG2sPressureWave(model);
    }
    /// Gain applied to Raw.IPAP/Raw.EPAP when writing CPAP_Pressure/CPAP_IPAP/CPAP_EPAP
    /// event lists.  Legacy BMC stores half-cmH2O units (gain 0.5); G3X stores hundredths
    /// of cmH2O (gain 0.01) for full 0.01 cmH2O resolution.
    virtual double PressureChannelGain() const { return 0.5; }
    virtual double FlowAbnormalityWaveformGain() const { return 1.0; }
    virtual double WaveformSampleIntervalMs() const { return 1000 / 25.0; }
    virtual int WaveformSamplesPerPacket() const { return 25; }
    virtual qint64 WaveformPacketDurationMs() const { return 1000; }
    /// Legacy BMC: the PressureWave array (0x08) is mapped to CPAP_MaskPressure.
    /// No separate pressure-wave chart is needed.
    virtual bool ExportPressureWaveform() const { return false; }
    virtual bool ExportFlowAbnormalityWaveform() const { return true; }
    virtual bool ExportLeakRate() const { return true; }
    /// Returns true if CPAP_Ti, CPAP_Te, CPAP_IE, and BMC_IE_Ratio channels
    /// should be populated.  Override to false when the source fields are not
    /// confirmed to carry Ti/Te data (e.g. G3X offsets 0x074/0x07E).
    virtual bool ExportTimingChannels() const { return true; }

    /// Returns true if CPAP_PB (periodic breathing / Cheyne-Stokes) events
    /// should be emitted to OSCAR.  Legacy BMC: enabled (source confirmed).
    /// G3X: suppressed until the 0x44 EVT flag is validated; the signal is
    /// suspected nonsense in SC.74+ firmware and unverified in SC.72.
    virtual bool ExportPeriodicBreathing() const { return true; }

    /// Returns true if the BMC_RESLEX_PATIENT ("Reslex Availability") and
    /// BMC_RESLEX_MODE ("Reslex Mode") settings should be published.  Both are
    /// legacy-BMC concepts: availability is parsed from IDX byte 0x151 bit 7, and
    /// mode is a constant because legacy Reslex only ever operates full time.
    /// Override to false on families where neither has a known source, so the
    /// Daily Device Settings panel does not present a default as though it were a
    /// reading.  See GitLab #274.
    virtual bool ExportReslexDetails() const { return true; }

    /// Returns the session start timestamp to use for really_set_first().
    /// Base implementation uses the session's StartTimestamp (no adjustment).
    /// Subclasses may override to skip startup noise before therapy stabilizes.
    virtual qint64 findStableStartMs(BmcSession* bmcSession) const {
        return bmcSession->StartTimestamp.toMSecsSinceEpoch();
    }

    /// Returns the mask-off timestamp in ms since epoch, i.e. the point at which
    /// the patient removed the mask.  Used to populate SessionSlice(MaskOn) so that
    /// session->hours() reports mask-on time rather than total session length.
    ///
    /// Base implementation returns EndTimestamp (no mask-off detection; MaskOn
    /// covers the whole session).  Subclasses may override with device-specific logic.
    virtual qint64 findStableEndMs(BmcSession* bmcSession) const {
        return bmcSession->EndTimestamp.toMSecsSinceEpoch();
    }

    int sessionsLoaded;

protected:
    /// Makes the parser for a folder of this loader's card data: BmcData here, BmcG3xData
    /// in the G3X loader.  Used for the import source, for each retired index set of a
    /// night archive, and for a backup folder that is still a full card copy.
    virtual std::unique_ptr<BmcDataParser> createParser(const QString& dirpath) const;

    /// The part of Open() both BMC loaders share, once the machine is known: read
    /// @p dirpath (a card or a night archive), import its nights from @p firstImportDay
    /// on, report anything an archive could not supply, and bring the device's backup up
    /// to date.  Returns the number of sessions imported.
    int importFromFolder(const QString& dirpath, Machine* mach, const QDate& firstImportDay);
};

#endif // BMCLOADER_H
