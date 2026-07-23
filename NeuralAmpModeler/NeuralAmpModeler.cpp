#include <algorithm> // std::clamp, std::min
#include <cmath> // pow
#include <filesystem>
#include <iostream>
#include <utility>

#include "Colors.h"
#include "../NeuralAmpModelerCore/NAM/activations.h"
#include "../NeuralAmpModelerCore/NAM/get_dsp.h"
// clang-format off
// These includes need to happen in this order or else the latter won't know
// a bunch of stuff.
#include "NeuralAmpModeler.h"
#include "IPlug_include_in_plug_src.h"
// clang-format on
#include "architecture.hpp"

#include "NeuralAmpModelerControls.h"
#include "NAMToneGalleryControl.h"
#include "NAMTheme.h"
#include "NAMTone3000Browser.h"
#include "NAMRigPresets.h"
#include "NAMChainView.h"

using namespace iplug;
using namespace igraphics;

const double kDCBlockerFrequency = 5.0;

// Styles
const IVColorSpec colorSpec{
  DEFAULT_BGCOLOR, // Background
  PluginColors::NAM_THEMECOLOR, // Foreground
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.3f), // Pressed
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.4f), // Frame
  PluginColors::MOUSEOVER, // Highlight
  DEFAULT_SHCOLOR, // Shadow
  PluginColors::NAM_THEMECOLOR, // Extra 1
  COLOR_RED, // Extra 2 --> color for clipping in meters
  PluginColors::NAM_THEMECOLOR.WithContrast(0.1f), // Extra 3
};

const IVStyle style =
  IVStyle{true, // Show label
          true, // Show value
          colorSpec,
          {11.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.65f), "Inter-Bold", EAlign::Center,
           EVAlign::Middle}, // Knob label text
          {11.f, PluginColors::NAM_THEMEFONTCOLOR, "Inter-Regular", EAlign::Center, EVAlign::Bottom}, // Knob value text
          DEFAULT_HIDE_CURSOR,
          DEFAULT_DRAW_FRAME,
          false,
          DEFAULT_EMBOSS,
          0.2f,
          2.f,
          DEFAULT_SHADOW_OFFSET,
          DEFAULT_WIDGET_FRAC,
          DEFAULT_WIDGET_ANGLE};
const IVStyle titleStyle =
  DEFAULT_STYLE.WithValueText(IText(30, COLOR_WHITE, "Michroma-Regular")).WithDrawFrame(false).WithShadowOffset(2.f);
const IVStyle radioButtonStyle =
  style
    .WithColor(EVColor::kON, PluginColors::NAM_THEMECOLOR) // Pressed buttons and their labels
    .WithColor(EVColor::kOFF, PluginColors::NAM_THEMECOLOR.WithOpacity(0.1f)) // Unpressed buttons
    .WithColor(EVColor::kX1, PluginColors::NAM_THEMECOLOR.WithOpacity(0.6f)); // Unpressed buttons' labels

EMsgBoxResult _ShowMessageBox(iplug::igraphics::IGraphics* pGraphics, const char* str, const char* caption,
                              EMsgBoxType type)
{
#ifdef OS_MAC
  // macOS is backwards?
  return pGraphics->ShowMessageBox(caption, str, type);
#else
  return pGraphics->ShowMessageBox(str, caption, type);
#endif
}

const std::string kCalibrateInputParamName = "CalibrateInput";
const bool kDefaultCalibrateInput = false;
const std::string kInputCalibrationLevelParamName = "InputCalibrationLevel";
const double kDefaultInputCalibrationLevel = 12.0;

NeuralAmpModeler::NeuralAmpModeler(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  _InitToneStack();
  // Tone Gallery fork: each extra chain slot gets its own tone stack.
  for (int ci = 0; ci < kNumChainSlots; ci++)
    mChainToneStacks[ci] = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
  nam::activations::Activation::enable_fast_tanh();
  // Allow the editor to shrink down to rack-view height (the host clamps
  // resize requests to these constraints).
  // Height range: rack view (short) up to the stacked chain view (tall).
  // AMPRYX skin: the normal window (PLUG_HEIGHT = 558) is now the tallest state
  // (rack 140 < chain 464 < 558), so the max-height constraint is PLUG_HEIGHT.
  SetSizeConstraints(PLUG_WIDTH, PLUG_WIDTH, (int)kRackViewHeight, PLUG_HEIGHT);
  GetParam(kInputLevel)->InitGain("Input", 0.0, -20.0, 20.0, 0.1);
  GetParam(kToneBass)->InitDouble("Bass", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneMid)->InitDouble("Middle", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneTreble)->InitDouble("Treble", 5.0, 0.0, 10.0, 0.1);
  GetParam(kOutputLevel)->InitGain("Output", 0.0, -40.0, 40.0, 0.1);
  GetParam(kNoiseGateThreshold)->InitGain("Threshold", -80.0, -100.0, 0.0, 0.1);
  GetParam(kNoiseGateActive)->InitBool("NoiseGateActive", true);
  GetParam(kEQActive)->InitBool("ToneStack", true);
  GetParam(kOutputMode)->InitEnum("OutputMode", 1, {"Raw", "Normalized", "Calibrated"}); // TODO DRY w/ control
  GetParam(kIRToggle)->InitBool("IRToggle", true);
  GetParam(kCalibrateInput)->InitBool(kCalibrateInputParamName.c_str(), kDefaultCalibrateInput);
  GetParam(kInputCalibrationLevel)
    ->InitDouble(kInputCalibrationLevelParamName.c_str(), kDefaultInputCalibrationLevel, -60.0, 60.0, 0.1, "dBu");
  GetParam(kSlim)->InitDouble("Slim", 0.0, 0.0, 1.0, 0.01);

  mNoiseGateTrigger.AddListener(&mNoiseGateGain);

  mMakeGraphicsFunc = [&]() {

#ifdef OS_IOS
    auto scaleFactor = GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT) * 0.85f;
#else
    auto scaleFactor = 1.0f;
#endif

    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, scaleFactor);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachTextEntryControl();
    pGraphics->EnableMouseOver(true);
    pGraphics->EnableTooltips(true);
    pGraphics->EnableMultiTouch(true);

    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->LoadFont("Michroma-Regular", MICHROMA_FN);
    // AMPRYX skin: the whole UI renders in JetBrains Mono. We register the mono
    // face under BOTH its own IDs and the legacy "Inter-*" IDs, so every text
    // style already in the codebase (which references Inter) picks up the mono
    // face with no per-call changes. Archivo Black is the display/wordmark face.
    // Each registration falls back to Inter, then Roboto, if a TTF is missing.
    auto loadFontFB = [&](const char* id, const char* primary, const char* fb1, const char* fb2) {
      if (!pGraphics->LoadFont(id, primary) && !pGraphics->LoadFont(id, fb1))
        pGraphics->LoadFont(id, fb2);
    };
    loadFontFB("Inter-Regular", JETBRAINS_FN, INTER_FN, ROBOTO_FN);
    loadFontFB("Inter-Bold", JETBRAINS_BOLD_FN, INTER_BOLD_FN, ROBOTO_FN);
    loadFontFB(namtheme::kFontMono, JETBRAINS_FN, INTER_FN, ROBOTO_FN);
    loadFontFB(namtheme::kFontMonoMed, JETBRAINS_MEDIUM_FN, JETBRAINS_FN, INTER_FN);
    loadFontFB(namtheme::kFontMonoBold, JETBRAINS_BOLD_FN, INTER_BOLD_FN, ROBOTO_FN);
    loadFontFB(namtheme::kFontDisplay, ARCHIVO_BLACK_FN, INTER_BOLD_FN, ROBOTO_FN);
    loadFontFB(namtheme::kFontWordmark, ARCHIVO_BOLD_FN, ARCHIVO_BLACK_FN, INTER_BOLD_FN);

    const auto gearSVG = pGraphics->LoadSVG(GEAR_FN);
    const auto fileSVG = pGraphics->LoadSVG(FILE_FN);
    const auto globeSVG = pGraphics->LoadSVG(GLOBE_ICON_FN);
    const auto crossSVG = pGraphics->LoadSVG(CLOSE_BUTTON_FN);
    const auto rightArrowSVG = pGraphics->LoadSVG(RIGHT_ARROW_FN);
    const auto leftArrowSVG = pGraphics->LoadSVG(LEFT_ARROW_FN);
    const auto modelIconSVG = pGraphics->LoadSVG(MODEL_ICON_FN);
    const auto irIconOnSVG = pGraphics->LoadSVG(IR_ICON_ON_FN);
    const auto irIconOffSVG = pGraphics->LoadSVG(IR_ICON_OFF_FN);
    const auto slimIconSVG = pGraphics->LoadSVG(SLIMMABLE_ICON_FN);

    const auto backgroundBitmap = pGraphics->LoadBitmap(BACKGROUND_FN);
    const auto fileBackgroundBitmap = pGraphics->LoadBitmap(FILEBACKGROUND_FN);
    const auto inputLevelBackgroundBitmap = pGraphics->LoadBitmap(INPUTLEVELBACKGROUND_FN);
    const auto linesBitmap = pGraphics->LoadBitmap(LINES_FN);
    const auto knobBackgroundBitmap = pGraphics->LoadBitmap(KNOBBACKGROUND_FN);
    const auto switchHandleBitmap = pGraphics->LoadBitmap(SLIDESWITCHHANDLE_FN);
    const auto meterBackgroundBitmap = pGraphics->LoadBitmap(METERBACKGROUND_FN);
    // AMPRYX skin: halftone-engraving textures behind the knob panel + A/B cards.
    const auto asciiHeroBitmap = pGraphics->LoadBitmap(ASCII_HERO_FN);
    const auto asciiToneABitmap = pGraphics->LoadBitmap(ASCII_TONEA_FN);
    // AMPRYX skin: the Z sigil logo used by the title control.
    ThemedTitleControl::sSigilBitmap = pGraphics->LoadBitmap(ZSIGIL_FN);

    const auto b = pGraphics->GetBounds();
    // Window regions: tone sidebar on the left, favorites bar under the main
    // UI, and the stock 600x400 main UI in the remaining space (mainB).
    const auto sidebarArea = b.GetFromLeft(kSidebarWidth);
    const auto rightRegion = b.GetReducedFromLeft(kSidebarWidth);
    // AMPRYX skin: stacked from the bottom -- utility bar, the favorites bar,
    // then the main UI above them.
    // Same flush span as the knob panel (46px in from the main-panel edges).
    const auto utilityBarArea =
      rightRegion.GetFromBottom(kUtilityBarHeight).GetReducedFromLeft(46.0f).GetReducedFromRight(46.0f);
    const auto favoritesArea = rightRegion.GetReducedFromBottom(kUtilityBarHeight).GetFromBottom(kFavoritesBarHeight);
    const auto mainB = rightRegion.GetReducedFromBottom(kUtilityBarHeight + kFavoritesBarHeight);
    const auto mainArea = mainB.GetPadded(-20);
    const auto contentArea = mainArea.GetPadded(-10);
    // AMPRYX skin: compact single-row header hugging the top of the panel
    // (sigil logo beside the AMPRYX wordmark, flanked by the TONE3000 and
    // SIGNAL CHAIN buttons). Proportions measured off the reference.
    const auto titleArea = mainB.GetFromTop(64.0f);

    // Areas for knobs (shifted down past the compact header row).
    const auto knobsPad = 20.0f;
    const auto singleKnobPad = -2.0f;
    const auto knobsArea = contentArea.GetFromTop(NAM_KNOB_HEIGHT)
                             .GetReducedFromLeft(knobsPad)
                             .GetReducedFromRight(knobsPad)
                             .GetVShifted(46.0f);
    // Tone Morph: one extra knob cell on the right of the row for the MORPH knob.
    const int kKnobCols = numKnobs + 1;
    const auto inputKnobArea = knobsArea.GetGridCell(0, kInputLevel, 1, kKnobCols).GetPadded(-singleKnobPad);
    const auto noiseGateArea = knobsArea.GetGridCell(0, kNoiseGateThreshold, 1, kKnobCols).GetPadded(-singleKnobPad);
    const auto bassKnobArea = knobsArea.GetGridCell(0, kToneBass, 1, kKnobCols).GetPadded(-singleKnobPad);
    const auto midKnobArea = knobsArea.GetGridCell(0, kToneMid, 1, kKnobCols).GetPadded(-singleKnobPad);
    const auto trebleKnobArea = knobsArea.GetGridCell(0, kToneTreble, 1, kKnobCols).GetPadded(-singleKnobPad);
    const auto outputKnobArea = knobsArea.GetGridCell(0, kOutputLevel, 1, kKnobCols).GetPadded(-singleKnobPad);
    const auto morphKnobArea = knobsArea.GetGridCell(0, numKnobs, 1, kKnobCols).GetPadded(-singleKnobPad);

    // Toggle bar: a full-width strip under the knob panel holding all four
    // switches evenly spread -- NOISE GATE, EQ, DOUBLE TRACK, BASS CENTER
    // (labels centered under each switch), matching the AMPRYX mock.
    const auto toggleBar = IRECT(knobsArea.L - 4.0f, knobsArea.B + 15.0f, knobsArea.R + 4.0f, knobsArea.B + 61.0f);
    const auto toggleBarInner = toggleBar.GetPadded(-6.0f);
    const auto ngToggleArea = toggleBarInner.GetGridCell(0, 0, 1, 4);
    const auto eqToggleArea = toggleBarInner.GetGridCell(0, 1, 1, 4);
    const auto doubleTrackSwitchArea = toggleBarInner.GetGridCell(0, 2, 1, 4);
    const auto bassCenterSwitchArea = toggleBarInner.GetGridCell(0, 3, 1, 4);

    // AMPRYX skin: everything below the toggle bar shares one flush span with
    // the knob panel / toggle bar. Stacked top-down: A/B tone cards, then the
    // full-span IR row (with the IR bypass toggle in a slot at its right end).
    const float spanL = knobsArea.L - 4.0f;
    const float spanR = knobsArea.R + 4.0f;
    const auto morphCardsArea = IRECT(spanL, toggleBar.B + 10.0f, spanR, toggleBar.B + 66.0f);
    const auto irArea = IRECT(spanL, morphCardsArea.B + 10.0f, spanR, morphCardsArea.B + 44.0f);
    // The IR bypass switch control is attached hidden (no icon in the AMPRYX
    // skin); the kIRToggle parameter stays functional for host automation.
    const auto irSwitchArea = IRECT(spanR - 26.0f, irArea.MH() - 9.0f, spanR - 8.0f, irArea.MH() + 9.0f);
    // Hidden legacy model browser keeps a rect stacked above the IR row.
    const auto modelArea = irArea.GetVShifted(-38.0f);
    const auto slimIconArea =
      IRECT(irArea.R + 6.f, irArea.MH() - 14.f, irArea.R + 6.f + 2.f * 28.f, irArea.MH() + 14.f);

    // Meters: thin vertical bars flanking the knob panel (AMPRYX mock).
    const auto knobPanelRect = knobsArea.GetHPadded(4.0f).GetVPadded(12.0f);
    const auto inputMeterArea = IRECT(knobPanelRect.L - 26.0f, knobPanelRect.T, knobPanelRect.L - 8.0f, knobPanelRect.B);
    const auto outputMeterArea =
      IRECT(knobPanelRect.R + 8.0f, knobPanelRect.T, knobPanelRect.R + 26.0f, knobPanelRect.B);

    // AMPRYX header buttons: slim square TONE3000 (left) + SIGNAL CHAIN
    // (right), vertically centred on the logo row. Sized off the reference
    // (~90/110 x 26 at this panel width).
    const float kHdrBtnH = 26.0f;
    const IRECT t3kButtonArea(contentArea.L, titleArea.MH() - 0.5f * kHdrBtnH, contentArea.L + 92.0f,
                              titleArea.MH() + 0.5f * kHdrBtnH);
    const IRECT chainButtonArea(contentArea.R - 112.0f, titleArea.MH() - 0.5f * kHdrBtnH, contentArea.R,
                                titleArea.MH() + 0.5f * kHdrBtnH);

    // Model loader button
    auto loadModelCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        // Tone Morph: while targeting a unit's B side, the load goes there.
        if (mChainEditTargetB && mChainEditSlot <= kNumChainSlots)
        {
          const int unit = mChainEditSlot >= 1 ? mChainEditSlot : 0;
          if (strcmp(GetUnitBTonePath(unit), path.Get()) != 0)
          {
            // New tone folder for B: clear the B IR so tones swap cleanly.
            if (unit == 0)
            {
              mIRPathB.Set("");
              mShouldRemoveIRB = true;
            }
            else
            {
              mChainSlots[unit - 1].irBPath.Set("");
              mChainSlots[unit - 1].removeIRB = true;
            }
          }
          if (unit == 0)
            _StageModelB(fileName);
          else
            _StageChainModelB(unit - 1, fileName.Get());
          if (unit == 0)
            mMainToneBPath.Set(path.Get());
          else
            mChainSlots[unit - 1].toneBPath.Set(path.Get());
          if (GetUnitMorph(unit) < 0.0001)
            SetUnitMorph(unit, 0.5); // make the newly loaded B tone audible
          _UpdateBrowsersForEditSlot();
          if (GetUI())
            GetUI()->SetAllControlsDirty();
          return;
        }
        // Tone Gallery fork: while editing a chain unit, loads go to that
        // slot instead of the main model. Loading a different tone folder
        // also clears the slot's old IR so tones swap cleanly.
        if (mChainEditSlot >= 1 && mChainEditSlot <= kNumChainSlots)
        {
          const int slot = mChainEditSlot - 1;
          if (strcmp(mChainSlots[slot].tonePath.Get(), path.Get()) != 0)
          {
            mChainSlots[slot].irPath.Set("");
            mChainSlots[slot].removeIR = true;
          }
          _StageChainModel(slot, fileName.Get());
          mChainSlots[slot].tonePath.Set(path.Get());
          mChainSlots[slot].enabled = true;
          _UpdateBrowsersForEditSlot();
          return;
        }
        // Sets mNAMPath and mStagedNAM
        const std::string msg = _StageModel(fileName);
        // TODO error messages like the IR loader.
        if (msg.size())
        {
          std::stringstream ss;
          ss << "Failed to load NAM model. Message:\n\n" << msg;
          _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load model!", kMB_OK);
        }
        std::cout << "Loaded: " << fileName.Get() << std::endl;
      }
    };

    // IR loader button
    auto loadIRCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        // Tone Morph: while targeting a unit's B side, the IR goes there.
        if (mChainEditTargetB && mChainEditSlot <= kNumChainSlots)
        {
          const int unit = mChainEditSlot >= 1 ? mChainEditSlot : 0;
          if (strcmp(GetUnitBTonePath(unit), path.Get()) != 0)
          {
            // New tone folder: an IR-only (cab) B clears the B amp model.
            if (unit == 0)
            {
              mNAMPathB.Set("");
              mShouldRemoveModelB = true;
            }
            else
            {
              mChainSlots[unit - 1].modelBPath.Set("");
              mChainSlots[unit - 1].removeModelB = true;
            }
          }
          if (unit == 0)
            _StageIRB(fileName);
          else
            _StageChainIRB(unit - 1, fileName.Get());
          if (unit == 0)
            mMainToneBPath.Set(path.Get());
          else
            mChainSlots[unit - 1].toneBPath.Set(path.Get());
          if (GetUnitMorph(unit) < 0.0001)
            SetUnitMorph(unit, 0.5); // make the newly loaded B tone audible
          _UpdateBrowsersForEditSlot();
          if (GetUI())
            GetUI()->SetAllControlsDirty();
          return;
        }
        // Tone Gallery fork: route into the chain slot being edited. An
        // IR-only tone (a cab) clears the slot's old amp model.
        if (mChainEditSlot >= 1 && mChainEditSlot <= kNumChainSlots)
        {
          const int slot = mChainEditSlot - 1;
          if (strcmp(mChainSlots[slot].tonePath.Get(), path.Get()) != 0)
          {
            mChainSlots[slot].modelPath.Set("");
            mChainSlots[slot].removeModel = true;
          }
          _StageChainIR(slot, fileName.Get());
          mChainSlots[slot].tonePath.Set(path.Get());
          mChainSlots[slot].enabled = true;
          _UpdateBrowsersForEditSlot();
          return;
        }
        mIRPath = fileName;
        const dsp::wav::LoadReturnCode retCode = _StageIR(fileName);
        if (retCode != dsp::wav::LoadReturnCode::SUCCESS)
        {
          std::stringstream message;
          message << "Failed to load IR file " << fileName.Get() << ":\n";
          message << dsp::wav::GetMsgForLoadReturnCode(retCode);

          _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load IR!", kMB_OK);
        }
      }
    };

    // AMPRYX skin: near-black window, faint gold dot-grid, square gold-bordered
    // panels with a halftone-engraving texture behind the knobs.
    pGraphics->AttachPanelBackground(namtheme::BG);
    pGraphics->AttachControl(new AmpryxDotGridControl(b));
    const auto knobCardArea = knobsArea.GetHPadded(4.0f).GetVPadded(12.0f);
    pGraphics->AttachControl(new ThemedCardControl(knobCardArea, namtheme::PANEL2, 0.0f, namtheme::BORDER, 2.0f));
    pGraphics->AttachControl(new AmpryxTextureControl(knobCardArea.GetPadded(-2.0f), asciiHeroBitmap, 0.42f));
    // The toggle bar's square gold-bordered background (behind the four switches).
    pGraphics->AttachControl(new ThemedCardControl(toggleBar, namtheme::CARD, 0.0f, namtheme::BORDER, 2.0f));
    // Stacked logo, centred across the full content width (the TONE3000 and
    // SIGNAL CHAIN buttons sit at the top corners, drawn on top).
    pGraphics->AttachControl(new ThemedTitleControl(IRECT(contentArea.L, titleArea.T, contentArea.R, titleArea.B)));
    // Tone Morph: the model box + its icon are hidden; the A/B TONE cards
    // (attached below) take their place above the IR box.

#ifdef NAM_PICK_DIRECTORY
    const std::string defaultNamFileString = "Select model directory...";
    const std::string defaultIRString = "Select IR directory...";
#else
    const std::string defaultNamFileString = "Select model...";
    const std::string defaultIRString = "Select IR...";
#endif
    // Getting started page listing additional resources
    const char* const getUrl = "https://www.neuralampmodeler.com/users#comp-marb84o5";
    pGraphics
      ->AttachControl(
        new ThemedFileBrowserControl(modelArea, kMsgTagClearModel, defaultNamFileString.c_str(), "nam",
                                     loadModelCompletionHandler, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                     fileBackgroundBitmap, globeSVG, "Get NAM Models", getUrl, namtheme::Accent()),
        kCtrlTagModelFileBrowser)
      ->Hide(true); // Tone Morph: model box hidden; the A/B cards replace it

    auto hideSlimOverlay = [](IControl* pCaller) {
      IGraphics* ui = pCaller->GetUI();
      if (auto* backdrop = ui->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        backdrop->Hide(true);
      if (auto* knob = ui->GetControlWithTag(kCtrlTagSlimKnob))
        knob->Hide(true);
      ui->SetAllControlsDirty();
    };
    auto showSlimOverlay = [](IControl* pCaller) {
      IGraphics* ui = pCaller->GetUI();
      if (auto* backdrop = ui->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        backdrop->Hide(false);
      if (auto* knob = ui->GetControlWithTag(kCtrlTagSlimKnob))
        knob->Hide(false);
      ui->SetAllControlsDirty();
    };

    pGraphics
      ->AttachControl(
        new NAMSquareButtonControl(slimIconArea, DefaultClickActionFunc, slimIconSVG), kCtrlTagSlimmableIcon)
      ->SetAnimationEndActionFunction(showSlimOverlay)
      ->Hide(true);

    pGraphics->AttachControl(new ISVGSwitchControl(irSwitchArea, {irIconOffSVG, irIconOnSVG}, kIRToggle))
      ->Hide(true); // AMPRYX skin: no bypass icon; the parameter remains automatable
    pGraphics->AttachControl(
      new ThemedFileBrowserControl(irArea, kMsgTagClearIR, defaultIRString.c_str(), "wav", loadIRCompletionHandler,
                                   style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG, fileBackgroundBitmap,
                                   globeSVG, "Get IRs", getUrl, namtheme::Accent()),
      kCtrlTagIRFileBrowser);
    pGraphics->AttachControl(new ThemedSwitchControl(ngToggleArea, kNoiseGateActive, "NOISE GATE", style));
    pGraphics->AttachControl(new ThemedSwitchControl(eqToggleArea, kEQActive, "EQ", style));

    // The knobs
    pGraphics->AttachControl(new ThemedKnobControl(inputKnobArea, kInputLevel, "INPUT", style));
    pGraphics->AttachControl(new ThemedKnobControl(noiseGateArea, kNoiseGateThreshold, "GATE", style));
    pGraphics->AttachControl(new ThemedKnobControl(bassKnobArea, kToneBass, "BASS", style), -1, "EQ_KNOBS");
    pGraphics->AttachControl(new ThemedKnobControl(midKnobArea, kToneMid, "MIDDLE", style), -1, "EQ_KNOBS");
    pGraphics->AttachControl(new ThemedKnobControl(trebleKnobArea, kToneTreble, "TREBLE", style), -1, "EQ_KNOBS");
    pGraphics->AttachControl(new ThemedKnobControl(outputKnobArea, kOutputLevel, "OUTPUT", style));
    // Tone Morph: the MORPH knob (top row) + the A TONE / B TONE cards.
    pGraphics->AttachControl(new NAMMorphKnobControl(morphKnobArea));
    pGraphics->AttachControl(new NAMMorphCardsControl(morphCardsArea), kCtrlTagMorphCards)
      ->As<NAMMorphCardsControl>()
      ->SetTexture(asciiToneABitmap);
    pGraphics->AttachControl(
      new NAMDoubleTrackSwitch(doubleTrackSwitchArea, NAMDoubleTrackSwitch::kToggleDoubleTrack, "DOUBLE TRACK"));
    pGraphics->AttachControl(
      new NAMDoubleTrackSwitch(bassCenterSwitchArea, NAMDoubleTrackSwitch::kToggleBassCenter, "BASS CENTER"));

    // The meters
    pGraphics->AttachControl(new ThemedMeterControl(inputMeterArea, style), kCtrlTagInputMeter);
    pGraphics->AttachControl(new ThemedMeterControl(outputMeterArea, style), kCtrlTagOutputMeter);

    // AMPRYX skin: bottom utility bar (version text at left; the
    // zoom/rack/accent/gear icons re-homed at right), plus the wide
    // SIGNAL CHAIN button that stays in the header.
    pGraphics->AttachControl(
      new AmpryxUtilityBarControl(utilityBarArea, "AMPRYX  \xC2\xB7  NIGHTFALL  v" PLUG_VERSION_STR));
    const float ubStep = 28.0f;
    const auto ubGearArea = utilityBarArea.GetFromRight(34.0f).GetCentredInside(24.0f);
    const auto ubAccentArea = ubGearArea.GetTranslated(-ubStep, 0.0f);
    const auto ubRackArea = ubAccentArea.GetTranslated(-ubStep, 0.0f);
    const auto ubZoomArea = ubRackArea.GetTranslated(-ubStep, 0.0f);
    pGraphics->AttachControl(new NAMAccentPickerControl(ubAccentArea));
    pGraphics->AttachControl(new NAMRackButtonControl(ubRackArea));
    pGraphics->AttachControl(new NAMZoomButtonControl(ubZoomArea));
    pGraphics->AttachControl(new NAMChainButtonControl(chainButtonArea));

    // Settings/help/about box opener (gear, far right of the utility bar).
    pGraphics->AttachControl(new NAMCircleButtonControl(
      ubGearArea,
      [pGraphics](IControl* pCaller) {
        pGraphics->GetControlWithTag(kCtrlTagSettingsBox)->As<NAMSettingsPageControl>()->HideAnimated(false);
      },
      gearSVG));

    // TONE3000 opener button (attached BEFORE the detail panel so the panel
    // slides over it instead of the button poking through).
    pGraphics->AttachControl(new NAMT3KButtonControl(t3kButtonArea));

    // Tone sidebar (left) and favorites bar (bottom) - always visible.
    pGraphics->AttachControl(
      new NAMToneSidebarControl(sidebarArea, loadModelCompletionHandler, loadIRCompletionHandler), kCtrlTagToneSidebar);
    pGraphics->AttachControl(
      new NAMFavoritesBarControl(favoritesArea, loadModelCompletionHandler, loadIRCompletionHandler),
      kCtrlTagFavoritesBar);

    // Tone detail panel: slides in to the right of the sidebar when a tone
    // card is clicked; hosts the variant picker.
    pGraphics
      ->AttachControl(new NAMToneDetailControl(b.GetReducedFromLeft(kSidebarWidth).GetFromLeft(kDetailPanelWidth),
                                               loadModelCompletionHandler, loadIRCompletionHandler),
                      kCtrlTagToneDetail)
      ->Hide(true);

    // TONE3000 live search browser overlay (the opener button is attached
    // earlier, underneath the tone detail panel).
    pGraphics
      ->AttachControl(
        new NAMTone3000BrowserControl(mainB, loadModelCompletionHandler, loadIRCompletionHandler), kCtrlTagTone3000)
      ->Hide(true);

    pGraphics
      ->AttachControl(new NAMSettingsPageControl(mainB, backgroundBitmap, inputLevelBackgroundBitmap,
                                                 switchHandleBitmap, crossSVG, style, radioButtonStyle),
                      kCtrlTagSettingsBox)
      ->Hide(true);

    const auto slimKnobArea = mainB.GetCentredInside(100.f, NAM_KNOB_HEIGHT + 24.f);
    pGraphics->AttachControl(new NAMSlimOverlayBackdropControl(mainB, hideSlimOverlay), kCtrlTagSlimOverlayBackdrop)
      ->Hide(true);
    pGraphics
      ->AttachControl(new NAMKnobControl(slimKnobArea, kSlim, "Slim", style, knobBackgroundBitmap), kCtrlTagSlimKnob)
      ->Hide(true);

    // Rack view overlay (shown by shrinking the window).
    pGraphics
      ->AttachControl(new NAMRackViewControl(b, loadModelCompletionHandler, loadIRCompletionHandler), kCtrlTagRackView)
      ->Hide(true);

    // Signal-chain view overlay (topmost; shown by growing the window). Its
    // bounds cover the full possible height so the stacked units fit.
    pGraphics
      ->AttachControl(new NAMChainViewControl(IRECT(0.0f, 0.0f, (float)PLUG_WIDTH, kChainViewHeight),
                                              loadModelCompletionHandler, loadIRCompletionHandler),
                      kCtrlTagChainView)
      ->Hide(true);

    pGraphics->ForAllControlsFunc([](IControl* pControl) {
      pControl->SetMouseEventsWhenDisabled(true);
      pControl->SetMouseOverWhenDisabled(true);
    });

    // Restore rack/chain mode across editor close/reopen (the mode lives on
    // the plugin instance, which survives; the UI is rebuilt from scratch).
    if (mToneChainMode)
    {
      if (IControl* pChain = pGraphics->GetControlWithTag(kCtrlTagChainView))
        pChain->Hide(false);
      pGraphics->Resize(PLUG_WIDTH, (int)kChainViewHeight, pGraphics->GetDrawScale());
    }
    else if (mToneRackMode)
    {
      if (IControl* pRack = pGraphics->GetControlWithTag(kCtrlTagRackView))
        pRack->Hide(false);
      pGraphics->Resize(PLUG_WIDTH, (int)kRackViewHeight, pGraphics->GetDrawScale());
    }
    else
    {
      pGraphics->Resize(PLUG_WIDTH, PLUG_HEIGHT, pGraphics->GetDrawScale());
    }

    // Restore the saved UI scale (this is how the standalone app gets big
    // enough to fill the screen; also applies inside a DAW).
    {
      const double savedScale = tonegallery::LoadSavedUIScale();
      if (std::abs((double)pGraphics->GetDrawScale() - savedScale) > 0.01)
        pGraphics->Resize(pGraphics->Width(), pGraphics->Height(), (float)savedScale);
    }

    // Restore the "now playing" display (sidebar glow, favorites, detail
    // panel, rack screen) from the model that's actually loaded in the DSP.
    if (mNAMPath.GetLength())
    {
      try
      {
        const auto modelDir = std::filesystem::u8path(mNAMPath.Get()).parent_path();
        const auto entries = tonegallery::ScanToneLibrary(tonegallery::GetToneLibraryRoot());
        for (const auto& entry : entries)
        {
          if (tonegallery::UTF8ToPath(entry.directory) == modelDir)
          {
            tonegallery::NotifyNowPlaying(pGraphics, entry, mNAMPath.Get(),
                                          mIRPath.GetLength() ? mIRPath.Get() : std::string(),
                                          /* force: this really is the main tone */ true);
            break;
          }
        }
      }
      catch (const std::exception&)
      {
      }
    }

    // Apply a saved custom accent color to all style-based controls.
    {
      const IColor ac = tonegallery::AccentColor();
      const IColor def = PluginColors::NAM_THEMECOLOR;
      if (ac.R != def.R || ac.G != def.G || ac.B != def.B)
      {
        char code[10];
        snprintf(code, sizeof(code), "#%02X%02X%02X", ac.R, ac.G, ac.B);
        mHighLightColor.Set(code);
        pGraphics->ForStandardControlsFunc([&](IControl* pControl) {
          if (auto* pVectorBase = pControl->As<IVectorBase>())
          {
            pVectorBase->SetColor(kX1, ac);
            pVectorBase->SetColor(kPR, ac.WithOpacity(0.3f));
            pVectorBase->SetColor(kFR, ac.WithOpacity(0.4f));
            pVectorBase->SetColor(kX3, ac.WithContrast(0.1f));
          }
        });
      }
    }

    // pGraphics->GetControlWithTag(kCtrlTagOutNorm)->SetMouseEventsWhenDisabled(false);
    // pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetMouseEventsWhenDisabled(false);
  };
}

NeuralAmpModeler::~NeuralAmpModeler()
{
  _DeallocateIOPointers();
}

void NeuralAmpModeler::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const size_t numChannelsExternalIn = (size_t)NInChansConnected();
  const size_t numChannelsExternalOut = (size_t)NOutChansConnected();
  const size_t numChannelsInternal = kNumChannelsInternal;
  const size_t numFrames = (size_t)nFrames;
  const double sampleRate = GetSampleRate();

  // Disable floating point denormals
  std::fenv_t fe_state;
  std::feholdexcept(&fe_state);
  disable_denormals();

  _PrepareBuffers(numChannelsInternal, numFrames);
  // Input is collapsed to mono in preparation for the NAM.
  _ProcessInput(inputs, numFrames, numChannelsExternalIn, numChannelsInternal);
  _ApplyDSPStaging();
  const bool noiseGateActive = GetParam(kNoiseGateActive)->Value();
  const bool toneStackActive = GetParam(kEQActive)->Value();

  // Noise gate trigger
  sample** triggerOutput = mInputPointers;
  if (noiseGateActive)
  {
    const double time = 0.01;
    const double threshold = GetParam(kNoiseGateThreshold)->Value(); // GetParam...
    const double ratio = 0.1; // Quadratic...
    const double openTime = 0.005;
    const double holdTime = 0.01;
    const double closeTime = 0.05;
    const dsp::noise_gate::TriggerParams triggerParams(time, threshold, ratio, openTime, holdTime, closeTime);
    mNoiseGateTrigger.SetParams(triggerParams);
    mNoiseGateTrigger.SetSampleRate(sampleRate);
    triggerOutput = mNoiseGateTrigger.Process(mInputPointers, numChannelsInternal, numFrames);
  }

  // Tone Gallery fork: unit 1 of the signal chain can be bypassed.
  const bool mainUnitEnabled = mChainMainEnabled.load();
  const bool irOn = GetParam(kIRToggle)->Value();
  // Tone Morph (main unit): when the morph knob is up and a B tone is loaded,
  // run a second complete tone (mModelB -> mIRB) in parallel and equal-power
  // crossfade between them. morph 0 or no B => the original path runs untouched.
  const double mainMorph = mChainMainMorph.load();
  const bool mainMorphing = mainUnitEnabled && mModel != nullptr && mModelB != nullptr && mainMorph > 0.0001;
  if (mainMorphing)
  {
    if (mMorphArrayA.size() < numFrames)
      mMorphArrayA.resize(numFrames, 0.0);
    if (mMorphArrayB.size() < numFrames)
      mMorphArrayB.resize(numFrames, 0.0);
    // A branch: main model -> main IR
    sample* aScratch = mMorphArrayA.data();
    mModel->process(triggerOutput, &aScratch, nFrames);
    sample** aPtr = &aScratch;
    if (mIR != nullptr && irOn)
      aPtr = mIR->Process(aPtr, numChannelsInternal, numFrames);
    // B branch: B model -> B IR
    sample* bScratch = mMorphArrayB.data();
    mModelB->process(triggerOutput, &bScratch, nFrames);
    sample** bPtr = &bScratch;
    if (mIRB != nullptr && irOn)
      bPtr = mIRB->Process(bPtr, numChannelsInternal, numFrames);
    // Equal-power crossfade into the main output buffer.
    const double gA = std::cos(mainMorph * 1.5707963267948966);
    const double gB = std::sin(mainMorph * 1.5707963267948966);
    for (size_t s = 0; s < numFrames; s++)
      mOutputPointers[0][s] = aPtr[0][s] * gA + bPtr[0][s] * gB;
  }
  else if (mModel != nullptr && mainUnitEnabled)
  {
    mModel->process(triggerOutput, mOutputPointers, nFrames);
  }
  else
  {
    _FallbackDSP(triggerOutput, mOutputPointers, numChannelsInternal, numFrames);
  }
  // Apply the noise gate after the NAM
  sample** gateGainOutput =
    noiseGateActive ? mNoiseGateGain.Process(mOutputPointers, numChannelsInternal, numFrames) : mOutputPointers;

  sample** toneStackOutPointers = (toneStackActive && mToneStack != nullptr)
                                    ? mToneStack->Process(gateGainOutput, numChannelsInternal, nFrames)
                                    : gateGainOutput;

  // In morph mode the per-tone IRs already ran above, so don't run the main IR again.
  sample** irPointers = toneStackOutPointers;
  if (!mainMorphing && mIR != nullptr && irOn && mainUnitEnabled)
    irPointers = mIR->Process(toneStackOutPointers, numChannelsInternal, numFrames);

  // --- Tone Gallery fork: signal chain -------------------------------------
  // Unit 1's level, then the extra units in series (each model -> IR -> level).
  {
    const double mainGain = DBToAmp(mChainMainLevelDB.load());
    if (mainGain != 1.0 && mainUnitEnabled)
      for (size_t s = 0; s < numFrames; s++)
        irPointers[0][s] *= mainGain;
  }
  sample** chainPointers = irPointers;
  for (int ci = 0; ci < kNumChainSlots; ci++)
  {
    ChainSlot& slot = mChainSlots[ci];
    if (!slot.enabled.load())
      continue;
    const bool haveSlotDSP =
      (slot.model != nullptr) || (slot.ir != nullptr) || (slot.modelB != nullptr) || (slot.irB != nullptr);
    if (!haveSlotDSP)
      continue;
    // Per-unit input gain (drives the unit harder or softer). Shared by both
    // morph branches so A and B are hit at the same level.
    const double slotInGain = DBToAmp(slot.inputDB.load());
    if (slotInGain != 1.0)
      for (size_t s = 0; s < numFrames; s++)
        chainPointers[0][s] *= slotInGain;

    // Tone Morph (this unit): blend a second complete tone (modelB -> irB) in
    // parallel. morph 0 or no B => the original single-tone path runs.
    const double slotMorph = slot.morph.load();
    const bool slotHasB = (slot.modelB != nullptr) || (slot.irB != nullptr);
    const bool slotMorphing = slotHasB && slotMorph > 0.0001;
    if (slotMorphing)
    {
      if (mChainArrays[ci].size() != numFrames)
        mChainArrays[ci].resize(numFrames, 0.0);
      if (mChainArraysB[ci].size() != numFrames)
        mChainArraysB[ci].resize(numFrames, 0.0);
      // A branch: model -> IR
      sample** aPtr = chainPointers;
      if (slot.model != nullptr)
      {
        mChainScratchPointers[ci] = mChainArrays[ci].data();
        slot.model->process(chainPointers, &mChainScratchPointers[ci], nFrames);
        aPtr = &mChainScratchPointers[ci];
      }
      if (slot.ir != nullptr)
        aPtr = slot.ir->Process(aPtr, numChannelsInternal, numFrames);
      // B branch: modelB -> irB (fed the same per-unit input)
      sample** bPtr = chainPointers;
      if (slot.modelB != nullptr)
      {
        mChainScratchPointersB[ci] = mChainArraysB[ci].data();
        slot.modelB->process(chainPointers, &mChainScratchPointersB[ci], nFrames);
        bPtr = &mChainScratchPointersB[ci];
      }
      if (slot.irB != nullptr)
        bPtr = slot.irB->Process(bPtr, numChannelsInternal, numFrames);
      // Equal-power crossfade back into this unit's A scratch buffer.
      const double gA = std::cos(slotMorph * 1.5707963267948966);
      const double gB = std::sin(slotMorph * 1.5707963267948966);
      for (size_t s = 0; s < numFrames; s++)
        mChainArrays[ci][s] = aPtr[0][s] * gA + bPtr[0][s] * gB;
      mChainScratchPointers[ci] = mChainArrays[ci].data();
      chainPointers = &mChainScratchPointers[ci];
    }
    else if (slot.model != nullptr)
    {
      if (mChainArrays[ci].size() != numFrames)
        mChainArrays[ci].resize(numFrames, 0.0);
      mChainScratchPointers[ci] = mChainArrays[ci].data();
      slot.model->process(chainPointers, &mChainScratchPointers[ci], nFrames);
      chainPointers = &mChainScratchPointers[ci];
    }
    // Per-unit EQ; 5/5/5 = flat = skipped entirely.
    const double sb = slot.bass.load(), sm = slot.middle.load(), st = slot.treble.load();
    if (mChainToneStacks[ci] != nullptr
        && (std::abs(sb - 5.0) > 0.01 || std::abs(sm - 5.0) > 0.01 || std::abs(st - 5.0) > 0.01))
      chainPointers = mChainToneStacks[ci]->Process(chainPointers, numChannelsInternal, nFrames);
    // In morph mode the per-tone IRs already ran above.
    if (!slotMorphing && slot.ir != nullptr)
      chainPointers = slot.ir->Process(chainPointers, numChannelsInternal, numFrames);
    const double slotGain = DBToAmp(slot.levelDB.load());
    if (slotGain != 1.0)
      for (size_t s = 0; s < numFrames; s++)
        chainPointers[0][s] *= slotGain;
  }
  irPointers = chainPointers;

  // And the HPF for DC offset (Issue 271)
  const double highPassCutoffFreq = kDCBlockerFrequency;
  // const double lowPassCutoffFreq = 20000.0;
  const recursive_linear_filter::HighPassParams highPassParams(sampleRate, highPassCutoffFreq);
  // const recursive_linear_filter::LowPassParams lowPassParams(sampleRate, lowPassCutoffFreq);
  mHighPass.SetParams(highPassParams);
  // mLowPass.SetParams(lowPassParams);
  sample** hpfPointers = mHighPass.Process(irPointers, numChannelsInternal, numFrames);
  // sample** lpfPointers = mLowPass.Process(hpfPointers, numChannelsInternal, numFrames);

  // Let's get outta here
  // This is where we exit mono for whatever the output requires.
  // Tone Gallery fork: the instant double-track is the very last step -- it
  // takes this finished mono tone and spreads it into two hard-panned takes.
  // Runs before the FP state is restored so its one-pole low-pass (bass-center)
  // stays denormal-protected.
  if (mDoubleTrackActive.load() && numChannelsExternalOut >= 2)
  {
    if (mDTOutL.size() < numFrames)
      mDTOutL.resize(numFrames, 0.0);
    if (mDTOutR.size() < numFrames)
      mDTOutR.resize(numFrames, 0.0);
    _RenderDoubleTrack(hpfPointers[0], numFrames, sampleRate, mDTOutL.data(), mDTOutR.data());
    _ProcessOutputStereo(mDTOutL.data(), mDTOutR.data(), outputs, numFrames, numChannelsExternalOut);
  }
  else
  {
    mDTActivePrev = false;
    _ProcessOutput(hpfPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
  }
  // _ProcessOutput(lpfPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);

  // restore previous floating point state
  std::feupdateenv(&fe_state);
  // * Output of input leveling (inputs -> mInputPointers),
  // * Output of output leveling (mOutputPointers -> outputs)
  _UpdateMeters(mInputPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
}

void NeuralAmpModeler::OnReset()
{
  const auto sampleRate = GetSampleRate();
  const int maxBlockSize = GetBlockSize();

  // Tail is because the HPF DC blocker has a decay.
  // 10 cycles should be enough to pass the VST3 tests checking tail behavior.
  // I'm ignoring the model & IR, but it's not the end of the world.
  const int tailCycles = 10;
  SetTailSize(tailCycles * (int)(sampleRate / kDCBlockerFrequency));
  mInputSender.Reset(sampleRate);
  mOutputSender.Reset(sampleRate);
  // If there is a model or IR loaded, they need to be checked for resampling.
  _ResetModelAndIR(sampleRate, GetBlockSize());
  mToneStack->Reset(sampleRate, maxBlockSize);
  // Tone Gallery fork: reset the chain slots' tone stacks (and re-apply
  // their stored EQ, since Reset can clear filter state).
  for (int ci = 0; ci < kNumChainSlots; ci++)
  {
    if (mChainToneStacks[ci] != nullptr)
    {
      mChainToneStacks[ci]->Reset(sampleRate, maxBlockSize);
      mChainToneStacks[ci]->SetParam("bass", mChainSlots[ci].bass.load());
      mChainToneStacks[ci]->SetParam("middle", mChainSlots[ci].middle.load());
      mChainToneStacks[ci]->SetParam("treble", mChainSlots[ci].treble.load());
    }
  }
  // Instant double-track delay line (sized for up to ~60 ms of history).
  {
    int dtLen = (int)(0.06 * sampleRate) + 8;
    if (dtLen < 64)
      dtLen = 64;
    mDTBuf.assign((size_t)dtLen, 0.0);
    mDTWritePos = 0;
    mDTPhaseL = 0.0;
    mDTPhaseR = 1.7;
    mDTLowState = 0.0;
    mDTActivePrev = false;
  }
  _UpdateLatency();
}

void NeuralAmpModeler::OnIdle()
{
  mInputSender.TransmitData(*this);
  mOutputSender.TransmitData(*this);

  if (mNewModelLoadedInDSP)
  {
    if (auto* pGraphics = GetUI())
    {
      _UpdateControlsFromModel();
      mNewModelLoadedInDSP = false;
    }
  }
  if (mModelCleared)
  {
    if (auto* pGraphics = GetUI())
    {
      // FIXME -- need to disable only the "normalized" model
      // pGraphics->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
      static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->ClearModelInfo();
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimKnob))
        p->Hide(true);
      pGraphics->SetAllControlsDirty();
      mModelCleared = false;
    }
  }
}

bool NeuralAmpModeler::SerializeState(IByteChunk& chunk) const
{
  // If this isn't here when unserializing, then we know we're dealing with something before v0.8.0.
  WDL_String header("###NeuralAmpModeler###"); // Don't change this!
  chunk.PutStr(header.Get());
  // Plugin version, so we can load legacy serialized states in the future!
  WDL_String version(PLUG_VERSION_STR);
  chunk.PutStr(version.Get());
  // Model directory (don't serialize the model itself; we'll just load it again
  // when we unserialize)
  chunk.PutStr(mNAMPath.Get());
  chunk.PutStr(mIRPath.Get());
  const bool ok = SerializeParams(chunk);

  // Tone Gallery fork: append the signal-chain block. Old versions of the
  // plugin simply never read this far, so this stays backwards-compatible.
  chunk.PutStr("###NAMChainV5###");
  int mainEnabled = mChainMainEnabled.load() ? 1 : 0;
  double mainLevel = mChainMainLevelDB.load();
  int chainMode = mToneChainMode ? 1 : 0;
  chunk.Put(&mainEnabled);
  chunk.Put(&mainLevel);
  chunk.Put(&chainMode);
  for (int ci = 0; ci < kNumChainSlots; ci++)
  {
    const ChainSlot& slot = mChainSlots[ci];
    chunk.PutStr(slot.modelPath.Get());
    chunk.PutStr(slot.irPath.Get());
    chunk.PutStr(slot.tonePath.Get());
    int enabled = slot.enabled.load() ? 1 : 0;
    double level = slot.levelDB.load();
    chunk.Put(&enabled);
    chunk.Put(&level);
    // V2: per-unit knob settings
    double inputDB = slot.inputDB.load();
    double bass = slot.bass.load();
    double middle = slot.middle.load();
    double treble = slot.treble.load();
    chunk.Put(&inputDB);
    chunk.Put(&bass);
    chunk.Put(&middle);
    chunk.Put(&treble);
    // V4: this slot's Tone Morph B tone + morph amount
    chunk.PutStr(slot.modelBPath.Get());
    chunk.PutStr(slot.irBPath.Get());
    chunk.PutStr(slot.toneBPath.Get());
    double morph = slot.morph.load();
    chunk.Put(&morph);
  }
  // V3: which rig preset is loaded (so SAVE knows what to overwrite)
  chunk.PutStr(mRigPresetRel.Get());
  // V4: the main unit's Tone Morph B tone + morph amount
  chunk.PutStr(mNAMPathB.Get());
  chunk.PutStr(mIRPathB.Get());
  chunk.PutStr(mMainToneBPath.Get());
  double mainMorph = mChainMainMorph.load();
  chunk.Put(&mainMorph);
  // V5: instant double-track state (final output stage)
  int dtActive = mDoubleTrackActive.load() ? 1 : 0;
  int dtBassCenter = mDoubleTrackBassCenter.load() ? 1 : 0;
  chunk.Put(&dtActive);
  chunk.Put(&dtBassCenter);
  return ok;
}

int NeuralAmpModeler::_UnserializeChain(const iplug::IByteChunk& chunk, int startPos)
{
  // Defaults first, so projects saved before the chain existed load clean.
  mChainMainEnabled = true;
  mChainMainLevelDB = 0.0;
  mToneChainMode = false;
  mRigPresetRel.Set("");
  // Tone Morph defaults (main unit)
  mChainMainMorph = 0.0;
  mNAMPathB.Set("");
  mIRPathB.Set("");
  mMainToneBPath.Set("");
  mShouldRemoveModelB = true;
  mShouldRemoveIRB = true;
  // Instant double-track defaults (off for projects saved before it existed)
  mDoubleTrackActive = false;
  mDoubleTrackBassCenter = false;
  for (int ci = 0; ci < kNumChainSlots; ci++)
  {
    ChainSlot& slot = mChainSlots[ci];
    slot.enabled = true;
    slot.levelDB = 0.0;
    slot.inputDB = 0.0;
    slot.bass = 5.0;
    slot.middle = 5.0;
    slot.treble = 5.0;
    slot.tonePath.Set("");
    slot.modelPath.Set("");
    slot.irPath.Set("");
    slot.removeModel = true;
    slot.removeIR = true;
    // Tone Morph defaults (slot)
    slot.morph = 0.0;
    slot.modelBPath.Set("");
    slot.irBPath.Set("");
    slot.toneBPath.Set("");
    slot.removeModelB = true;
    slot.removeIRB = true;
  }

  WDL_String marker;
  int pos = chunk.GetStr(marker, startPos);
  const bool isV1 = pos > startPos && strcmp(marker.Get(), "###NAMChainV1###") == 0;
  const bool isV2 = pos > startPos && strcmp(marker.Get(), "###NAMChainV2###") == 0;
  const bool isV3 = pos > startPos && strcmp(marker.Get(), "###NAMChainV3###") == 0;
  const bool isV4 = pos > startPos && strcmp(marker.Get(), "###NAMChainV4###") == 0;
  const bool isV5 = pos > startPos && strcmp(marker.Get(), "###NAMChainV5###") == 0;
  if (!isV1 && !isV2 && !isV3 && !isV4 && !isV5)
    return startPos; // no chain block in this save

  int mainEnabled = 1, chainMode = 0;
  double mainLevel = 0.0;
  pos = chunk.Get(&mainEnabled, pos);
  if (pos < 0)
    return startPos;
  pos = chunk.Get(&mainLevel, pos);
  if (pos < 0)
    return startPos;
  pos = chunk.Get(&chainMode, pos);
  if (pos < 0)
    return startPos;
  mChainMainEnabled = mainEnabled != 0;
  mChainMainLevelDB = mainLevel;
  mToneChainMode = chainMode != 0;

  for (int ci = 0; ci < kNumChainSlots; ci++)
  {
    ChainSlot& slot = mChainSlots[ci];
    WDL_String modelPath, irPath, tonePath;
    int enabled = 1;
    double level = 0.0;
    pos = chunk.GetStr(modelPath, pos);
    if (pos < 0)
      return startPos;
    pos = chunk.GetStr(irPath, pos);
    if (pos < 0)
      return startPos;
    pos = chunk.GetStr(tonePath, pos);
    if (pos < 0)
      return startPos;
    pos = chunk.Get(&enabled, pos);
    if (pos < 0)
      return startPos;
    pos = chunk.Get(&level, pos);
    if (pos < 0)
      return startPos;
    double inputDB = 0.0, bass = 5.0, middle = 5.0, treble = 5.0;
    if (isV2 || isV3 || isV4 || isV5)
    {
      pos = chunk.Get(&inputDB, pos);
      if (pos < 0)
        return startPos;
      pos = chunk.Get(&bass, pos);
      if (pos < 0)
        return startPos;
      pos = chunk.Get(&middle, pos);
      if (pos < 0)
        return startPos;
      pos = chunk.Get(&treble, pos);
      if (pos < 0)
        return startPos;
    }
    // V4: this slot's Tone Morph B tone + morph amount
    WDL_String modelBPath, irBPath, toneBPath;
    double morph = 0.0;
    if (isV4 || isV5)
    {
      pos = chunk.GetStr(modelBPath, pos);
      if (pos < 0)
        return startPos;
      pos = chunk.GetStr(irBPath, pos);
      if (pos < 0)
        return startPos;
      pos = chunk.GetStr(toneBPath, pos);
      if (pos < 0)
        return startPos;
      pos = chunk.Get(&morph, pos);
      if (pos < 0)
        return startPos;
    }
    slot.enabled = enabled != 0;
    slot.levelDB = level;
    slot.inputDB = inputDB;
    slot.bass = bass;
    slot.middle = middle;
    slot.treble = treble;
    slot.morph = morph;
    if (mChainToneStacks[ci] != nullptr)
    {
      mChainToneStacks[ci]->SetParam("bass", bass);
      mChainToneStacks[ci]->SetParam("middle", middle);
      mChainToneStacks[ci]->SetParam("treble", treble);
    }
    SetChainTone(ci, modelPath.Get(), irPath.Get(), tonePath.Get());
    if (isV4 || isV5)
      StageUnitBTone(ci + 1, modelBPath.Get(), irBPath.Get(), toneBPath.Get());
  }
  // V3: the loaded rig preset path
  if (isV3 || isV4 || isV5)
  {
    WDL_String rigRel;
    const int p2 = chunk.GetStr(rigRel, pos);
    if (p2 > 0)
    {
      mRigPresetRel = rigRel;
      pos = p2;
    }
  }
  // V4: the main unit's Tone Morph B tone + morph amount
  if (isV4 || isV5)
  {
    WDL_String namB, irB, toneB;
    double mainMorph = 0.0;
    int p3 = chunk.GetStr(namB, pos);
    if (p3 > 0)
    {
      pos = p3;
      p3 = chunk.GetStr(irB, pos);
    }
    if (p3 > 0)
    {
      pos = p3;
      p3 = chunk.GetStr(toneB, pos);
    }
    if (p3 > 0)
    {
      pos = p3;
      p3 = chunk.Get(&mainMorph, pos);
    }
    if (p3 > 0)
    {
      pos = p3;
      mChainMainMorph = mainMorph;
      StageUnitBTone(0, namB.Get(), irB.Get(), toneB.Get());
    }
  }
  // V5: instant double-track state (final output stage)
  if (isV5)
  {
    int dtActive = 0, dtBassCenter = 0;
    int p4 = chunk.Get(&dtActive, pos);
    if (p4 > 0)
    {
      pos = p4;
      p4 = chunk.Get(&dtBassCenter, pos);
    }
    if (p4 > 0)
    {
      pos = p4;
      mDoubleTrackActive = dtActive != 0;
      mDoubleTrackBassCenter = dtBassCenter != 0;
    }
  }
  return pos;
}

int NeuralAmpModeler::UnserializeState(const IByteChunk& chunk, int startPos)
{
  // Look for the expected header. If it's there, then we'll know what to do.
  WDL_String header;
  int pos = startPos;
  pos = chunk.GetStr(header, pos);

  const char* kExpectedHeader = "###NeuralAmpModeler###";
  int endPos;
  if (strcmp(header.Get(), kExpectedHeader) == 0)
  {
    endPos = _UnserializeStateWithKnownVersion(chunk, pos);
  }
  else
  {
    endPos = _UnserializeStateWithUnknownVersion(chunk, startPos);
  }
  // Tone Gallery fork: the signal-chain block sits after everything else.
  if (endPos > 0)
    endPos = _UnserializeChain(chunk, endPos);
  return endPos;
}

void NeuralAmpModeler::OnUIOpen()
{
  Plugin::OnUIOpen();

  if (mNAMPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
    // If it's not loaded yet, then mark as failed.
    // If it's yet to be loaded, then the completion handler will set us straight once it runs.
    if (mModel == nullptr && mStagedModel == nullptr)
      SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
  }

  if (mIRPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
    if (mIR == nullptr && mStagedIR == nullptr)
      SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadFailed);
  }

  if (mModel != nullptr)
  {
    _UpdateControlsFromModel();
  }

  // Tone Gallery fork: if the window reopened while a chain unit was being
  // edited, the browsers should show that unit's files, not the main tone's.
  if (mChainEditSlot >= 1)
    _UpdateBrowsersForEditSlot();
}

void NeuralAmpModeler::OnParamChange(int paramIdx)
{
  // Tone Gallery fork: while a rack unit (2..4) is being edited, the main
  // knobs drive that chain slot's own settings instead of the globals.
  if (mChainEditSlot >= 1 && mChainEditSlot <= kNumChainSlots)
  {
    const int ci = mChainEditSlot - 1;
    switch (paramIdx)
    {
      case kInputLevel: mChainSlots[ci].inputDB = GetParam(paramIdx)->Value(); return;
      case kOutputLevel: mChainSlots[ci].levelDB = GetParam(paramIdx)->Value(); return;
      case kToneBass:
        mChainSlots[ci].bass = GetParam(paramIdx)->Value();
        if (mChainToneStacks[ci] != nullptr)
          mChainToneStacks[ci]->SetParam("bass", GetParam(paramIdx)->Value());
        return;
      case kToneMid:
        mChainSlots[ci].middle = GetParam(paramIdx)->Value();
        if (mChainToneStacks[ci] != nullptr)
          mChainToneStacks[ci]->SetParam("middle", GetParam(paramIdx)->Value());
        return;
      case kToneTreble:
        mChainSlots[ci].treble = GetParam(paramIdx)->Value();
        if (mChainToneStacks[ci] != nullptr)
          mChainToneStacks[ci]->SetParam("treble", GetParam(paramIdx)->Value());
        return;
      default: break; // gate, toggles, etc. stay global
    }
  }
  switch (paramIdx)
  {
    // Changes to the input gain
    case kCalibrateInput:
    case kInputCalibrationLevel:
    case kInputLevel: _SetInputGain(); break;
    // Changes to the output gain
    case kOutputLevel:
    case kOutputMode: _SetOutputGain(); break;
    // Tone stack:
    case kToneBass: mToneStack->SetParam("bass", GetParam(paramIdx)->Value()); break;
    case kToneMid: mToneStack->SetParam("middle", GetParam(paramIdx)->Value()); break;
    case kToneTreble: mToneStack->SetParam("treble", GetParam(paramIdx)->Value()); break;
    case kSlim: _ApplySlimParamToLoadedNAMs(); break;
    default: break;
  }
}

void NeuralAmpModeler::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (auto pGraphics = GetUI())
  {
    bool active = GetParam(paramIdx)->Bool();

    switch (paramIdx)
    {
      case kNoiseGateActive: pGraphics->GetControlWithParamIdx(kNoiseGateThreshold)->SetDisabled(!active); break;
      case kEQActive:
        pGraphics->ForControlInGroup("EQ_KNOBS", [active](IControl* pControl) { pControl->SetDisabled(!active); });
        break;
      case kIRToggle: pGraphics->GetControlWithTag(kCtrlTagIRFileBrowser)->SetDisabled(!active); break;
      default: break;
    }
  }
}

bool NeuralAmpModeler::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  switch (msgTag)
  {
    case kMsgTagClearModel:
      // Tone Gallery fork: the browser's X clears the edited chain unit.
      if (mChainEditTargetB)
      {
        if (mChainEditSlot >= 1 && mChainEditSlot <= kNumChainSlots)
        {
          mChainSlots[mChainEditSlot - 1].modelBPath.Set("");
          mChainSlots[mChainEditSlot - 1].removeModelB = true;
        }
        else
        {
          mNAMPathB.Set("");
          mShouldRemoveModelB = true;
        }
      }
      else if (mChainEditSlot >= 1 && mChainEditSlot <= kNumChainSlots)
      {
        mChainSlots[mChainEditSlot - 1].modelPath.Set("");
        mChainSlots[mChainEditSlot - 1].removeModel = true;
      }
      else
        mShouldRemoveModel = true;
      return true;
    case kMsgTagClearIR:
      if (mChainEditTargetB)
      {
        if (mChainEditSlot >= 1 && mChainEditSlot <= kNumChainSlots)
        {
          mChainSlots[mChainEditSlot - 1].irBPath.Set("");
          mChainSlots[mChainEditSlot - 1].removeIRB = true;
        }
        else
        {
          mIRPathB.Set("");
          mShouldRemoveIRB = true;
        }
      }
      else if (mChainEditSlot >= 1 && mChainEditSlot <= kNumChainSlots)
      {
        mChainSlots[mChainEditSlot - 1].irPath.Set("");
        mChainSlots[mChainEditSlot - 1].removeIR = true;
      }
      else
        mShouldRemoveIR = true;
      return true;
    case kMsgTagHighlightColor:
    {
      mHighLightColor.Set((const char*)pData);

      if (GetUI())
      {
        GetUI()->ForStandardControlsFunc([&](IControl* pControl) {
          if (auto* pVectorBase = pControl->As<IVectorBase>())
          {
            IColor color = IColor::FromColorCodeStr(mHighLightColor.Get());

            pVectorBase->SetColor(kX1, color);
            pVectorBase->SetColor(kPR, color.WithOpacity(0.3f));
            pVectorBase->SetColor(kFR, color.WithOpacity(0.4f));
            pVectorBase->SetColor(kX3, color.WithContrast(0.1f));
          }
          pControl->GetUI()->SetAllControlsDirty();
        });
      }

      return true;
    }
    default: return false;
  }
}

// Private methods ============================================================

void NeuralAmpModeler::_AllocateIOPointers(const size_t nChans)
{
  if (mInputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mInputPointers without freeing");
  mInputPointers = new sample*[nChans];
  if (mInputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mOutputPointers without freeing");
  mOutputPointers = new sample*[nChans];
  if (mOutputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_ApplyDSPStaging()
{
  // Remove marked modules
  if (mShouldRemoveModel)
  {
    mModel = nullptr;
    mNAMPath.Set("");
    mShouldRemoveModel = false;
    mModelCleared = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mShouldRemoveIR)
  {
    mIR = nullptr;
    mIRPath.Set("");
    mShouldRemoveIR = false;
  }
  // Move things from staged to live
  if (mStagedModel != nullptr)
  {
    mModel = std::move(mStagedModel);
    mStagedModel = nullptr;
    mNewModelLoadedInDSP = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mStagedIR != nullptr)
  {
    mIR = std::move(mStagedIR);
    mStagedIR = nullptr;
  }
  // Tone Morph: the main unit's B model/IR stage the same way.
  if (mShouldRemoveModelB)
  {
    mModelB = nullptr;
    mNAMPathB.Set("");
    mShouldRemoveModelB = false;
  }
  if (mShouldRemoveIRB)
  {
    mIRB = nullptr;
    mIRPathB.Set("");
    mShouldRemoveIRB = false;
  }
  if (mStagedModelB != nullptr)
  {
    mModelB = std::move(mStagedModelB);
    mStagedModelB = nullptr;
  }
  if (mStagedIRB != nullptr)
  {
    mIRB = std::move(mStagedIRB);
    mStagedIRB = nullptr;
  }
  // Tone Gallery fork: the extra chain slots stage the same way.
  for (int ci = 0; ci < kNumChainSlots; ci++)
  {
    ChainSlot& slot = mChainSlots[ci];
    if (slot.removeModel)
    {
      slot.model = nullptr;
      slot.removeModel = false;
    }
    if (slot.removeIR)
    {
      slot.ir = nullptr;
      slot.removeIR = false;
    }
    if (slot.stagedModel != nullptr)
    {
      slot.model = std::move(slot.stagedModel);
      slot.stagedModel = nullptr;
    }
    if (slot.stagedIR != nullptr)
    {
      slot.ir = std::move(slot.stagedIR);
      slot.stagedIR = nullptr;
    }
    // Tone Morph: the slot's B model/IR.
    if (slot.removeModelB)
    {
      slot.modelB = nullptr;
      slot.removeModelB = false;
    }
    if (slot.removeIRB)
    {
      slot.irB = nullptr;
      slot.removeIRB = false;
    }
    if (slot.stagedModelB != nullptr)
    {
      slot.modelB = std::move(slot.stagedModelB);
      slot.stagedModelB = nullptr;
    }
    if (slot.stagedIRB != nullptr)
    {
      slot.irB = std::move(slot.stagedIRB);
      slot.stagedIRB = nullptr;
    }
  }
}

void NeuralAmpModeler::_DeallocateIOPointers()
{
  if (mInputPointers != nullptr)
  {
    delete[] mInputPointers;
    mInputPointers = nullptr;
  }
  if (mInputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
  {
    delete[] mOutputPointers;
    mOutputPointers = nullptr;
  }
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels,
                                    const size_t numFrames)
{
  for (auto c = 0; c < numChannels; c++)
    for (auto s = 0; s < numFrames; s++)
      mOutputArray[c][s] = mInputArray[c][s];
}

void NeuralAmpModeler::_ResetModelAndIR(const double sampleRate, const int maxBlockSize)
{
  // Model
  if (mStagedModel != nullptr)
  {
    mStagedModel->Reset(sampleRate, maxBlockSize);
  }
  else if (mModel != nullptr)
  {
    mModel->Reset(sampleRate, maxBlockSize);
  }

  // IR
  if (mStagedIR != nullptr)
  {
    const double irSampleRate = mStagedIR->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mStagedIR->GetData();
      mStagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
  else if (mIR != nullptr)
  {
    const double irSampleRate = mIR->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mIR->GetData();
      mStagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }

  // Tone Morph: reset the main unit's B model + IR.
  if (mStagedModelB != nullptr)
    mStagedModelB->Reset(sampleRate, maxBlockSize);
  else if (mModelB != nullptr)
    mModelB->Reset(sampleRate, maxBlockSize);
  if (mStagedIRB != nullptr)
  {
    if (mStagedIRB->GetSampleRate() != sampleRate)
    {
      const auto irData = mStagedIRB->GetData();
      mStagedIRB = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
  else if (mIRB != nullptr)
  {
    if (mIRB->GetSampleRate() != sampleRate)
    {
      const auto irData = mIRB->GetData();
      mStagedIRB = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }

  // Tone Gallery fork: same treatment for the extra chain slots.
  for (int ci = 0; ci < kNumChainSlots; ci++)
  {
    ChainSlot& slot = mChainSlots[ci];
    if (slot.stagedModel != nullptr)
      slot.stagedModel->Reset(sampleRate, maxBlockSize);
    else if (slot.model != nullptr)
      slot.model->Reset(sampleRate, maxBlockSize);

    if (slot.stagedIR != nullptr)
    {
      if (slot.stagedIR->GetSampleRate() != sampleRate)
      {
        const auto irData = slot.stagedIR->GetData();
        slot.stagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
      }
    }
    else if (slot.ir != nullptr)
    {
      if (slot.ir->GetSampleRate() != sampleRate)
      {
        const auto irData = slot.ir->GetData();
        slot.stagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
      }
    }
    // Tone Morph: reset the slot's B model + IR.
    if (slot.stagedModelB != nullptr)
      slot.stagedModelB->Reset(sampleRate, maxBlockSize);
    else if (slot.modelB != nullptr)
      slot.modelB->Reset(sampleRate, maxBlockSize);
    if (slot.stagedIRB != nullptr)
    {
      if (slot.stagedIRB->GetSampleRate() != sampleRate)
      {
        const auto irData = slot.stagedIRB->GetData();
        slot.stagedIRB = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
      }
    }
    else if (slot.irB != nullptr)
    {
      if (slot.irB->GetSampleRate() != sampleRate)
      {
        const auto irData = slot.irB->GetData();
        slot.stagedIRB = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
      }
    }
  }
}

void NeuralAmpModeler::_SetInputGain()
{
  iplug::sample inputGainDB = GetParam(kInputLevel)->Value();
  // Input calibration
  if ((mModel != nullptr) && (mModel->HasInputLevel()) && GetParam(kCalibrateInput)->Bool())
  {
    inputGainDB += GetParam(kInputCalibrationLevel)->Value() - mModel->GetInputLevel();
  }
  mInputGain = DBToAmp(inputGainDB);
}

void NeuralAmpModeler::_SetOutputGain()
{
  double gainDB = GetParam(kOutputLevel)->Value();
  if (mModel != nullptr)
  {
    const int outputMode = GetParam(kOutputMode)->Int();
    switch (outputMode)
    {
      case 1: // Normalized
        if (mModel->HasLoudness())
        {
          const double loudness = mModel->GetLoudness();
          const double targetLoudness = -18.0;
          gainDB += (targetLoudness - loudness);
        }
        break;
      case 2: // Calibrated
        if (mModel->HasOutputLevel())
        {
          const double inputLevel = GetParam(kInputCalibrationLevel)->Value();
          const double outputLevel = mModel->GetOutputLevel();
          gainDB += (outputLevel - inputLevel);
        }
        break;
      case 0: // Raw
      default: break;
    }
  }
  mOutputGain = DBToAmp(gainDB);
}

void NeuralAmpModeler::_ApplySlimParamToLoadedNAMs()
{
  const double v = GetParam(kSlim)->Value();
  auto apply = [v](ResamplingNAM* p) {
    if (p == nullptr)
      return;
    if (nam::SlimmableModel* s = p->GetSlimmableModel())
      s->SetSlimmableSize(v);
  };
  apply(mModel.get());
  apply(mStagedModel.get());
  // Tone Morph: the main unit's B model too
  apply(mModelB.get());
  apply(mStagedModelB.get());
  // Tone Gallery fork: chain models too
  for (int ci = 0; ci < kNumChainSlots; ci++)
  {
    apply(mChainSlots[ci].model.get());
    apply(mChainSlots[ci].stagedModel.get());
    apply(mChainSlots[ci].modelB.get());
    apply(mChainSlots[ci].stagedModelB.get());
  }
}

std::string NeuralAmpModeler::_StageModel(const WDL_String& modelPath)
{
  WDL_String previousNAMPath = mNAMPath;
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);

    // Check that the model has 1 input and 1 output channel
    if (model->NumInputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 input channel, but has " + std::to_string(model->NumInputChannels()));
    }
    if (model->NumOutputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 output channel, but has "
                               + std::to_string(model->NumOutputChannels()));
    }

    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
    {
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    }
    mStagedModel = std::move(temp);
    mNAMPath = modelPath;
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);

    if (mStagedModel != nullptr)
    {
      mStagedModel = nullptr;
    }
    mNAMPath = previousNAMPath;
    std::cerr << "Failed to read DSP module" << std::endl;
    std::cerr << e.what() << std::endl;
    return e.what();
  }
  return "";
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageIR(const WDL_String& irPath)
{
  // FIXME it'd be better for the path to be "staged" as well. Just in case the
  // path and the model got caught on opposite sides of the fence...
  WDL_String previousIRPath = mIRPath;
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath.Get());
    mStagedIR = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), sampleRate);
    wavState = mStagedIR->GetWavState();
  }
  catch (std::runtime_error& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    mIRPath = irPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
  }
  else
  {
    if (mStagedIR != nullptr)
    {
      mStagedIR = nullptr;
    }
    mIRPath = previousIRPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadFailed);
  }

  return wavState;
}

// --- Tone Gallery fork: signal chain loading --------------------------------

void NeuralAmpModeler::_StageChainModel(int slot, const char* modelPath)
{
  if (slot < 0 || slot >= kNumChainSlots)
    return;
  ChainSlot& cs = mChainSlots[slot];
  if (modelPath == nullptr || modelPath[0] == '\0')
  {
    cs.modelPath.Set("");
    cs.removeModel = true;
    return;
  }
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath);
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
    if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
      throw std::runtime_error("Chain models must be mono");
    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    cs.stagedModel = std::move(temp);
    cs.modelPath.Set(modelPath);
  }
  catch (const std::exception& e)
  {
    cs.stagedModel = nullptr;
    cs.modelPath.Set("");
    cs.removeModel = true;
    std::cerr << "Failed to load chain model: " << e.what() << std::endl;
  }
}

void NeuralAmpModeler::_StageChainIR(int slot, const char* irPath)
{
  if (slot < 0 || slot >= kNumChainSlots)
    return;
  ChainSlot& cs = mChainSlots[slot];
  if (irPath == nullptr || irPath[0] == '\0')
  {
    cs.irPath.Set("");
    cs.removeIR = true;
    return;
  }
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath);
    auto staged = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), GetSampleRate());
    if (staged->GetWavState() == dsp::wav::LoadReturnCode::SUCCESS)
    {
      cs.stagedIR = std::move(staged);
      cs.irPath.Set(irPath);
    }
    else
    {
      cs.irPath.Set("");
      cs.removeIR = true;
    }
  }
  catch (const std::exception& e)
  {
    cs.stagedIR = nullptr;
    cs.irPath.Set("");
    cs.removeIR = true;
    std::cerr << "Failed to load chain IR: " << e.what() << std::endl;
  }
}

void NeuralAmpModeler::SetChainTone(int slot, const char* modelPath, const char* irPath, const char* tonePath)
{
  if (slot < 0 || slot >= kNumChainSlots)
    return;
  _StageChainModel(slot, modelPath);
  _StageChainIR(slot, irPath);
  mChainSlots[slot].tonePath.Set(tonePath != nullptr ? tonePath : "");
}

// --- Tone Morph: B-side staging + per-unit API ------------------------------

std::string NeuralAmpModeler::_StageModelB(const WDL_String& modelPath)
{
  if (modelPath.GetLength() == 0)
  {
    mNAMPathB.Set("");
    mShouldRemoveModelB = true;
    return "";
  }
  WDL_String previousPath = mNAMPathB;
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
    if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
      throw std::runtime_error("Morph models must be mono");
    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    mStagedModelB = std::move(temp);
    mNAMPathB = modelPath;
  }
  catch (const std::exception& e)
  {
    mStagedModelB = nullptr;
    mNAMPathB = previousPath;
    std::cerr << "Failed to load morph model: " << e.what() << std::endl;
    return e.what();
  }
  return "";
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageIRB(const WDL_String& irPath)
{
  if (irPath.GetLength() == 0)
  {
    mIRPathB.Set("");
    mShouldRemoveIRB = true;
    return dsp::wav::LoadReturnCode::SUCCESS;
  }
  WDL_String previousPath = mIRPathB;
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath.Get());
    mStagedIRB = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), sampleRate);
    wavState = mStagedIRB->GetWavState();
  }
  catch (std::runtime_error& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Failed to load morph IR: " << e.what() << std::endl;
  }
  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    mIRPathB = irPath;
  }
  else
  {
    mStagedIRB = nullptr;
    mIRPathB = previousPath;
  }
  return wavState;
}

void NeuralAmpModeler::_StageChainModelB(int slot, const char* modelPath)
{
  if (slot < 0 || slot >= kNumChainSlots)
    return;
  ChainSlot& cs = mChainSlots[slot];
  if (modelPath == nullptr || modelPath[0] == '\0')
  {
    cs.modelBPath.Set("");
    cs.removeModelB = true;
    return;
  }
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath);
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
    if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
      throw std::runtime_error("Morph models must be mono");
    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    cs.stagedModelB = std::move(temp);
    cs.modelBPath.Set(modelPath);
  }
  catch (const std::exception& e)
  {
    cs.stagedModelB = nullptr;
    cs.modelBPath.Set("");
    cs.removeModelB = true;
    std::cerr << "Failed to load morph chain model: " << e.what() << std::endl;
  }
}

void NeuralAmpModeler::_StageChainIRB(int slot, const char* irPath)
{
  if (slot < 0 || slot >= kNumChainSlots)
    return;
  ChainSlot& cs = mChainSlots[slot];
  if (irPath == nullptr || irPath[0] == '\0')
  {
    cs.irBPath.Set("");
    cs.removeIRB = true;
    return;
  }
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath);
    auto staged = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), GetSampleRate());
    if (staged->GetWavState() == dsp::wav::LoadReturnCode::SUCCESS)
    {
      cs.stagedIRB = std::move(staged);
      cs.irBPath.Set(irPath);
    }
    else
    {
      cs.irBPath.Set("");
      cs.removeIRB = true;
    }
  }
  catch (const std::exception& e)
  {
    cs.stagedIRB = nullptr;
    cs.irBPath.Set("");
    cs.removeIRB = true;
    std::cerr << "Failed to load morph chain IR: " << e.what() << std::endl;
  }
}

void NeuralAmpModeler::StageUnitBTone(int unit, const char* modelPath, const char* irPath, const char* tonePath)
{
  if (unit < 0 || unit > kNumChainSlots)
    return;
  if (unit == 0)
  {
    _StageModelB(WDL_String(modelPath != nullptr ? modelPath : ""));
    _StageIRB(WDL_String(irPath != nullptr ? irPath : ""));
    mMainToneBPath.Set(tonePath != nullptr ? tonePath : "");
  }
  else
  {
    _StageChainModelB(unit - 1, modelPath);
    _StageChainIRB(unit - 1, irPath);
    mChainSlots[unit - 1].toneBPath.Set(tonePath != nullptr ? tonePath : "");
  }
}

void NeuralAmpModeler::ClearUnitB(int unit)
{
  if (unit < 0 || unit > kNumChainSlots)
    return;
  if (unit == 0)
  {
    mNAMPathB.Set("");
    mIRPathB.Set("");
    mMainToneBPath.Set("");
    mShouldRemoveModelB = true;
    mShouldRemoveIRB = true;
    mChainMainMorph = 0.0;
  }
  else
  {
    ChainSlot& cs = mChainSlots[unit - 1];
    cs.modelBPath.Set("");
    cs.irBPath.Set("");
    cs.toneBPath.Set("");
    cs.removeModelB = true;
    cs.removeIRB = true;
    cs.morph = 0.0;
  }
}

void NeuralAmpModeler::SetUnitMorph(int unit, double amt01)
{
  const double v = std::max(0.0, std::min(1.0, amt01));
  if (unit == 0)
    mChainMainMorph = v;
  else if (unit >= 1 && unit <= kNumChainSlots)
    mChainSlots[unit - 1].morph = v;
}

double NeuralAmpModeler::GetUnitMorph(int unit) const
{
  if (unit == 0)
    return mChainMainMorph.load();
  if (unit >= 1 && unit <= kNumChainSlots)
    return mChainSlots[unit - 1].morph.load();
  return 0.0;
}

bool NeuralAmpModeler::UnitHasB(int unit) const
{
  if (unit == 0)
    return mNAMPathB.GetLength() > 0 || mIRPathB.GetLength() > 0;
  if (unit >= 1 && unit <= kNumChainSlots)
    return mChainSlots[unit - 1].modelBPath.GetLength() > 0 || mChainSlots[unit - 1].irBPath.GetLength() > 0;
  return false;
}

const char* NeuralAmpModeler::GetUnitBTonePath(int unit) const
{
  if (unit == 0)
    return mMainToneBPath.Get();
  if (unit >= 1 && unit <= kNumChainSlots)
    return mChainSlots[unit - 1].toneBPath.Get();
  return "";
}

// --- Rig presets ------------------------------------------------------------

nlohmann::json NeuralAmpModeler::CaptureRigPreset() const
{
  namespace fs = std::filesystem;
  auto relOf = [](const char* abs) -> std::string {
    try
    {
      if (abs == nullptr || abs[0] == '\0')
        return "";
      const fs::path root = tonegallery::GetToneLibraryRoot();
      const fs::path p = tonegallery::UTF8ToPath(abs);
      const fs::path rel = p.lexically_relative(root);
      const std::string r = tonegallery::PathToUTF8(rel);
      if (!r.empty() && r.rfind("..", 0) != 0 && rel.is_relative())
        return r;
    }
    catch (const std::exception&)
    {
    }
    return "";
  };
  auto peekName = [](const char* tone, const char* model, const char* ir) -> std::string {
    try
    {
      if (tone != nullptr && tone[0] != '\0')
        return tonegallery::PathToUTF8(tonegallery::UTF8ToPath(tone).filename());
      const char* f = (model != nullptr && model[0] != '\0') ? model : ir;
      if (f != nullptr && f[0] != '\0')
        return tonegallery::PathToUTF8(tonegallery::UTF8ToPath(f).stem());
    }
    catch (const std::exception&)
    {
    }
    return "Empty";
  };

  nlohmann::json j;
  j["format"] = "namrig-v1";
  nlohmann::json peek = nlohmann::json::array();

  // Unit 1 (the main model + IR)
  {
    nlohmann::json m;
    m["model"] = std::string(mNAMPath.Get());
    m["model_rel"] = relOf(mNAMPath.Get());
    m["ir"] = std::string(mIRPath.Get());
    m["ir_rel"] = relOf(mIRPath.Get());
    m["enabled"] = mChainMainEnabled.load();
    m["level"] = mChainMainLevelDB.load();
    // Tone Morph: the main unit's B tone + morph amount
    m["model_b"] = std::string(mNAMPathB.Get());
    m["model_b_rel"] = relOf(mNAMPathB.Get());
    m["ir_b"] = std::string(mIRPathB.Get());
    m["ir_b_rel"] = relOf(mIRPathB.Get());
    m["tone_b"] = std::string(mMainToneBPath.Get());
    m["tone_b_rel"] = relOf(mMainToneBPath.Get());
    m["morph"] = mChainMainMorph.load();
    j["main"] = m;
    // Display name: the tone folder if it's in the library, else the file.
    std::string disp = "Empty";
    try
    {
      const char* f = mNAMPath.GetLength() ? mNAMPath.Get() : mIRPath.Get();
      if (f != nullptr && f[0] != '\0')
        disp = !relOf(f).empty() ? tonegallery::PathToUTF8(tonegallery::UTF8ToPath(f).parent_path().filename())
                                 : tonegallery::PathToUTF8(tonegallery::UTF8ToPath(f).stem());
    }
    catch (const std::exception&)
    {
    }
    peek.push_back(disp);
  }

  // Global knobs + toggles. If a chain unit is borrowing the knobs right
  // now, the params hold the SLOT's values -- use the main backup instead.
  {
    nlohmann::json p;
    const bool useBackup = mKnobEditActive;
    p["input"] = useBackup ? mMainKnobBackup[0] : GetParam(kInputLevel)->Value();
    p["bass"] = useBackup ? mMainKnobBackup[1] : GetParam(kToneBass)->Value();
    p["middle"] = useBackup ? mMainKnobBackup[2] : GetParam(kToneMid)->Value();
    p["treble"] = useBackup ? mMainKnobBackup[3] : GetParam(kToneTreble)->Value();
    p["output"] = useBackup ? mMainKnobBackup[4] : GetParam(kOutputLevel)->Value();
    p["gate_threshold"] = GetParam(kNoiseGateThreshold)->Value();
    p["gate_on"] = GetParam(kNoiseGateActive)->Bool();
    p["eq_on"] = GetParam(kEQActive)->Bool();
    p["ir_on"] = GetParam(kIRToggle)->Bool();
    p["double_track_on"] = mDoubleTrackActive.load();
    p["double_track_bass_center"] = mDoubleTrackBassCenter.load();
    j["params"] = p;
  }

  // The extra chain slots
  {
    nlohmann::json slots = nlohmann::json::array();
    for (int ci = 0; ci < kNumChainSlots; ci++)
    {
      const ChainSlot& slot = mChainSlots[ci];
      nlohmann::json s;
      s["model"] = std::string(slot.modelPath.Get());
      s["model_rel"] = relOf(slot.modelPath.Get());
      s["ir"] = std::string(slot.irPath.Get());
      s["ir_rel"] = relOf(slot.irPath.Get());
      s["tone"] = std::string(slot.tonePath.Get());
      s["tone_rel"] = relOf(slot.tonePath.Get());
      s["enabled"] = slot.enabled.load();
      s["level"] = slot.levelDB.load();
      s["input"] = slot.inputDB.load();
      s["bass"] = slot.bass.load();
      s["middle"] = slot.middle.load();
      s["treble"] = slot.treble.load();
      // Tone Morph: this slot's B tone + morph amount
      s["model_b"] = std::string(slot.modelBPath.Get());
      s["model_b_rel"] = relOf(slot.modelBPath.Get());
      s["ir_b"] = std::string(slot.irBPath.Get());
      s["ir_b_rel"] = relOf(slot.irBPath.Get());
      s["tone_b"] = std::string(slot.toneBPath.Get());
      s["tone_b_rel"] = relOf(slot.toneBPath.Get());
      s["morph"] = slot.morph.load();
      slots.push_back(s);
      peek.push_back(peekName(slot.tonePath.Get(), slot.modelPath.Get(), slot.irPath.Get()));
    }
    j["slots"] = slots;
  }
  j["peek"] = peek;
  return j;
}

void NeuralAmpModeler::ApplyRigPreset(const nlohmann::json& j)
{
  namespace fs = std::filesystem;
  auto resolve = [](const nlohmann::json& obj, const char* absKey, const char* relKey) -> std::string {
    try
    {
      const std::string rel = obj.value(relKey, "");
      if (!rel.empty())
      {
        const fs::path p = tonegallery::GetToneLibraryRoot() / tonegallery::UTF8ToPath(rel);
        if (fs::exists(p))
          return tonegallery::PathToUTF8(p);
      }
      const std::string abs = obj.value(absKey, "");
      if (!abs.empty() && fs::exists(tonegallery::UTF8ToPath(abs)))
        return abs;
    }
    catch (const std::exception&)
    {
    }
    return "";
  };
  try
  {
    // Unit 1 (main)
    if (j.contains("main"))
    {
      const auto& m = j["main"];
      const std::string model = resolve(m, "model", "model_rel");
      const std::string ir = resolve(m, "ir", "ir_rel");
      if (!model.empty())
        _StageModel(WDL_String(model.c_str()));
      else
      {
        mShouldRemoveModel = true;
        mNAMPath.Set("");
        SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagClearedDisplay);
      }
      if (!ir.empty())
        _StageIR(WDL_String(ir.c_str()));
      else
      {
        mShouldRemoveIR = true;
        mIRPath.Set("");
        SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagClearedDisplay);
      }
      mChainMainEnabled = m.value("enabled", true);
      mChainMainLevelDB = m.value("level", 0.0);
      // Tone Morph: the main unit's B tone + morph
      const std::string modelB = resolve(m, "model_b", "model_b_rel");
      const std::string irB = resolve(m, "ir_b", "ir_b_rel");
      const std::string toneB = resolve(m, "tone_b", "tone_b_rel");
      StageUnitBTone(0, modelB.c_str(), irB.c_str(), toneB.c_str());
      mChainMainMorph = m.value("morph", 0.0);
    }

    // Global knobs + toggles (only valid when no chain unit is borrowing
    // the knobs; presets are loaded from the chain view, where that's true)
    if (j.contains("params") && mChainEditSlot < 1)
    {
      const auto& p = j["params"];
      _SetKnobParamAndNotify(kInputLevel, p.value("input", 0.0));
      _SetKnobParamAndNotify(kNoiseGateThreshold, p.value("gate_threshold", -80.0));
      _SetKnobParamAndNotify(kToneBass, p.value("bass", 5.0));
      _SetKnobParamAndNotify(kToneMid, p.value("middle", 5.0));
      _SetKnobParamAndNotify(kToneTreble, p.value("treble", 5.0));
      _SetKnobParamAndNotify(kOutputLevel, p.value("output", 0.0));
      auto setToggle = [&](int idx, const char* key, bool defv) {
        GetParam(idx)->Set(p.value(key, defv) ? 1.0 : 0.0);
        SendParameterValueFromDelegate(idx, GetParam(idx)->GetNormalized(), true);
        OnParamChange(idx);
        OnParamChangeUI(idx, iplug::EParamSource::kPresetRecall);
      };
      setToggle(kNoiseGateActive, "gate_on", true);
      setToggle(kEQActive, "eq_on", true);
      setToggle(kIRToggle, "ir_on", true);
      // Instant double-track (fork state, not a base param)
      mDoubleTrackActive = p.value("double_track_on", false);
      mDoubleTrackBassCenter = p.value("double_track_bass_center", false);
    }

    // The extra chain slots
    if (j.contains("slots") && j["slots"].is_array())
    {
      for (int ci = 0; ci < kNumChainSlots; ci++)
      {
        if (ci < (int)j["slots"].size())
        {
          const auto& s = j["slots"][ci];
          const std::string model = resolve(s, "model", "model_rel");
          const std::string ir = resolve(s, "ir", "ir_rel");
          const std::string tone = resolve(s, "tone", "tone_rel");
          SetChainTone(ci, model.c_str(), ir.c_str(), tone.c_str());
          ChainSlot& slot = mChainSlots[ci];
          slot.enabled = s.value("enabled", true);
          slot.levelDB = s.value("level", 0.0);
          slot.inputDB = s.value("input", 0.0);
          slot.bass = s.value("bass", 5.0);
          slot.middle = s.value("middle", 5.0);
          slot.treble = s.value("treble", 5.0);
          // Tone Morph: this slot's B tone + morph
          const std::string modelB = resolve(s, "model_b", "model_b_rel");
          const std::string irB = resolve(s, "ir_b", "ir_b_rel");
          const std::string toneB = resolve(s, "tone_b", "tone_b_rel");
          StageUnitBTone(ci + 1, modelB.c_str(), irB.c_str(), toneB.c_str());
          slot.morph = s.value("morph", 0.0);
        }
        else
        {
          ClearChainSlot(ci);
          ClearUnitB(ci + 1);
        }
        if (mChainToneStacks[ci] != nullptr)
        {
          mChainToneStacks[ci]->SetParam("bass", mChainSlots[ci].bass.load());
          mChainToneStacks[ci]->SetParam("middle", mChainSlots[ci].middle.load());
          mChainToneStacks[ci]->SetParam("treble", mChainSlots[ci].treble.load());
        }
      }
    }

    // Refresh the "now playing" displays from the new main tone.
    if (GetUI() != nullptr)
    {
      tonegallery::ToneEntry match; // empty = clears the glow
      try
      {
        if (mNAMPath.GetLength())
        {
          const auto modelDir = std::filesystem::u8path(mNAMPath.Get()).parent_path();
          for (const auto& entry : tonegallery::ScanToneLibrary(tonegallery::GetToneLibraryRoot()))
          {
            if (tonegallery::UTF8ToPath(entry.directory) == modelDir)
            {
              match = entry;
              break;
            }
          }
        }
      }
      catch (const std::exception&)
      {
      }
      tonegallery::NotifyNowPlaying(GetUI(), match, mNAMPath.Get(), mIRPath.Get(), /* force */ true);
    }
  }
  catch (const std::exception&)
  {
  }
}

void NeuralAmpModeler::BeginChainKnobEdit(int unit)
{
  // Switching straight from one edit to another? Close out the old one so
  // the backup/restore pairing stays clean.
  if (mKnobEditActive && mChainEditSlot >= 1)
    EndChainKnobEdit();
  mChainEditSlot = unit;
  if (unit < 1 || unit > kNumChainSlots)
    return; // unit 1 (the main tone) uses the knobs as normal

  static const int kKnobs[5] = {kInputLevel, kToneBass, kToneMid, kToneTreble, kOutputLevel};
  if (!mKnobEditActive)
  {
    for (int i = 0; i < 5; i++)
      mMainKnobBackup[i] = GetParam(kKnobs[i])->Value();
    mKnobEditActive = true;
  }
  ChainSlot& slot = mChainSlots[unit - 1];
  const double values[5] = {
    slot.inputDB.load(), slot.bass.load(), slot.middle.load(), slot.treble.load(), slot.levelDB.load()};
  for (int i = 0; i < 5; i++)
    _SetKnobParamAndNotify(kKnobs[i], values[i]);
  // The file browsers show this unit's files while it's being edited.
  _UpdateBrowsersForEditSlot();
}

void NeuralAmpModeler::EndChainKnobEdit()
{
  mChainEditSlot = -1;
  mChainEditTargetB = false;
  if (!mKnobEditActive)
    return;
  mKnobEditActive = false;
  static const int kKnobs[5] = {kInputLevel, kToneBass, kToneMid, kToneTreble, kOutputLevel};
  for (int i = 0; i < 5; i++)
    _SetKnobParamAndNotify(kKnobs[i], mMainKnobBackup[i]);
  // Hand the file browsers back to the main tone.
  _UpdateBrowsersForEditSlot();
}

void NeuralAmpModeler::_SetKnobParamAndNotify(int paramIdx, double value)
{
  GetParam(paramIdx)->Set(value);
  // Update the on-screen knob...
  SendParameterValueFromDelegate(paramIdx, GetParam(paramIdx)->GetNormalized(), true);
  // ...and apply the change to the DSP.
  OnParamChange(paramIdx);
}

void NeuralAmpModeler::_UpdateBrowsersForEditSlot()
{
  auto send = [&](int ctrlTag, int loadedMsg, const WDL_String& path) {
    if (path.GetLength())
      SendControlMsgFromDelegate(ctrlTag, loadedMsg, path.GetLength(), path.Get());
    else
      SendControlMsgFromDelegate(ctrlTag, kMsgTagClearedDisplay);
  };
  if (mChainEditTargetB)
  {
    // Editing a unit's B (morph) side: show the B files.
    if (mChainEditSlot >= 1 && mChainEditSlot <= kNumChainSlots)
    {
      const ChainSlot& slot = mChainSlots[mChainEditSlot - 1];
      send(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, slot.modelBPath);
      send(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, slot.irBPath);
    }
    else
    {
      send(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPathB);
      send(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPathB);
    }
  }
  else if (mChainEditSlot >= 1 && mChainEditSlot <= kNumChainSlots)
  {
    const ChainSlot& slot = mChainSlots[mChainEditSlot - 1];
    send(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, slot.modelPath);
    send(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, slot.irPath);
  }
  else
  {
    send(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath);
    send(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath);
  }
}

size_t NeuralAmpModeler::_GetBufferNumChannels() const
{
  // Assumes input=output (no mono->stereo effects)
  return mInputArray.size();
}

size_t NeuralAmpModeler::_GetBufferNumFrames() const
{
  if (_GetBufferNumChannels() == 0)
    return 0;
  return mInputArray[0].size();
}

void NeuralAmpModeler::_InitToneStack()
{
  // If you want to customize the tone stack, then put it here!
  mToneStack = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
}
void NeuralAmpModeler::_PrepareBuffers(const size_t numChannels, const size_t numFrames)
{
  const bool updateChannels = numChannels != _GetBufferNumChannels();
  const bool updateFrames = updateChannels || (_GetBufferNumFrames() != numFrames);
  // if (!updateChannels && !updateFrames) // Could we do this?
  // return;

  if (updateChannels)
  {
    _PrepareIOPointers(numChannels);
    mInputArray.resize(numChannels);
    mOutputArray.resize(numChannels);
  }
  if (updateFrames)
  {
    for (auto c = 0; c < mInputArray.size(); c++)
    {
      mInputArray[c].resize(numFrames);
      std::fill(mInputArray[c].begin(), mInputArray[c].end(), 0.0);
    }
    for (auto c = 0; c < mOutputArray.size(); c++)
    {
      mOutputArray[c].resize(numFrames);
      std::fill(mOutputArray[c].begin(), mOutputArray[c].end(), 0.0);
    }
  }
  // Tone Morph: keep the parallel B-branch scratch buffers sized to the block.
  if (mMorphArrayA.size() != numFrames)
    mMorphArrayA.assign(numFrames, 0.0);
  if (mMorphArrayB.size() != numFrames)
    mMorphArrayB.assign(numFrames, 0.0);
  for (int ci = 0; ci < kNumChainSlots; ci++)
  {
    if (mChainArraysB[ci].size() != numFrames)
      mChainArraysB[ci].assign(numFrames, 0.0);
  }
  // Would these ever get changed by something?
  for (auto c = 0; c < mInputArray.size(); c++)
    mInputPointers[c] = mInputArray[c].data();
  for (auto c = 0; c < mOutputArray.size(); c++)
    mOutputPointers[c] = mOutputArray[c].data();
}

void NeuralAmpModeler::_PrepareIOPointers(const size_t numChannels)
{
  _DeallocateIOPointers();
  _AllocateIOPointers(numChannels);
}

void NeuralAmpModeler::_ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn,
                                     const size_t nChansOut)
{
  // We'll assume that the main processing is mono for now. We'll handle dual amps later.
  if (nChansOut != 1)
  {
    std::stringstream ss;
    ss << "Expected mono output, but " << nChansOut << " output channels are requested!";
    throw std::runtime_error(ss.str());
  }

  // On the standalone, we can probably assume that the user has plugged into only one input and they expect it to be
  // carried straight through. Don't apply any division over nChansIn because we're just "catching anything out there."
  // However, in a DAW, it's probably something providing stereo, and we want to take the average in order to avoid
  // doubling the loudness. (This would change w/ double mono processing)
  double gain = mInputGain;
#ifndef APP_API
  gain /= (float)nChansIn;
#endif
  // Assume _PrepareBuffers() was already called
  for (size_t c = 0; c < nChansIn; c++)
    for (size_t s = 0; s < nFrames; s++)
      if (c == 0)
        mInputArray[0][s] = gain * inputs[c][s];
      else
        mInputArray[0][s] += gain * inputs[c][s];
}

void NeuralAmpModeler::_ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames,
                                      const size_t nChansIn, const size_t nChansOut)
{
  const double gain = mOutputGain;
  // Assume _PrepareBuffers() was already called
  if (nChansIn != 1)
    throw std::runtime_error("Plugin is supposed to process in mono.");
  // Broadcast the internal mono stream to all output channels.
  const size_t cin = 0;
  for (auto cout = 0; cout < nChansOut; cout++)
    for (auto s = 0; s < nFrames; s++)
#ifdef APP_API // Ensure valid output to interface
      outputs[cout][s] = std::clamp(gain * inputs[cin][s], -1.0, 1.0);
#else // In a DAW, other things may come next and should be able to handle large
      // values.
      outputs[cout][s] = gain * inputs[cin][s];
#endif
}

// Fractional read from the double-track history buffer, `delaySamples` behind
// the write head, with linear interpolation and wrap-around.
iplug::sample NeuralAmpModeler::_DTReadTap(int writePos, double delaySamples, int len) const
{
  double rp = (double)writePos - delaySamples;
  while (rp < 0.0)
    rp += (double)len;
  while (rp >= (double)len)
    rp -= (double)len;
  const int i0 = (int)rp;
  const int i1 = (i0 + 1) % len;
  const double frac = rp - (double)i0;
  return mDTBuf[i0] * (1.0 - frac) + mDTBuf[i1] * frac;
}

// Instant double-track. One mono history buffer read by two slowly-modulated
// taps (different base delays + LFO rates) makes two decorrelated, micro-detuned
// "takes" that pan hard left/right. With bass-center on, the lows stay mono and
// only the highs are spread, so the low end stays tight.
void NeuralAmpModeler::_RenderDoubleTrack(iplug::sample* mono, const size_t nFrames, const double sampleRate,
                                          iplug::sample* outL, iplug::sample* outR)
{
  const int len = (int)mDTBuf.size();
  if (len < 8 || sampleRate <= 0.0)
  {
    for (size_t s = 0; s < nFrames; s++)
    {
      outL[s] = mono[s];
      outR[s] = mono[s];
    }
    return;
  }
  const bool bassCenter = mDoubleTrackBassCenter.load();
  const double twoPi = 6.283185307179586;
  const double baseL = 0.011 * sampleRate; // 11 ms
  const double baseR = 0.018 * sampleRate; // 18 ms
  const double depth = 0.0025 * sampleRate; // +/- 2.5 ms slow drift -> micro-detune
  const double incL = twoPi * 0.13 / sampleRate;
  const double incR = twoPi * 0.19 / sampleRate;
  const double lpCoef = std::exp(-twoPi * 160.0 / sampleRate); // ~160 Hz low/high split
  // Rising edge: clear stale history so switching on doesn't burst.
  if (!mDTActivePrev)
  {
    std::fill(mDTBuf.begin(), mDTBuf.end(), 0.0);
    mDTWritePos = 0;
    mDTPhaseL = 0.0;
    mDTPhaseR = 1.7;
    mDTLowState = 0.0;
  }
  mDTActivePrev = true;
  for (size_t s = 0; s < nFrames; s++)
  {
    const double x = mono[s];
    double low = 0.0, fed = x;
    if (bassCenter)
    {
      mDTLowState = lpCoef * mDTLowState + (1.0 - lpCoef) * x;
      low = mDTLowState;
      fed = x - low; // only the highs get spread
    }
    mDTBuf[mDTWritePos] = fed;
    const double dL = baseL + depth * std::sin(mDTPhaseL);
    const double dR = baseR + depth * std::sin(mDTPhaseR);
    const double vL = _DTReadTap(mDTWritePos, dL, len);
    const double vR = _DTReadTap(mDTWritePos, dR, len);
    outL[s] = bassCenter ? (low + vL) : vL;
    outR[s] = bassCenter ? (low + vR) : vR;
    mDTPhaseL += incL;
    if (mDTPhaseL > twoPi)
      mDTPhaseL -= twoPi;
    mDTPhaseR += incR;
    if (mDTPhaseR > twoPi)
      mDTPhaseR -= twoPi;
    mDTWritePos = (mDTWritePos + 1) % len;
  }
}

// Write the two double-track voices out (even channels = left, odd = right),
// applying output gain and the same clamp as the mono path.
void NeuralAmpModeler::_ProcessOutputStereo(iplug::sample* left, iplug::sample* right, iplug::sample** outputs,
                                            const size_t nFrames, const size_t nChansOut)
{
  const double gain = mOutputGain;
  for (size_t c = 0; c < nChansOut; c++)
  {
    const iplug::sample* src = (c % 2 == 0) ? left : right;
    for (size_t s = 0; s < nFrames; s++)
#ifdef APP_API // Ensure valid output to interface
      outputs[c][s] = std::clamp(gain * src[s], -1.0, 1.0);
#else // In a DAW, downstream can handle large values.
      outputs[c][s] = gain * src[s];
#endif
  }
}

void NeuralAmpModeler::_UpdateControlsFromModel()
{
  if (mModel == nullptr)
  {
    return;
  }
  if (auto* pGraphics = GetUI())
  {
    ModelInfo modelInfo;
    modelInfo.sampleRate.known = true;
    modelInfo.sampleRate.value = mModel->GetEncapsulatedSampleRate();
    modelInfo.inputCalibrationLevel.known = mModel->HasInputLevel();
    modelInfo.inputCalibrationLevel.value = mModel->HasInputLevel() ? mModel->GetInputLevel() : 0.0;
    modelInfo.outputCalibrationLevel.known = mModel->HasOutputLevel();
    modelInfo.outputCalibrationLevel.value = mModel->HasOutputLevel() ? mModel->GetOutputLevel() : 0.0;

    static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->SetModelInfo(modelInfo);

    const bool disableInputCalibrationControls = !mModel->HasInputLevel();
    pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetDisabled(disableInputCalibrationControls);
    pGraphics->GetControlWithTag(kCtrlTagInputCalibrationLevel)->SetDisabled(disableInputCalibrationControls);
    {
      auto* c = static_cast<OutputModeControl*>(pGraphics->GetControlWithTag(kCtrlTagOutputMode));
      c->SetNormalizedDisable(!mModel->HasLoudness());
      c->SetCalibratedDisable(!mModel->HasOutputLevel());
    }

    if (auto* pSlimIcon = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
    {
      const bool show = mModel->GetSlimmableModel() != nullptr;
      pSlimIcon->Hide(!show);
    }
  }
}

void NeuralAmpModeler::_UpdateLatency()
{
  int latency = 0;
  if (mModel)
  {
    latency += mModel->GetLatency();
  }
  // Other things that add latency here...

  // Feels weird to have to do this.
  if (GetLatency() != latency)
  {
    SetLatency(latency);
  }
}

void NeuralAmpModeler::_UpdateMeters(sample** inputPointer, sample** outputPointer, const size_t nFrames,
                                     const size_t nChansIn, const size_t nChansOut)
{
  // Right now, we didn't specify MAXNC when we initialized these, so it's 1.
  const int nChansHack = 1;
  mInputSender.ProcessBlock(inputPointer, (int)nFrames, kCtrlTagInputMeter, nChansHack);
  mOutputSender.ProcessBlock(outputPointer, (int)nFrames, kCtrlTagOutputMeter, nChansHack);
}

// HACK
#include "Unserialization.cpp"
