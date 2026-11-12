#pragma once

#include "Library.h"
#include <mutex>

namespace gp
{

class SampleDownloader;

/**
    Finds, loads and owns sample libraries. All heavy work happens on background
    threads; the audio thread picks up a finished library through an atomic pointer.
*/
class SampleManager : private juce::Thread
{
public:
    enum class State { Idle, Searching, Building, Loading, Ready, NotFound, Downloading, Error };

    explicit SampleManager (VoiceMonitor& monitor);
    ~SampleManager() override;

    /** Starts loading. `preferredSfz` may be empty (auto-detect). */
    void begin (const juce::File& preferredSfz);

    /** Loads a specific .sfz file, or the best .sfz inside a folder. */
    void loadAsync (const juce::File& sfzOrFolder);

    /** Downloads and installs the free Salamander Grand Piano, then loads it. */
    void downloadAsync();

    // ---- audio thread ----
    Library* getRequestedLibrary() const noexcept { return requested.load (std::memory_order_acquire); }
    void acknowledge (Library* lib) noexcept      { inUseByAudio.store (lib, std::memory_order_release); }

    // ---- status for the UI ----
    State getState() const noexcept            { return state.load(); }
    float getProgress() const noexcept         { return progress.load(); }
    juce::String getStatusText() const;
    juce::File getCurrentSfz() const;
    juce::StringArray getVariantNames() const;  // other .sfz files next to the current one
    juce::Array<juce::File> getVariants() const;

    static juce::File getDataRoot();             // local app-data folder
    static juce::File getDownloadFolder()        { return getDataRoot().getChildFile ("Library"); }

private:
    void run() override;
    void loadNow (const juce::File& target);
    juce::File findDefaultSfz (const juce::File& preferred) const;
    static juce::File pickSfzInFolder (const juce::File& folder);
    void setStatus (State s, const juce::String& text);
    void prefetchActiveVoices();
    void collectGarbage();

    VoiceMonitor& monitor;

    std::atomic<Library*> requested { nullptr };
    std::atomic<Library*> inUseByAudio { nullptr };

    std::mutex libsLock;  // guards `libraries` against prefetch / garbage collection
    std::vector<std::unique_ptr<Library>> libraries;

    std::atomic<State> state { State::Idle };
    std::atomic<float> progress { 0.0f };
    mutable std::mutex statusLock;
    juce::String statusText;
    juce::File currentSfz;

    // job queue (single background thread)
    std::mutex jobLock;
    enum class Job { None, Begin, Load, Download };
    Job pendingJob = Job::None;
    juce::File pendingFile;
    juce::WaitableEvent wake;

    std::unique_ptr<SampleDownloader> downloader;

    struct Prefetcher;
    std::unique_ptr<Prefetcher> prefetcher;
};

} // namespace gp
