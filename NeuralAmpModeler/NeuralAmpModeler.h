#pragma once

#include "../AudioDSPTools/dsp/ImpulseResponse.h"
#include "../AudioDSPTools/dsp/NoiseGate.h"
#include "../AudioDSPTools/dsp/dsp.h"
#include "../AudioDSPTools/dsp/wav.h"
#include "../AudioDSPTools/dsp/ResamplingContainer/ResamplingContainer.h"
#include "../NeuralAmpModelerCore/NAM/dsp.h"
#include "../NeuralAmpModelerCore/NAM/slimmable.h"

#include "Colors.h"
#include "ToneStack.h"

#include "IPlug_include_in_plug_hdr.h"
#include "ISender.h"

#include <array>

const int kNumPresets = 1;
// The plugin is mono inside
constexpr size_t kNumChannelsInternal = 1;

class NAMSender : public iplug::IPeakAvgSender<>
{
public:
  NAMSender()
  : iplug::IPeakAvgSender<>(-90.0, true, 5.0f, 1.0f, 300.0f, 500.0f)
  {
  }
};

enum EParams
{
  // These need to be the first ones because I use their indices to place
  // their rects in the GUI.
  kInputLevel = 0,
  kNoiseGateThreshold,
  kToneBass,
  kToneMid,
  kToneTreble,
  kOutputLevel,
  // The rest is fine though.
  kNoiseGateActive,
  kEQActive,
  kIRToggle,
  // Input calibration
  kCalibrateInput,
  kInputCalibrationLevel,
  kOutputMode,
  kSlim,
  kNumParams
};

const int numKnobs = 6;

enum ECtrlTags
{
  kCtrlTagModelFileBrowser = 0,
  kCtrlTagIRFileBrowser,
  kCtrlTagInputMeter,
  kCtrlTagOutputMeter,
  kCtrlTagSettingsBox,
  kCtrlTagOutputMode,
  kCtrlTagCalibrateInput,
  kCtrlTagInputCalibrationLevel,
  kCtrlTagSlimmableIcon,
  kCtrlTagSlimOverlayBackdrop,
  kCtrlTagSlimKnob,
  kNumCtrlTags
};

enum EMsgTags
{
  // These tags are used from UI -> DSP
  kMsgTagClearModel = 0,
  kMsgTagClearIR,
  kMsgTagHighlightColor,
  // The following tags are from DSP -> UI
  kMsgTagLoadFailed,
  kMsgTagLoadedModel,
  kMsgTagLoadedIR,
  // Tone Gallery fork: tell a file browser to show its empty/default state
  // (used when the browsers switch to showing a chain unit with no file).
  kMsgTagClearedDisplay,
  kNumMsgTags
};

// Get the sample rate of a NAM model.
// Sometimes, the model doesn't know its own sample rate; this wrapper guesses 48k based on the way that most
// people have used NAM in the past.
double GetNAMSampleRate(const std::unique_ptr<nam::DSP>& model)
{
  // Some models are from when we didn't have sample rate in the model.
  // For those, this wraps with the assumption that they're 48k models, which is probably true.
  const double assumedSampleRate = 48000.0;
  const double reportedEncapsulatedSampleRate = model->GetExpectedSampleRate();
  const double encapsulatedSampleRate =
    reportedEncapsulatedSampleRate <= 0.0 ? assumedSampleRate : reportedEncapsulatedSampleRate;
  return encapsulatedSampleRate;
};

class ResamplingNAM : public nam::DSP
{
public:
  // Resampling wrapper around the NAM models
  ResamplingNAM(std::unique_ptr<nam::DSP> encapsulated, const double expected_sample_rate)
  : nam::DSP(encapsulated->NumInputChannels(), encapsulated->NumOutputChannels(), expected_sample_rate)
  , mEncapsulated(std::move(encapsulated))
  , mResampler(GetNAMSampleRate(mEncapsulated))
  {
    // Assign the encapsulated object's processing function to this object's member so that the resampler can use it:
    auto ProcessBlockFunc = [&](NAM_SAMPLE** input, NAM_SAMPLE** output, int numFrames) {
      mEncapsulated->process(input, output, numFrames);
    };
    mBlockProcessFunc = ProcessBlockFunc;

    // Get the other information from the encapsulated NAM so that we can tell the outside world about what we're
    // holding.
    if (mEncapsulated->HasLoudness())
    {
      SetLoudness(mEncapsulated->GetLoudness());
    }
    if (mEncapsulated->HasInputLevel())
    {
      SetInputLevel(mEncapsulated->GetInputLevel());
    }
    if (mEncapsulated->HasOutputLevel())
    {
      SetOutputLevel(mEncapsulated->GetOutputLevel());
    }

    // NOTE: prewarm samples doesn't mean anything--we can prewarm the encapsulated model as it likes and be good to
    // go.
    // _prewarm_samples = 0;

    // And be ready
    int maxBlockSize = 2048; // Conservative
    Reset(expected_sample_rate, maxBlockSize);
  };

  ~ResamplingNAM() = default;

  void prewarm() override { mEncapsulated->prewarm(); };

  void process(NAM_SAMPLE** input, NAM_SAMPLE** output, const int num_frames) override
  {
    if (num_frames > mMaxExternalBlockSize)
      // We can afford to be careful
      throw std::runtime_error("More frames were provided than the max expected!");

    if (!NeedToResample())
    {
      mEncapsulated->process(input, output, num_frames);
    }
    else
    {
      mResampler.ProcessBlock(input, output, num_frames, mBlockProcessFunc);
    }
  };

  int GetLatency() const { return NeedToResample() ? mResampler.GetLatency() : 0; };

  void Reset(const double sampleRate, const int maxBlockSize) override
  {
    mExpectedSampleRate = sampleRate;
    mMaxExternalBlockSize = maxBlockSize;
    mResampler.Reset(sampleRate, maxBlockSize);

    // Allocations in the encapsulated model (HACK)
    // Stolen some code from the resampler; it'd be nice to have these exposed as methods? :)
    const double mUpRatio = sampleRate / GetEncapsulatedSampleRate();
    const auto maxEncapsulatedBlockSize = static_cast<int>(std::ceil(static_cast<double>(maxBlockSize) / mUpRatio));
    mEncapsulated->ResetAndPrewarm(sampleRate, maxEncapsulatedBlockSize);
  };

  // So that we can let the world know if we're resampling (useful for debugging)
  double GetEncapsulatedSampleRate() const { return GetNAMSampleRate(mEncapsulated); };

  nam::SlimmableModel* GetSlimmableModel() { return dynamic_cast<nam::SlimmableModel*>(mEncapsulated.get()); }
  const nam::SlimmableModel* GetSlimmableModel() const
  {
    return dynamic_cast<const nam::SlimmableModel*>(mEncapsulated.get());
  }

private:
  bool NeedToResample() const { return GetExpectedSampleRate() != GetEncapsulatedSampleRate(); };
  // The encapsulated NAM
  std::unique_ptr<nam::DSP> mEncapsulated;

  // The resampling wrapper
  dsp::ResamplingContainer<NAM_SAMPLE, 1, 12> mResampler;

  // Used to check that we don't get too large a block to process.
  int mMaxExternalBlockSize = 0;

  // This function is defined to conform to the interface expected by the iPlug2 resampler.
  std::function<void(NAM_SAMPLE**, NAM_SAMPLE**, int)> mBlockProcessFunc;
};

class NeuralAmpModeler final : public iplug::Plugin
{
public:
  NeuralAmpModeler(const iplug::InstanceInfo& info);
  ~NeuralAmpModeler();

  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;

  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;
  void OnUIOpen() override;
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override { return true; }

  void OnParamChange(int paramIdx) override;
  void OnParamChangeUI(int paramIdx, iplug::EParamSource source) override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

  // Tone Gallery fork: whether the compact rack view is active. Lives on the
  // plugin (not the UI) so it survives the editor window being closed and
  // reopened.
  bool mToneRackMode = false;

  // --- Tone Gallery fork: signal chain -------------------------------------
  // The plugin can run a chain of up to 4 tone units in series:
  //   unit 1 = the plugin's regular model + IR (mModel / mIR)
  //   units 2..4 = the extra ChainSlots below, each model -> IR -> level,
  //                processed right after unit 1's IR, before the DC blocker.
  // Each unit has a bypass and an output level. All of it is serialized with
  // the project (see the ###NAMChainV1### block in Serialization).
  static constexpr int kNumChainSlots = 3; // extra units beyond the main one

  struct ChainSlot
  {
    // Live DSP (audio thread only, swapped in via _ApplyDSPStaging)
    std::unique_ptr<ResamplingNAM> model;
    std::unique_ptr<dsp::ImpulseResponse> ir;
    // Staged DSP (created on the UI thread, picked up by the audio thread)
    std::unique_ptr<ResamplingNAM> stagedModel;
    std::unique_ptr<dsp::ImpulseResponse> stagedIR;
    // Safe-removal flags
    std::atomic<bool> removeModel{false};
    std::atomic<bool> removeIR{false};
    // What's loaded (UI thread)
    WDL_String modelPath;
    WDL_String irPath;
    WDL_String tonePath; // tone-library folder, for the UI's photo/name
    // Unit controls (written by UI, read by audio thread)
    std::atomic<bool> enabled{true};
    std::atomic<double> levelDB{0.0};
    // Per-unit knob settings (driven by the main knobs while this unit is
    // being edited). EQ values of 5/5/5 mean "flat" and skip the tone stack
    // entirely so untouched units sound exactly as before.
    std::atomic<double> inputDB{0.0};
    std::atomic<double> bass{5.0};
    std::atomic<double> middle{5.0};
    std::atomic<double> treble{5.0};
  };

  std::array<ChainSlot, kNumChainSlots> mChainSlots;
  // Unit-1 (main model) chain controls
  std::atomic<bool> mChainMainEnabled{true};
  std::atomic<double> mChainMainLevelDB{0.0};
  // Whether the stacked chain view is showing (persists across editor reopen)
  bool mToneChainMode = false;
  // Which chain unit the full UI is currently choosing a tone for:
  //   -1 = not editing (loads go to the main model as usual)
  //    0 = unit 1 (the main model), 1..kNumChainSlots = the extra slots.
  // While >= 1, the model/IR load handlers route into that chain slot.
  int mChainEditSlot = -1;

  // Load a tone into an extra chain slot (0..kNumChainSlots-1). Empty paths
  // clear that part of the slot. Called from the UI thread.
  void SetChainTone(int slot, const char* modelPath, const char* irPath, const char* tonePath);
  void ClearChainSlot(int slot) { SetChainTone(slot, "", "", ""); }

  // Start/stop editing a rack unit from the full UI. While a unit >= 1 is
  // being edited, the INPUT/BASS/MIDDLE/TREBLE/OUTPUT knobs drive that chain
  // slot's own settings (the main tone's knob values are backed up and
  // restored when editing ends).
  void BeginChainKnobEdit(int unit);
  void EndChainKnobEdit();

  // --- Rig presets (see NAMRigPresets.h) ----------------------------------
  // Capture the ENTIRE current rig (main model/IR, global knobs + toggles,
  // all chain slots with their settings) as JSON / apply one back.
  nlohmann::json CaptureRigPreset() const;
  void ApplyRigPreset(const nlohmann::json& j);
  // Library-relative path of the currently loaded rig preset ("" = none).
  // Serialized with the project so SAVE knows what to overwrite.
  WDL_String mRigPresetRel;

private:
  // Allocates mInputPointers and mOutputPointers
  void _AllocateIOPointers(const size_t nChans);
  // Moves DSP modules from staging area to the main area.
  // Also deletes DSP modules that are flagged for removal.
  // Exists so that we don't try to use a DSP module that's only
  // partially-instantiated.
  void _ApplyDSPStaging();
  // Deallocates mInputPointers and mOutputPointers
  void _DeallocateIOPointers();
  // Fallback that just copies inputs to outputs if mDSP doesn't hold a model.
  void _FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels, const size_t numFrames);
  // Sizes based on mInputArray
  size_t _GetBufferNumChannels() const;
  size_t _GetBufferNumFrames() const;
  void _InitToneStack();
  // Loads a NAM model and stores it to mStagedNAM
  // Returns an empty string on success, or an error message on failure.
  std::string _StageModel(const WDL_String& dspFile);
  // Loads an IR and stores it to mStagedIR.
  // Return status code so that error messages can be relayed if
  // it wasn't successful.
  dsp::wav::LoadReturnCode _StageIR(const WDL_String& irPath);

  bool _HaveModel() const { return this->mModel != nullptr; };
  // Prepare the input & output buffers
  void _PrepareBuffers(const size_t numChannels, const size_t numFrames);
  // Manage pointers
  void _PrepareIOPointers(const size_t nChans);
  // Copy the input buffer to the object, applying input level.
  // :param nChansIn: In from external
  // :param nChansOut: Out to the internal of the DSP routine
  void _ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn, const size_t nChansOut);
  // Copy the output to the output buffer, applying output level.
  // :param nChansIn: In from internal
  // :param nChansOut: Out to external
  void _ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames, const size_t nChansIn,
                      const size_t nChansOut);
  // Resetting for models and IRs, called by OnReset
  void _ResetModelAndIR(const double sampleRate, const int maxBlockSize);

  void _SetInputGain();
  void _SetOutputGain();
  void _ApplySlimParamToLoadedNAMs();

  // Stage a model/IR into an extra chain slot (UI thread). Used by
  // SetChainTone and by unserialization.
  void _StageChainModel(int slot, const char* modelPath);
  void _StageChainIR(int slot, const char* irPath);
  // Set a knob param's value, update the on-screen knob, and apply it.
  void _SetKnobParamAndNotify(int paramIdx, double value);
  // Point the model/IR file browsers at whatever the current edit target is
  // (a chain slot's files while editing, the main tone's otherwise).
  void _UpdateBrowsersForEditSlot();
  // Read the ###NAMChainV1### block appended to the state chunk (if present).
  int _UnserializeChain(const iplug::IByteChunk& chunk, int startPos);

  // See: Unserialization.cpp
  void _UnserializeApplyConfig(nlohmann::json& config);
  // 0.7.9 and later
  int _UnserializeStateWithKnownVersion(const iplug::IByteChunk& chunk, int startPos);
  // Hopefully 0.7.3-0.7.8, but no gurantees
  int _UnserializeStateWithUnknownVersion(const iplug::IByteChunk& chunk, int startPos);

  // Update all controls that depend on a model
  void _UpdateControlsFromModel();

  // Make sure that the latency is reported correctly.
  void _UpdateLatency();

  // Update level meters
  // Called within ProcessBlock().
  // Assume _ProcessInput() and _ProcessOutput() were run immediately before.
  void _UpdateMeters(iplug::sample** inputPointer, iplug::sample** outputPointer, const size_t nFrames,
                     const size_t nChansIn, const size_t nChansOut);

  // Member data

  // Input arrays to NAM
  std::vector<std::vector<iplug::sample>> mInputArray;
  // Output from NAM
  std::vector<std::vector<iplug::sample>> mOutputArray;
  // Pointer versions
  iplug::sample** mInputPointers = nullptr;
  iplug::sample** mOutputPointers = nullptr;

  // Input and output gain
  double mInputGain = 1.0;
  double mOutputGain = 1.0;

  // Noise gates
  dsp::noise_gate::Trigger mNoiseGateTrigger;
  dsp::noise_gate::Gain mNoiseGateGain;
  // The model actually being used:
  std::unique_ptr<ResamplingNAM> mModel;
  // And the IR
  std::unique_ptr<dsp::ImpulseResponse> mIR;
  // Manages switching what DSP is being used.
  std::unique_ptr<ResamplingNAM> mStagedModel;
  std::unique_ptr<dsp::ImpulseResponse> mStagedIR;
  // Flags to take away the modules at a safe time.
  std::atomic<bool> mShouldRemoveModel = false;
  std::atomic<bool> mShouldRemoveIR = false;

  std::atomic<bool> mNewModelLoadedInDSP = false;
  std::atomic<bool> mModelCleared = false;

  // Tone stack modules
  std::unique_ptr<dsp::tone_stack::AbstractToneStack> mToneStack;

  // Post-IR filters
  recursive_linear_filter::HighPass mHighPass;
  // recursive_linear_filter::LowPass mLowPass;

  // Scratch buffers for the extra chain units (mono; sized lazily in
  // ProcessBlock the first time a slot with a model runs).
  std::array<std::vector<iplug::sample>, kNumChainSlots> mChainArrays;
  std::array<iplug::sample*, kNumChainSlots> mChainScratchPointers{};
  // Per-slot tone stacks (only run when a slot's EQ is moved off flat).
  std::array<std::unique_ptr<dsp::tone_stack::AbstractToneStack>, kNumChainSlots> mChainToneStacks;
  // Main-knob backup while a chain slot borrows the knobs.
  bool mKnobEditActive = false;
  double mMainKnobBackup[5] = {0.0, 5.0, 5.0, 5.0, 0.0};

  // Path to model's config.json or model.nam
  WDL_String mNAMPath;
  // Path to IR (.wav file)
  WDL_String mIRPath;

  WDL_String mHighLightColor{PluginColors::NAM_THEMECOLOR.ToColorCode()};

  std::unordered_map<std::string, double> mNAMParams = {{"Input", 0.0}, {"Output", 0.0}};

  NAMSender mInputSender, mOutputSender;
};
