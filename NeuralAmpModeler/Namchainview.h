#pragma once

// Tone Gallery fork: the stacked signal-chain view.
//
// Shows the plugin's 4 chain units as rack gear stacked in one window --
// unit 1 is the plugin's main model+IR, units 2-4 are the extra ChainSlots
// on the plugin class, processed in series (top to bottom = signal flow).
// Each unit: bypass LED, tone photo "LCD", name/gear info, level knob, and
// a click-to-choose tone picker fed by the local tone library.
//
// Include from NeuralAmpModeler.cpp AFTER NAMTheme.h (needs namtheme, the
// PLUG() macro from NeuralAmpModelerControls.h, and the tonegallery
// helpers/kCtrlTagChainView from NAMToneGalleryControl.h).

constexpr float kChainHeaderHeight = 34.0f;
constexpr float kChainUnitHeight = 106.0f;
constexpr float kChainViewHeight = kChainHeaderHeight + 4.0f * kChainUnitHeight + 6.0f; // 464

// ---------------------------------------------------------------------------
// Main-view Tone Morph controls (a MORPH knob in the top knob row + a pair of
// A TONE / B TONE cards). They act on the "focused" unit: the slot currently
// being edited, or the main tone otherwise. Clicking a card selects which side
// (A or B) the tone library loads into; the knob blends between them.
// ---------------------------------------------------------------------------

// The MORPH knob for the top knob row (drawn to match ThemedKnobControl).
class NAMMorphKnobControl : public IControl
{
public:
  NAMMorphKnobControl(const IRECT& bounds)
  : IControl(bounds)
  {
    SetTooltip("MORPH: blend from the A tone to the B tone");
  }

  int FocusUnit() { return PLUG()->mChainEditSlot >= 1 ? PLUG()->mChainEditSlot : 0; }

  void Draw(IGraphics& g) override
  {
    const IColor accent = namtheme::Accent();
    const int unit = FocusUnit();
    const bool hasB = PLUG()->UnitHasB(unit);
    const double morph = PLUG()->GetUnitMorph(unit);

    const IRECT knobBox = mRECT.GetReducedFromBottom(20.0f);
    const float d = std::min(knobBox.W(), knobBox.H());
    const IRECT sq = knobBox.GetCentredInside(d);
    const float cx = sq.MW(), cy = sq.MH();
    const float radius = 0.5f * d * 0.80f;
    const float a1 = -135.0f, a2 = 135.0f;
    const float angle = a1 + (float)morph * (a2 - a1);
    const IColor arcCol = hasB ? accent : namtheme::TEXT_FAINT;

    g.DrawArc(namtheme::TRACK, cx, cy, radius, a1, a2, nullptr, 3.5f);
    if (hasB && angle > a1 + 0.5f)
      g.DrawArc(arcCol, cx, cy, radius, a1, angle, nullptr, 3.5f);

    const float capR = radius - 7.0f;
    const IRECT capRect(cx - capR, cy - capR, cx + capR, cy + capR);
    g.FillEllipse(namtheme::KNOB_FACE, capRect);
    g.DrawEllipse(namtheme::LINE, capRect);
    if (mHover && hasB)
      g.FillEllipse(PluginColors::MOUSEOVER, capRect);

    if (hasB)
    {
      const float rad = (angle - 90.0f) * 3.14159265f / 180.0f;
      const float px = cx + (capR * 0.62f) * std::cos(rad);
      const float py = cy + (capR * 0.62f) * std::sin(rad);
      g.PathCircle(px, py, 5.5f);
      g.PathFill(
        IPattern::CreateRadialGradient(px, py, 7.0f, {{accent.WithOpacity(0.5f), 0.0f}, {COLOR_TRANSPARENT, 1.0f}}));
      g.FillCircle(accent, px, py, 2.5f);
    }
    else
    {
      const IText plus(15.0f, accent.WithOpacity(0.85f), namtheme::kFontBold, EAlign::Center, EVAlign::Middle);
      g.DrawText(plus, "+", capRect);
    }

    const IText label(12.0f, hasB ? accent : namtheme::TEXT_DIM, namtheme::kFontBold, EAlign::Center, EVAlign::Middle);
    g.DrawText(label, "MORPH", mRECT.GetFromBottom(18.0f).GetFromTop(11.0f));
    char sub[24];
    if (hasB)
      snprintf(sub, sizeof(sub), "%.0f%%", morph * 100.0);
    else
      snprintf(sub, sizeof(sub), "ADD B");
    const IText subText(8.0f, namtheme::TEXT_FAINT, namtheme::kFontBody, EAlign::Center, EVAlign::Middle);
    g.DrawText(subText, sub, mRECT.GetFromBottom(9.0f));
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (!PLUG()->UnitHasB(FocusUnit()))
    {
      // No B yet: arm the B side so the next library pick loads into B.
      PLUG()->SetChainEditTargetB(true);
      GetUI()->SetAllControlsDirty();
      return;
    }
    mDragging = true;
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (!mDragging)
      return;
    const int unit = FocusUnit();
    const double next = std::max(0.0, std::min(1.0, PLUG()->GetUnitMorph(unit) - (double)dY * 0.005));
    PLUG()->SetUnitMorph(unit, next);
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override { mDragging = false; }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    PLUG()->SetUnitMorph(FocusUnit(), 0.0);
    SetDirty(false);
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    if (!mHover)
    {
      mHover = true;
      SetDirty(false);
    }
  }
  void OnMouseOut() override
  {
    if (mHover)
    {
      mHover = false;
      SetDirty(false);
    }
  }

private:
  bool mDragging = false;
  bool mHover = false;
};

// A compact slide-switch toggle for the main view that flips a plugin flag:
// the Instant Double-Track and its Bass-Center option. Styled to match
// ThemedSwitchControl (pill + handle + small label). Reads/writes plugin state
// directly (these are fork atomics, not automatable params).
class NAMDoubleTrackSwitch : public IControl
{
public:
  enum EWhich
  {
    kToggleDoubleTrack,
    kToggleBassCenter
  };

  NAMDoubleTrackSwitch(const IRECT& bounds, EWhich which, const char* label)
  : IControl(bounds)
  , mWhich(which)
  , mLabel(label)
  {
    SetTooltip(which == kToggleDoubleTrack ? "DOUBLE TRACK: spread the final tone into two hard-panned takes (L/R)"
                                           : "BASS CENTER: keep the low end mono/centered while the highs spread");
  }

  bool On() const
  {
    return mWhich == kToggleDoubleTrack ? PLUG()->IsDoubleTrackActive() : PLUG()->IsDoubleTrackBassCenter();
  }
  bool Enabled() const { return mWhich == kToggleDoubleTrack ? true : PLUG()->IsDoubleTrackActive(); }

  void Draw(IGraphics& g) override
  {
    const IColor accent = namtheme::Accent();
    const bool on = On();
    const bool enabled = Enabled();
    const IRECT pill = mRECT.GetFromTop(17.0f).GetCentredInside(34.0f, 16.0f);
    const IColor offCol = IColor(26, 255, 255, 255);
    g.FillRoundRect(on && enabled ? accent : offCol, pill, pill.H() * 0.5f);
    if (mHover && enabled)
      g.FillRoundRect(PluginColors::MOUSEOVER, pill, pill.H() * 0.5f);
    const float hr = 5.5f;
    const float hx = on ? pill.R - 2.5f - hr : pill.L + 2.5f + hr;
    g.FillCircle(on && enabled ? COLOR_WHITE : IColor(255, 130, 130, 138), hx, pill.MH(), hr);
    const IColor txt = enabled ? (on ? accent : namtheme::TEXT_DIM) : namtheme::TEXT_FAINT;
    const IText label(8.0f, txt, namtheme::kFontBold, EAlign::Center, EVAlign::Middle);
    g.DrawText(label, mLabel.c_str(), IRECT(mRECT.L, pill.B + 1.0f, mRECT.R, mRECT.B));
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (mWhich == kToggleDoubleTrack)
      PLUG()->ToggleDoubleTrack();
    else
    {
      if (!PLUG()->IsDoubleTrackActive())
        return; // bass-center only matters while double-track is on
      PLUG()->ToggleDoubleTrackBassCenter();
    }
    if (GetUI())
      GetUI()->SetAllControlsDirty();
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    if (!mHover)
    {
      mHover = true;
      SetDirty(false);
    }
  }
  void OnMouseOut() override
  {
    if (mHover)
    {
      mHover = false;
      SetDirty(false);
    }
  }

private:
  EWhich mWhich;
  std::string mLabel;
  bool mHover = false;
};

// The A TONE / B TONE card pair for the main view.
class NAMMorphCardsControl : public IControl, public tonegallery::INowPlayingListener
{
public:
  NAMMorphCardsControl(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  int FocusUnit() { return PLUG()->mChainEditSlot >= 1 ? PLUG()->mChainEditSlot : 0; }

  void SetNowPlaying(const tonegallery::ToneEntry& entry, const std::string& modelPath,
                     const std::string& irPath) override
  {
    mMainEntry = entry;
    mHasMainEntry = true;
    SetDirty(false);
  }

  IRECT ACardRect() const { return mRECT.GetGridCell(0, 0, 1, 2).GetPadded(-3.0f); }
  IRECT BCardRect() const { return mRECT.GetGridCell(0, 1, 1, 2).GetPadded(-3.0f); }

  void Draw(IGraphics& g) override
  {
    const IColor accent = namtheme::Accent();
    const bool bActive = PLUG()->mChainEditTargetB;
    DrawCard(g, ACardRect(), false, !bActive, accent);
    DrawCard(g, BCardRect(), true, bActive, accent);
  }

  void DrawCard(IGraphics& g, const IRECT& card, bool isB, bool active, const IColor& accent)
  {
    const int unit = FocusUnit();
    const tonegallery::ToneEntry* pEntry = isB ? ResolveB(unit) : ResolveA(unit);
    const bool hasTone = pEntry != nullptr;

    g.FillRoundRect(namtheme::CARD, card, 8.0f);
    if (active)
      g.DrawRoundRect(accent, card, 8.0f, nullptr, 1.5f);
    else
      g.DrawRoundRect(namtheme::LINE, card, 8.0f);

    const IRECT labelRow = card.GetFromTop(16.0f).GetReducedFromLeft(8.0f).GetReducedFromTop(3.0f);
    const IText labelText(
      8.0f, active ? accent : namtheme::TEXT_DIM, namtheme::kFontBold, EAlign::Near, EVAlign::Middle);
    g.DrawText(labelText, isB ? "B TONE" : "A TONE", labelRow);

    if (isB && hasTone)
    {
      const IRECT xr = card.GetFromTRHC(18.0f, 18.0f).GetCentredInside(8.0f);
      const IColor xc = namtheme::TEXT_FAINT;
      g.DrawLine(xc, xr.L, xr.T, xr.R, xr.B, nullptr, 1.5f);
      g.DrawLine(xc, xr.L, xr.B, xr.R, xr.T, nullptr, 1.5f);
    }

    const IRECT bodyR = card.GetReducedFromTop(18.0f).GetPadded(-6.0f);
    const IRECT photo = bodyR.GetFromLeft(bodyR.H());
    if (hasTone)
    {
      IBitmap* pBitmap = GetImage(pEntry->imagePath);
      const IRECT photoVis = photo.Intersect(mRECT);
      if (pBitmap != nullptr && pBitmap->W() > 0 && pBitmap->H() > 0 && photoVis.W() > 0.5f && photoVis.H() > 0.5f)
      {
        const float bmpAspect = (float)pBitmap->W() / (float)pBitmap->H();
        const float areaAspect = photo.W() / photo.H();
        IRECT cover = photo;
        if (bmpAspect > areaAspect)
          cover = photo.GetMidHPadded(0.5f * photo.H() * bmpAspect);
        else
          cover = photo.GetMidVPadded(0.5f * photo.W() / bmpAspect);
        g.PathClipRegion(photo);
        g.DrawFittedBitmap(*pBitmap, cover);
        g.PathClipRegion();
      }
      else
      {
        const IColor gc = tonegallery::GearTypeColor(pEntry->gearType);
        g.FillRoundRect(gc.WithOpacity(0.15f), photo, 6.0f);
        const IText it(14.0f, gc, namtheme::kFontBold, EAlign::Center, EVAlign::Middle);
        std::string ini;
        ini += pEntry->name.empty() ? '?' : (char)std::toupper((unsigned char)pEntry->name[0]);
        g.DrawText(it, ini.c_str(), photo);
      }
      const IRECT txt = bodyR.GetReducedFromLeft(photo.W() + 7.0f);
      const IText nameText(9.5f, namtheme::TEXT_MAIN, namtheme::kFontBold, EAlign::Near, EVAlign::Top);
      float ty = txt.T;
      for (const auto& line : tonegallery::WrapLines(pEntry->name, 16, 2))
      {
        g.DrawText(nameText, line.c_str(), IRECT(txt.L, ty, txt.R, ty + 12.0f));
        ty += 12.0f;
      }
      const char* gearLabel = tonegallery::GearTypeChipLabel(pEntry->gearType);
      std::string sub = gearLabel;
      if (!pEntry->author.empty())
        sub += std::string("  by ") + pEntry->author;
      const IText subText(7.5f, namtheme::TEXT_DIM, namtheme::kFontBody, EAlign::Near, EVAlign::Middle);
      g.DrawText(subText, tonegallery::Ellipsize(sub, 28).c_str(), IRECT(txt.L, txt.B - 12.0f, txt.R, txt.B));
    }
    else
    {
      const IColor promptCol = active ? accent : namtheme::TEXT_FAINT;
      const IText t(9.5f, promptCol, namtheme::kFontBold, EAlign::Center, EVAlign::Middle);
      if (active)
        g.DrawText(t, "Pick a tone from the library", bodyR);
      else
        g.DrawText(t, isB ? "+ LOAD B" : "No tone", bodyR);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (BCardRect().Contains(x, y))
    {
      const int unit = FocusUnit();
      if (PLUG()->UnitHasB(unit) && BCardRect().GetFromTRHC(18.0f, 18.0f).Contains(x, y))
      {
        PLUG()->ClearUnitB(unit);
        GetUI()->SetAllControlsDirty();
        return;
      }
      PLUG()->SetChainEditTargetB(true);
      GetUI()->SetAllControlsDirty();
      return;
    }
    if (ACardRect().Contains(x, y))
    {
      PLUG()->SetChainEditTargetB(false);
      GetUI()->SetAllControlsDirty();
      return;
    }
  }

  const tonegallery::ToneEntry* ResolveA(int unit)
  {
    if (unit == 0)
      return mHasMainEntry ? &mMainEntry : nullptr;
    return ResolvePath(0, PLUG()->mChainSlots[unit - 1].tonePath.Get());
  }
  const tonegallery::ToneEntry* ResolveB(int unit) { return ResolvePath(1, PLUG()->GetUnitBTonePath(unit)); }

  const tonegallery::ToneEntry* ResolvePath(int slot, const char* path)
  {
    const std::string key = (path != nullptr) ? path : "";
    if (key != mCacheKey[slot])
    {
      mCacheKey[slot] = key;
      mCacheValid[slot] = false;
      if (!key.empty())
      {
        try
        {
          tonegallery::ToneEntry e;
          if (tonegallery::ScanToneFolder(tonegallery::UTF8ToPath(key), e))
          {
            mCache[slot] = e;
            mCacheValid[slot] = true;
          }
        }
        catch (const std::exception&)
        {
        }
      }
    }
    return mCacheValid[slot] ? &mCache[slot] : nullptr;
  }

  IBitmap* GetImage(const std::string& path)
  {
    if (path.empty())
      return nullptr;
    auto f = mImageCache.find(path);
    if (f != mImageCache.end())
      return f->second.GetAPIBitmap() ? &f->second : nullptr;
    IBitmap bmp;
    try
    {
      if (std::filesystem::exists(tonegallery::UTF8ToPath(path)))
        bmp = GetUI()->LoadBitmap(path.c_str());
    }
    catch (const std::exception&)
    {
    }
    auto ins = mImageCache.insert({path, bmp});
    return ins.first->second.GetAPIBitmap() ? &ins.first->second : nullptr;
  }

private:
  tonegallery::ToneEntry mMainEntry;
  bool mHasMainEntry = false;
  tonegallery::ToneEntry mCache[2];
  std::string mCacheKey[2];
  bool mCacheValid[2] = {false, false};
  std::map<std::string, IBitmap> mImageCache;
};

class NAMChainViewControl : public IControl, public tonegallery::INowPlayingListener
{
public:
  static constexpr int kNumUnits = 4; // unit 0 = main model, 1..3 = ChainSlots

  NAMChainViewControl(const IRECT& bounds, IFileDialogCompletionHandlerFunc loadModelFunc,
                      IFileDialogCompletionHandlerFunc loadIRFunc)
  : IControl(bounds)
  , mLoadModelFunc(loadModelFunc)
  , mLoadIRFunc(loadIRFunc)
  {
  }

  void SetNowPlaying(const tonegallery::ToneEntry& entry, const std::string& modelPath,
                     const std::string& irPath) override
  {
    mMainEntry = entry;
    mHasMainEntry = true;
    if (!modelPath.empty())
      mMainModelPath = modelPath;
    if (!irPath.empty())
      mMainIRPath = irPath;
    if (modelPath.empty() && irPath.empty())
    {
      mMainModelPath = entry.modelPath;
      mMainIRPath = entry.irPath;
    }
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    const IColor accent = namtheme::Accent();
    g.FillRect(namtheme::BG, mRECT);

    // Header: title + the rig preset bar
    const IRECT header = mRECT.GetFromTop(kChainHeaderHeight);
    const IText titleText(13.0f, namtheme::TEXT_MAIN, "Inter-Bold", EAlign::Near, EVAlign::Middle);
    g.DrawText(titleText, "SIGNAL CHAIN", header.GetReducedFromLeft(16.0f));
    DrawPresetBar(g, accent);

    // Exit (expand) button
    const IRECT expand = ExpandRect();
    if (mMouseOverExpand)
      g.FillRoundRect(PluginColors::MOUSEOVER, expand, 5.0f);
    g.DrawRoundRect(namtheme::TEXT_DIM, expand.GetCentredInside(13.0f), 2.0f, nullptr, 1.4f);
    const IRECT xInner = expand.GetCentredInside(13.0f);
    g.DrawLine(namtheme::TEXT_DIM, xInner.L + 3.0f, xInner.MH(), xInner.R - 3.0f, xInner.MH(), nullptr, 1.2f);

    // Units
    for (int i = 0; i < kNumUnits; i++)
      DrawUnit(g, i, accent);

    // Anything below the last unit (host refused to resize): blank it.
    const float used = kChainHeaderHeight + kNumUnits * kChainUnitHeight;
    if (mRECT.H() > used + 8.0f)
      g.FillRect(namtheme::BG, mRECT.GetReducedFromTop(used));
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (ExpandRect().Contains(x, y))
    {
      PLUG()->mToneChainMode = false;
      PLUG()->EndChainKnobEdit();
      Hide(true);
      GetUI()->Resize(PLUG_WIDTH, PLUG_HEIGHT, GetUI()->GetDrawScale());
      return;
    }
    // --- Preset bar ---
    if (PrevRect().Contains(x, y))
    {
      StepPreset(-1);
      return;
    }
    if (NextRect().Contains(x, y))
    {
      StepPreset(+1);
      return;
    }
    if (PresetNameRect().Contains(x, y))
    {
      OpenPresetMenu();
      return;
    }
    if (SaveRect().Contains(x, y))
    {
      SavePreset();
      return;
    }
    if (SaveAsRect().Contains(x, y))
    {
      BeginTextEntry(ERigEntry::SaveAs);
      return;
    }
    if (ShareRect().Contains(x, y))
    {
      ExportCurrentRig();
      return;
    }
    if (SearchRect().Contains(x, y))
    {
      BeginTextEntry(ERigEntry::Search);
      return;
    }
    for (int i = 0; i < kNumUnits; i++)
    {
      if (LEDRect(i).Contains(x, y))
      {
        SetUnitEnabled(i, !GetUnitEnabled(i));
        SetDirty(false);
        return;
      }
      if (KnobRect(i).Contains(x, y))
      {
        mDragUnit = i;
        return;
      }
      if (MorphKnobRect(i).Contains(x, y))
      {
        if (!PLUG()->UnitHasB(i))
        {
          // No B tone yet: jump to the editor to choose one for the B side.
          EnterEditMode(i, /* targetB */ true);
          return;
        }
        if (mod.R)
        {
          // Right-click clears the B tone (and the morph).
          PLUG()->ClearUnitB(i);
          SetDirty(false);
          return;
        }
        mDragMorphUnit = i;
        return;
      }
      if (i > 0 && ClearRect(i).Contains(x, y) && UnitHasTone(i))
      {
        PLUG()->ClearChainSlot(i - 1);
        mSlotCacheKey[i - 1].clear();
        mSlotCacheValid[i - 1] = false;
        SetDirty(false);
        return;
      }
      if (ChooseRect(i).Contains(x, y))
      {
        EnterEditMode(i);
        return;
      }
    }
  }

  // Jump to the full main view to choose/edit this unit's tone. The load
  // handlers on the plugin route into the unit while mChainEditSlot is set,
  // and the banner strip brings the user back here when they're done.
  void EnterEditMode(int unit, bool targetB = false)
  {
    PLUG()->BeginChainKnobEdit(unit); // also points the main knobs at the unit
    PLUG()->SetChainEditTargetB(targetB); // route tone loads to A or B (+ refresh browsers)
    PLUG()->mToneChainMode = false;
    Hide(true);
    GetUI()->Resize(PLUG_WIDTH, PLUG_HEIGHT, GetUI()->GetDrawScale());
    GetUI()->SetAllControlsDirty(); // the SIGNAL CHAIN button becomes BACK TO RACK
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (pSelectedMenu == nullptr || pSelectedMenu->GetChosenItem() == nullptr)
      return;
    const int tag = pSelectedMenu->GetChosenItem()->GetTag();
    if (tag >= 0 && tag < (int)mRigMenuPaths.size())
      LoadPresetFile(mRigMenuPaths[tag]);
  }

  void OnTextEntryCompletion(const char* str, int valIdx) override
  {
    const ERigEntry mode = mRigEntryMode;
    mRigEntryMode = ERigEntry::None;
    if (str == nullptr || str[0] == '\0')
      return;
    if (mode == ERigEntry::SaveAs)
    {
      const std::filesystem::path file = namrig::ResolveSavePath(str);
      if (file.empty())
      {
        Flash("Couldn't use that name");
        return;
      }
      try
      {
        std::ofstream f(file);
        f << PLUG()->CaptureRigPreset().dump(2);
        PLUG()->mRigPresetRel.Set(namrig::RelOfPreset(file).c_str());
        Flash("Saved");
      }
      catch (const std::exception&)
      {
        Flash("Couldn't save the preset");
      }
      SetDirty(false);
    }
    else if (mode == ERigEntry::Search)
    {
      OpenSearchMenu(str);
    }
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (mDragMorphUnit >= 0)
    {
      const double next = std::max(0.0, std::min(1.0, PLUG()->GetUnitMorph(mDragMorphUnit) - (double)dY * 0.01));
      PLUG()->SetUnitMorph(mDragMorphUnit, next);
      SetDirty(false);
      return;
    }
    if (mDragUnit < 0)
      return;
    const double next = std::max(-20.0, std::min(20.0, GetUnitLevel(mDragUnit) - (double)dY * 0.15));
    SetUnitLevel(mDragUnit, next);
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    mDragUnit = -1;
    mDragMorphUnit = -1;
  }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    for (int i = 0; i < kNumUnits; i++)
    {
      if (KnobRect(i).Contains(x, y))
      {
        SetUnitLevel(i, 0.0);
        SetDirty(false);
        return;
      }
      if (MorphKnobRect(i).Contains(x, y))
      {
        PLUG()->SetUnitMorph(i, 0.0);
        SetDirty(false);
        return;
      }
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    const bool overExpand = ExpandRect().Contains(x, y);
    int overChoose = -1, overLED = -1, overClear = -1;
    int barHover = -1;
    if (PrevRect().Contains(x, y))
      barHover = 0;
    else if (NextRect().Contains(x, y))
      barHover = 1;
    else if (PresetNameRect().Contains(x, y))
      barHover = 2;
    else if (SaveRect().Contains(x, y))
      barHover = 3;
    else if (SaveAsRect().Contains(x, y))
      barHover = 4;
    else if (ShareRect().Contains(x, y))
      barHover = 5;
    else if (SearchRect().Contains(x, y))
      barHover = 6;
    for (int i = 0; i < kNumUnits; i++)
    {
      if (ChooseRect(i).Contains(x, y))
        overChoose = i;
      if (LEDRect(i).Contains(x, y))
        overLED = i;
      if (i > 0 && ClearRect(i).Contains(x, y))
        overClear = i;
    }
    if (overExpand != mMouseOverExpand || overChoose != mMouseOverChoose || overLED != mMouseOverLED
        || overClear != mMouseOverClear || barHover != mBarHover)
    {
      mMouseOverExpand = overExpand;
      mMouseOverChoose = overChoose;
      mMouseOverLED = overLED;
      mMouseOverClear = overClear;
      mBarHover = barHover;
      if (barHover == 2)
        SetTooltip("Rig presets - click to browse");
      else if (barHover == 4)
        SetTooltip("Save as new (type Folder/Sub/Name to file it)");
      else if (barHover == 5)
        SetTooltip("Export this rig + its tones for a friend");
      else if (barHover == 6)
        SetTooltip("Search presets in every folder");
      else if (overLED >= 0)
        SetTooltip("Bypass this unit");
      else if (overChoose >= 0)
        SetTooltip("Choose a tone from your library");
      else if (overClear >= 0)
        SetTooltip("Empty this slot");
      else
        SetTooltip("");
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    mMouseOverExpand = false;
    mMouseOverChoose = -1;
    mMouseOverLED = -1;
    mMouseOverClear = -1;
    mBarHover = -1;
    SetDirty(false);
  }

private:
  // --- Rig preset bar ------------------------------------------------------
  enum class ERigEntry
  {
    None,
    SaveAs,
    Search
  };

  IRECT BarRow() const
  {
    return IRECT(
      mRECT.L, mRECT.T + kChainHeaderHeight * 0.5f - 11.0f, mRECT.R, mRECT.T + kChainHeaderHeight * 0.5f + 11.0f);
  }
  IRECT PrevRect() const
  {
    const IRECT r = BarRow();
    return IRECT(r.L + 140.0f, r.T, r.L + 162.0f, r.B);
  }
  IRECT NextRect() const
  {
    const IRECT r = BarRow();
    return IRECT(r.L + 166.0f, r.T, r.L + 188.0f, r.B);
  }
  IRECT PresetNameRect() const
  {
    const IRECT r = BarRow();
    return IRECT(r.L + 196.0f, r.T, r.L + 426.0f, r.B);
  }
  IRECT SaveRect() const
  {
    const IRECT r = BarRow();
    return IRECT(r.L + 432.0f, r.T, r.L + 478.0f, r.B);
  }
  IRECT SaveAsRect() const
  {
    const IRECT r = BarRow();
    return IRECT(r.L + 484.0f, r.T, r.L + 550.0f, r.B);
  }
  IRECT ShareRect() const
  {
    const IRECT r = BarRow();
    return IRECT(r.L + 556.0f, r.T, r.L + 580.0f, r.B);
  }
  IRECT SearchRect() const
  {
    const IRECT r = BarRow();
    return IRECT(r.L + 586.0f, r.T, r.L + 610.0f, r.B);
  }

  void DrawPresetBar(IGraphics& g, const IColor& accent)
  {
    auto pill = [&](const IRECT& r, bool hover, bool accented) {
      g.FillRoundRect(IColor(255, 32, 33, 41), r, r.H() * 0.5f);
      if (hover)
        g.DrawRoundRect(accent, r, r.H() * 0.5f);
      else
        g.DrawRoundRect(IColor(30, 255, 255, 255), r, r.H() * 0.5f);
      (void)accented;
    };
    const IColor dim = namtheme::TEXT_DIM;

    // Prev / next arrows
    pill(PrevRect(), mBarHover == 0, false);
    pill(NextRect(), mBarHover == 1, false);
    {
      const IRECT p = PrevRect().GetCentredInside(8.0f);
      g.FillTriangle(mBarHover == 0 ? COLOR_WHITE : dim, p.R, p.T, p.R, p.B, p.L, p.MH());
      const IRECT n = NextRect().GetCentredInside(8.0f);
      g.FillTriangle(mBarHover == 1 ? COLOR_WHITE : dim, n.L, n.T, n.L, n.B, n.R, n.MH());
    }

    // Preset name dropdown
    const IRECT name = PresetNameRect();
    pill(name, mBarHover == 2, false);
    const std::string disp = namrig::PresetDisplayName(PLUG()->mRigPresetRel.Get());
    const bool none = disp == "No preset";
    const IText nameText(10.0f, none ? namtheme::TEXT_FAINT : COLOR_WHITE, "Inter-Bold", EAlign::Near, EVAlign::Middle);
    g.DrawText(
      nameText, tonegallery::Ellipsize(disp, 32).c_str(), name.GetReducedFromLeft(12.0f).GetReducedFromRight(20.0f));
    // chevron
    const IRECT ch = name.GetFromRight(20.0f).GetCentredInside(8.0f, 5.0f);
    g.FillTriangle(mBarHover == 2 ? accent : dim, ch.L, ch.T, ch.R, ch.T, ch.MW(), ch.B);

    // SAVE / SAVE AS
    pill(SaveRect(), mBarHover == 3, false);
    pill(SaveAsRect(), mBarHover == 4, false);
    const IText btnText(8.5f, dim, "Inter-Bold", EAlign::Center, EVAlign::Middle);
    IText saveText = btnText;
    if (mBarHover == 3)
      saveText.mFGColor = COLOR_WHITE;
    g.DrawText(saveText, "SAVE", SaveRect());
    IText saveAsText = btnText;
    if (mBarHover == 4)
      saveAsText.mFGColor = COLOR_WHITE;
    g.DrawText(saveAsText, "SAVE AS", SaveAsRect());

    // Share (box with an up arrow)
    {
      const IRECT r = ShareRect();
      if (mBarHover == 5)
        g.FillRoundRect(PluginColors::MOUSEOVER, r, 4.0f);
      const IColor c = mBarHover == 5 ? COLOR_WHITE : dim;
      const IRECT box = r.GetCentredInside(12.0f).GetReducedFromTop(3.0f);
      g.DrawRoundRect(c, box, 1.5f, nullptr, 1.2f);
      const float mx = r.MW();
      g.DrawLine(c, mx, box.T - 4.0f, mx, box.T + 3.0f, nullptr, 1.2f);
      g.DrawLine(c, mx - 3.0f, box.T - 1.0f, mx, box.T - 4.0f, nullptr, 1.2f);
      g.DrawLine(c, mx + 3.0f, box.T - 1.0f, mx, box.T - 4.0f, nullptr, 1.2f);
    }
    // Search (magnifier)
    {
      const IRECT r = SearchRect();
      if (mBarHover == 6)
        g.FillRoundRect(PluginColors::MOUSEOVER, r, 4.0f);
      const IColor c = mBarHover == 6 ? COLOR_WHITE : dim;
      const IRECT m = r.GetCentredInside(12.0f);
      g.DrawEllipse(c, m.GetFromTLHC(8.0f, 8.0f), nullptr, 1.4f);
      g.DrawLine(c, m.L + 7.0f, m.T + 7.0f, m.R, m.B, nullptr, 1.4f);
    }

    // Status flash
    if (mRigStatusFrames > 0)
    {
      const IText statusText(8.5f, accent, "Inter-Bold", EAlign::Near, EVAlign::Middle);
      g.DrawText(
        statusText, mRigStatus.c_str(), IRECT(SearchRect().R + 10.0f, BarRow().T, ExpandRect().L - 6.0f, BarRow().B));
      mRigStatusFrames--;
      SetDirty(false);
    }
  }

  void Flash(const std::string& msg)
  {
    mRigStatus = msg;
    mRigStatusFrames = 120;
    SetDirty(false);
  }

  void BuildMenuRec(IPopupMenu* pMenu, const std::filesystem::path& dir)
  {
    namespace fs = std::filesystem;
    std::vector<fs::path> dirs, files;
    try
    {
      for (const auto& it : fs::directory_iterator(dir))
      {
        if (it.is_directory())
          dirs.push_back(it.path());
        else if (tonegallery::HasExtension(it.path(), ".namrig"))
          files.push_back(it.path());
      }
    }
    catch (const std::exception&)
    {
    }
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());
    for (const auto& d : dirs)
    {
      IPopupMenu* pSub = new IPopupMenu();
      BuildMenuRec(pSub, d);
      const std::string label = tonegallery::PathToUTF8(d.filename());
      if (pSub->NItems() == 0)
      {
        delete pSub;
        pMenu->AddItem(new IPopupMenu::Item((label + " (empty)").c_str(), IPopupMenu::Item::kDisabled));
      }
      else
        pMenu->AddItem(new IPopupMenu::Item(label.c_str(), pSub));
    }
    for (const auto& f : files)
    {
      const int tag = (int)mRigMenuPaths.size();
      mRigMenuPaths.push_back(tonegallery::PathToUTF8(f));
      pMenu->AddItem(new IPopupMenu::Item(tonegallery::PathToUTF8(f.stem()).c_str(), IPopupMenu::Item::kNoFlags, tag));
    }
  }

  void OpenPresetMenu()
  {
    namrig::EnsurePresetsRoot();
    mRigMenu.Clear();
    mRigMenuPaths.clear();
    BuildMenuRec(&mRigMenu, namrig::PresetsRoot());
    if (mRigMenu.NItems() == 0)
      mRigMenu.AddItem(new IPopupMenu::Item("(no rigs saved yet - use SAVE AS)", IPopupMenu::Item::kDisabled));
    GetUI()->CreatePopupMenu(*this, mRigMenu, PresetNameRect());
  }

  void OpenSearchMenu(const char* query)
  {
    namespace fs = std::filesystem;
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    mRigMenu.Clear();
    mRigMenuPaths.clear();
    try
    {
      std::vector<fs::path> hits;
      for (const auto& it : fs::recursive_directory_iterator(namrig::PresetsRoot()))
      {
        if (!it.is_regular_file() || !tonegallery::HasExtension(it.path(), ".namrig"))
          continue;
        std::string stem = tonegallery::PathToUTF8(it.path().stem());
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (stem.find(q) != std::string::npos)
          hits.push_back(it.path());
      }
      std::sort(hits.begin(), hits.end());
      for (const auto& f : hits)
      {
        const int tag = (int)mRigMenuPaths.size();
        mRigMenuPaths.push_back(tonegallery::PathToUTF8(f));
        std::string label = tonegallery::PathToUTF8(f.stem());
        const std::string folder = tonegallery::PathToUTF8(f.parent_path().lexically_relative(namrig::PresetsRoot()));
        if (!folder.empty() && folder != ".")
          label += " (" + folder + ")";
        mRigMenu.AddItem(new IPopupMenu::Item(label.c_str(), IPopupMenu::Item::kNoFlags, tag));
      }
    }
    catch (const std::exception&)
    {
    }
    if (mRigMenu.NItems() == 0)
      mRigMenu.AddItem(new IPopupMenu::Item("(no matches)", IPopupMenu::Item::kDisabled));
    GetUI()->CreatePopupMenu(*this, mRigMenu, PresetNameRect());
  }

  void LoadPresetFile(const std::string& absPath)
  {
    try
    {
      std::ifstream f(tonegallery::UTF8ToPath(absPath));
      nlohmann::json j = nlohmann::json::parse(f, nullptr, true, true);
      PLUG()->ApplyRigPreset(j);
      PLUG()->mRigPresetRel.Set(namrig::RelOfPreset(tonegallery::UTF8ToPath(absPath)).c_str());
      for (int i = 0; i < 3; i++)
      {
        mSlotCacheKey[i].clear();
        mSlotCacheValid[i] = false;
      }
      Flash("Loaded");
      GetUI()->SetAllControlsDirty();
    }
    catch (const std::exception&)
    {
      Flash("Couldn't load that preset");
    }
  }

  void StepPreset(int delta)
  {
    namespace fs = std::filesystem;
    try
    {
      namrig::EnsurePresetsRoot();
      // Step within the current preset's folder (or the root if none).
      fs::path dir = namrig::PresetsRoot();
      std::string currentFile;
      if (PLUG()->mRigPresetRel.GetLength())
      {
        const fs::path cur = namrig::PresetsRoot() / tonegallery::UTF8ToPath(PLUG()->mRigPresetRel.Get());
        dir = cur.parent_path();
        currentFile = tonegallery::PathToUTF8(cur.filename());
      }
      std::vector<fs::path> files;
      for (const auto& it : fs::directory_iterator(dir))
        if (it.is_regular_file() && tonegallery::HasExtension(it.path(), ".namrig"))
          files.push_back(it.path());
      std::sort(files.begin(), files.end());
      if (files.empty())
      {
        Flash("No presets here yet");
        return;
      }
      int idx = 0;
      for (int i = 0; i < (int)files.size(); i++)
        if (tonegallery::PathToUTF8(files[i].filename()) == currentFile)
        {
          idx = i + delta;
          break;
        }
      idx = ((idx % (int)files.size()) + (int)files.size()) % (int)files.size();
      LoadPresetFile(tonegallery::PathToUTF8(files[idx]));
    }
    catch (const std::exception&)
    {
      Flash("Couldn't switch preset");
    }
  }

  void SavePreset()
  {
    if (!PLUG()->mRigPresetRel.GetLength())
    {
      BeginTextEntry(ERigEntry::SaveAs);
      return;
    }
    try
    {
      const std::filesystem::path file = namrig::PresetsRoot() / tonegallery::UTF8ToPath(PLUG()->mRigPresetRel.Get());
      std::filesystem::create_directories(file.parent_path());
      std::ofstream f(file);
      f << PLUG()->CaptureRigPreset().dump(2);
      Flash("Saved");
    }
    catch (const std::exception&)
    {
      Flash("Couldn't save");
    }
  }

  void BeginTextEntry(ERigEntry mode)
  {
    mRigEntryMode = mode;
    const IText entryText(11.0f, COLOR_WHITE, "Inter-Regular", EAlign::Near, EVAlign::Middle);
    const char* seed = "";
    std::string cur;
    if (mode == ERigEntry::SaveAs && PLUG()->mRigPresetRel.GetLength())
    {
      cur = namrig::PresetDisplayName(PLUG()->mRigPresetRel.Get());
      seed = cur.c_str();
    }
    GetUI()->CreateTextEntry(*this, entryText, PresetNameRect(), seed);
  }

  void ExportCurrentRig()
  {
    std::string name = namrig::PresetDisplayName(PLUG()->mRigPresetRel.Get());
    if (name == "No preset")
      name = "My Rig";
    std::string msg;
    namrig::ExportRigBundle(PLUG()->CaptureRigPreset(), name, msg);
    Flash(msg);
  }

  // --- Geometry ------------------------------------------------------------
  IRECT UnitRect(int i) const
  {
    const float y = mRECT.T + kChainHeaderHeight + i * kChainUnitHeight;
    return IRECT(mRECT.L, y, mRECT.R, y + kChainUnitHeight);
  }
  IRECT ExpandRect() const { return mRECT.GetFromTop(kChainHeaderHeight).GetFromRight(38.0f).GetCentredInside(24.0f); }
  IRECT LEDRect(int i) const
  {
    const IRECT face = UnitRect(i);
    return IRECT(face.L + 32.0f, face.MH() - 18.0f, face.L + 52.0f, face.MH() + 2.0f);
  }
  IRECT ScreenRect(int i) const
  {
    const IRECT face = UnitRect(i);
    return IRECT(face.L + 62.0f, face.T + 10.0f, face.L + 202.0f, face.B - 12.0f);
  }
  IRECT TextRect(int i) const
  {
    const IRECT face = UnitRect(i);
    return IRECT(face.L + 214.0f, face.T + 12.0f, face.R - 220.0f, face.B - 12.0f);
  }
  // The whole click-to-pick region: screen + text block
  IRECT ChooseRect(int i) const
  {
    const IRECT face = UnitRect(i);
    return IRECT(face.L + 62.0f, face.T + 8.0f, face.R - 220.0f, face.B - 8.0f);
  }
  IRECT KnobRect(int i) const
  {
    const IRECT face = UnitRect(i);
    return IRECT(face.R - 150.0f, face.MH() - 26.0f, face.R - 98.0f, face.MH() + 26.0f);
  }
  IRECT ClearRect(int i) const
  {
    const IRECT face = UnitRect(i);
    return IRECT(face.R - 72.0f, face.MH() - 11.0f, face.R - 50.0f, face.MH() + 11.0f);
  }
  // Tone Morph knob: sits just left of the LEVEL knob.
  IRECT MorphKnobRect(int i) const
  {
    const IRECT face = UnitRect(i);
    return IRECT(face.R - 210.0f, face.MH() - 24.0f, face.R - 168.0f, face.MH() + 24.0f);
  }

  // --- Per-unit state ------------------------------------------------------
  bool GetUnitEnabled(int i)
  {
    return i == 0 ? PLUG()->mChainMainEnabled.load() : PLUG()->mChainSlots[i - 1].enabled.load();
  }
  void SetUnitEnabled(int i, bool v)
  {
    if (i == 0)
      PLUG()->mChainMainEnabled = v;
    else
      PLUG()->mChainSlots[i - 1].enabled = v;
  }
  double GetUnitLevel(int i)
  {
    return i == 0 ? PLUG()->mChainMainLevelDB.load() : PLUG()->mChainSlots[i - 1].levelDB.load();
  }
  void SetUnitLevel(int i, double v)
  {
    if (i == 0)
      PLUG()->mChainMainLevelDB = v;
    else
      PLUG()->mChainSlots[i - 1].levelDB = v;
  }
  bool UnitHasTone(int i)
  {
    if (i == 0)
      return mHasMainEntry;
    return PLUG()->mChainSlots[i - 1].modelPath.GetLength() > 0 || PLUG()->mChainSlots[i - 1].irPath.GetLength() > 0;
  }

  // Fetch (and cache) the ToneEntry describing what's in a chain slot.
  const tonegallery::ToneEntry* SlotEntry(int slot)
  {
    const std::string tonePath = PLUG()->mChainSlots[slot].tonePath.Get();
    if (tonePath != mSlotCacheKey[slot])
    {
      mSlotCacheKey[slot] = tonePath;
      mSlotCacheValid[slot] = false;
      if (!tonePath.empty())
      {
        try
        {
          tonegallery::ToneEntry entry;
          if (tonegallery::ScanToneFolder(tonegallery::UTF8ToPath(tonePath), entry))
          {
            mSlotCache[slot] = entry;
            mSlotCacheValid[slot] = true;
          }
        }
        catch (const std::exception&)
        {
        }
      }
    }
    return mSlotCacheValid[slot] ? &mSlotCache[slot] : nullptr;
  }

  // --- Drawing -------------------------------------------------------------
  void DrawUnit(IGraphics& g, int i, const IColor& accent)
  {
    const IRECT face = UnitRect(i);

    // Faceplate
    g.PathRect(face);
    g.PathFill(IPattern::CreateLinearGradient(
      face.L, face.T, face.L, face.B,
      {{IColor(255, 30, 31, 36), 0.0f}, {IColor(255, 20, 21, 25), 0.35f}, {IColor(255, 16, 17, 20), 1.0f}}));
    g.FillRect(IColor(30, 255, 255, 255), face.GetFromTop(1.0f));
    g.FillRect(COLOR_BLACK.WithOpacity(0.5f), face.GetFromBottom(2.0f));

    // Rack ears + screws
    for (int side = 0; side < 2; side++)
    {
      const IRECT ear = side == 0 ? face.GetFromLeft(24.0f) : face.GetFromRight(24.0f);
      g.FillRect(IColor(255, 24, 25, 29), ear);
      g.FillRect(IColor(20, 255, 255, 255), side == 0 ? ear.GetFromRight(1.0f) : ear.GetFromLeft(1.0f));
      for (int screw = 0; screw < 2; screw++)
      {
        const IRECT sr = (screw == 0 ? ear.GetFromTop(24.0f) : ear.GetFromBottom(24.0f)).GetCentredInside(10.0f);
        g.FillEllipse(IColor(255, 48, 50, 58), sr);
        g.DrawEllipse(COLOR_BLACK.WithOpacity(0.6f), sr);
        g.DrawLine(IColor(255, 90, 93, 104), sr.L + 2.0f, sr.MH(), sr.R - 2.0f, sr.MH(), nullptr, 1.2f);
      }
    }

    const bool enabled = GetUnitEnabled(i);

    // Bypass LED + unit number
    const IRECT led = LEDRect(i);
    const IColor ledColor = enabled ? accent : IColor(255, 70, 72, 80);
    if (enabled)
    {
      g.PathCircle(led.MW(), led.MH(), 7.0f);
      g.PathFill(IPattern::CreateRadialGradient(
        led.MW(), led.MH(), 9.0f, {{accent.WithOpacity(0.55f), 0.0f}, {COLOR_TRANSPARENT, 1.0f}}));
    }
    g.FillEllipse(ledColor, led.GetCentredInside(7.0f));
    if (mMouseOverLED == i)
      g.DrawEllipse(COLOR_WHITE.WithOpacity(0.6f), led.GetCentredInside(11.0f));
    const IText numText(8.0f, namtheme::TEXT_FAINT, "Inter-Bold", EAlign::Center, EVAlign::Middle);
    const char num[2] = {(char)('1' + i), 0};
    g.DrawText(numText, num, IRECT(led.L, led.B + 4.0f, led.R, led.B + 16.0f));

    // What's loaded?
    const tonegallery::ToneEntry* pEntry = nullptr;
    std::string fallbackName;
    if (i == 0)
    {
      if (mHasMainEntry)
        pEntry = &mMainEntry;
      else
        fallbackName = "Main tone";
    }
    else
    {
      pEntry = SlotEntry(i - 1);
      if (pEntry == nullptr && UnitHasTone(i))
      {
        try
        {
          const std::string mp = PLUG()->mChainSlots[i - 1].modelPath.Get();
          const std::string ip = PLUG()->mChainSlots[i - 1].irPath.Get();
          fallbackName = tonegallery::PathToUTF8(tonegallery::UTF8ToPath(mp.empty() ? ip : mp).stem());
        }
        catch (const std::exception&)
        {
          fallbackName = "Loaded file";
        }
      }
    }
    const bool haveTone = pEntry != nullptr || !fallbackName.empty();

    // LCD screen
    const IRECT screen = ScreenRect(i);
    g.FillRoundRect(COLOR_BLACK, screen.GetPadded(3.0f), 5.0f);
    g.DrawRoundRect(IColor(40, 255, 255, 255), screen.GetPadded(3.0f), 5.0f);
    IBitmap* pBitmap = pEntry != nullptr ? GetImage(pEntry->imagePath) : nullptr;
    if (pBitmap != nullptr && pBitmap->W() > 0 && pBitmap->H() > 0)
    {
      const float bmpAspect = (float)pBitmap->W() / (float)pBitmap->H();
      const float areaAspect = screen.W() / screen.H();
      IRECT cover = screen;
      if (bmpAspect > areaAspect)
        cover = screen.GetMidHPadded(0.5f * screen.H() * bmpAspect);
      else
        cover = screen.GetMidVPadded(0.5f * screen.W() / bmpAspect);
      g.PathClipRegion(screen);
      g.DrawFittedBitmap(*pBitmap, cover);
      g.PathClipRegion();
      // Screen glass sheen
      g.PathRect(screen.GetFromTop(screen.H() * 0.4f));
      g.PathFill(IPattern::CreateLinearGradient(screen.L, screen.T, screen.L, screen.T + screen.H() * 0.4f,
                                                {{COLOR_WHITE.WithOpacity(0.07f), 0.0f}, {COLOR_TRANSPARENT, 1.0f}}));
    }
    else if (haveTone)
    {
      g.FillRoundRect(accent.WithOpacity(0.10f), screen, 4.0f);
      const IText initText(20.0f, accent, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      std::string initials;
      const std::string nm = pEntry != nullptr ? pEntry->name : fallbackName;
      initials += nm.empty() ? '?' : (char)std::toupper((unsigned char)nm[0]);
      g.DrawText(initText, initials.c_str(), screen);
    }
    else
    {
      // Empty slot: dashed border + prompt
      const IColor promptColor = mMouseOverChoose == i ? accent : namtheme::TEXT_FAINT;
      const float dash = 6.0f;
      for (float dx = screen.L; dx < screen.R; dx += dash * 2.0f)
      {
        g.DrawLine(promptColor.WithOpacity(0.5f), dx, screen.T, std::min(dx + dash, screen.R), screen.T, nullptr, 1.0f);
        g.DrawLine(promptColor.WithOpacity(0.5f), dx, screen.B, std::min(dx + dash, screen.R), screen.B, nullptr, 1.0f);
      }
      for (float dy = screen.T; dy < screen.B; dy += dash * 2.0f)
      {
        g.DrawLine(promptColor.WithOpacity(0.5f), screen.L, dy, screen.L, std::min(dy + dash, screen.B), nullptr, 1.0f);
        g.DrawLine(promptColor.WithOpacity(0.5f), screen.R, dy, screen.R, std::min(dy + dash, screen.B), nullptr, 1.0f);
      }
      const IText plusText(10.0f, promptColor, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(plusText, "+ CHOOSE A TONE", screen);
    }

    // Hover ring around the choose region
    if (mMouseOverChoose == i && haveTone)
      g.DrawRoundRect(accent.WithOpacity(0.7f), screen.GetPadded(3.0f), 5.0f, nullptr, 1.4f);

    // Name / gear / author
    const IRECT text = TextRect(i);
    const IText nameText(
      12.0f, enabled ? namtheme::TEXT_MAIN : namtheme::TEXT_DIM, "Inter-Bold", EAlign::Near, EVAlign::Top);
    std::string displayName = pEntry != nullptr ? pEntry->name : (!fallbackName.empty() ? fallbackName : "Empty");
    // Tone Morph: show the B tone in the name as "A -> B".
    if (PLUG()->UnitHasB(i))
    {
      const char* btp = PLUG()->GetUnitBTonePath(i);
      std::string bName;
      if (btp != nullptr && btp[0] != '\0')
      {
        try
        {
          bName = tonegallery::PathToUTF8(tonegallery::UTF8ToPath(btp).filename());
        }
        catch (const std::exception&)
        {
        }
      }
      if (!bName.empty())
        displayName += " \xe2\x86\x92 " + bName;
    }
    float ty = text.T;
    for (const auto& line : tonegallery::WrapLines(displayName, 30, 2))
    {
      g.DrawText(nameText, line.c_str(), IRECT(text.L, ty, text.R, ty + 15.0f));
      ty += 15.0f;
    }
    ty += 4.0f;
    if (pEntry != nullptr)
    {
      const IColor gearColor = tonegallery::GearTypeColor(pEntry->gearType);
      const char* gearLabel = tonegallery::GearTypeChipLabel(pEntry->gearType);
      const float gw = 10.0f + 4.6f * (float)strlen(gearLabel);
      const IRECT chip = IRECT(text.L, ty, text.L + gw, ty + 13.0f);
      g.FillRoundRect(enabled ? gearColor : IColor(255, 60, 62, 70), chip, 5.0f);
      const IText chipText(6.5f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(chipText, gearLabel, chip);
      if (!pEntry->author.empty())
      {
        const IText byText(8.0f, namtheme::TEXT_FAINT, "Inter-Regular", EAlign::Near, EVAlign::Middle);
        g.DrawText(byText, ("by " + pEntry->author).c_str(), IRECT(chip.R + 8.0f, ty, text.R, ty + 13.0f));
      }
    }
    else if (!haveTone)
    {
      const IText emptyText(8.5f, namtheme::TEXT_FAINT, "Inter-Regular", EAlign::Near, EVAlign::Top);
      g.DrawText(emptyText, i == 0 ? "Pick from your library or the sidebar" : "This unit is skipped while empty",
                 IRECT(text.L, ty, text.R, ty + 12.0f));
    }

    // Which variation (model/IR file) is actually loaded in this unit
    if (haveTone)
    {
      std::string variation;
      try
      {
        std::string vp;
        if (i == 0)
          vp = !mMainModelPath.empty() ? mMainModelPath : mMainIRPath;
        else
        {
          const std::string mp = PLUG()->mChainSlots[i - 1].modelPath.Get();
          const std::string ip = PLUG()->mChainSlots[i - 1].irPath.Get();
          vp = !mp.empty() ? mp : ip;
        }
        if (!vp.empty())
          variation = tonegallery::PathToUTF8(tonegallery::UTF8ToPath(vp).stem());
      }
      catch (const std::exception&)
      {
      }
      if (!variation.empty())
      {
        const IText varLabel(6.5f, namtheme::TEXT_FAINT, "Inter-Bold", EAlign::Near, EVAlign::Middle);
        const IRECT varRow = IRECT(text.L, ty + 19.0f, text.R, ty + 31.0f);
        IRECT varBounds;
        g.MeasureText(varLabel, "VARIATION", varBounds);
        g.DrawText(varLabel, "VARIATION", varRow);
        const IText varText(
          8.0f, enabled ? namtheme::TEXT_DIM : namtheme::TEXT_FAINT, "Inter-Regular", EAlign::Near, EVAlign::Middle);
        g.DrawText(
          varText, tonegallery::Ellipsize(variation, 34).c_str(), varRow.GetReducedFromLeft(varBounds.W() + 6.0f));
      }
    }

    // Level knob
    const IRECT knob = KnobRect(i);
    const float cx = knob.MW();
    const float cy = knob.MH() - 4.0f;
    const float radius = 16.0f;
    const double level = GetUnitLevel(i);
    const float frac = (float)((level + 20.0) / 40.0);
    const float a1 = -135.0f, a2 = 135.0f;
    const float angle = a1 + frac * (a2 - a1);
    g.DrawArc(IColor(255, 45, 47, 54), cx, cy, radius, a1, a2, nullptr, 3.0f);
    const float zeroAngle = 0.0f;
    if (angle < zeroAngle)
      g.DrawArc(enabled ? accent : namtheme::TEXT_FAINT, cx, cy, radius, angle, zeroAngle, nullptr, 3.0f);
    else if (angle > zeroAngle)
      g.DrawArc(enabled ? accent : namtheme::TEXT_FAINT, cx, cy, radius, zeroAngle, angle, nullptr, 3.0f);
    g.FillEllipse(IColor(255, 36, 37, 44), IRECT(cx - 10.0f, cy - 10.0f, cx + 10.0f, cy + 10.0f));
    // Pointer dot
    const float rad = (angle - 90.0f) * 3.14159265f / 180.0f;
    const float px = cx + 7.0f * std::cos(rad);
    const float py = cy + 7.0f * std::sin(rad);
    g.FillEllipse(enabled ? accent : namtheme::TEXT_DIM, IRECT(px - 2.0f, py - 2.0f, px + 2.0f, py + 2.0f));
    char lvl[24];
    snprintf(lvl, sizeof(lvl), "%+.1f dB", level);
    const IText lvlText(7.5f, namtheme::TEXT_DIM, "Inter-Regular", EAlign::Center, EVAlign::Middle);
    g.DrawText(lvlText, lvl, IRECT(knob.L - 8.0f, knob.B - 6.0f, knob.R + 8.0f, knob.B + 6.0f));
    const IText lvlLabel(6.5f, namtheme::TEXT_FAINT, "Inter-Bold", EAlign::Center, EVAlign::Middle);
    g.DrawText(lvlLabel, "LEVEL", IRECT(knob.L - 8.0f, knob.T - 4.0f, knob.R + 8.0f, knob.T + 6.0f));

    // Tone Morph knob: blends this unit toward its B tone.
    {
      const bool hasB = PLUG()->UnitHasB(i);
      const IRECT mk = MorphKnobRect(i);
      const float mcx = mk.MW();
      const float mcy = mk.MH() - 2.0f;
      const float mradius = 13.0f;
      const double morphAmt = PLUG()->GetUnitMorph(i);
      const float ma1 = -135.0f, ma2 = 135.0f;
      const float mangle = ma1 + (float)morphAmt * (ma2 - ma1);
      const IColor morphCol = (enabled && hasB) ? accent : namtheme::TEXT_FAINT;
      g.DrawArc(IColor(255, 45, 47, 54), mcx, mcy, mradius, ma1, ma2, nullptr, 3.0f);
      if (hasB && mangle > ma1 + 0.5f)
        g.DrawArc(morphCol, mcx, mcy, mradius, ma1, mangle, nullptr, 3.0f);
      g.FillEllipse(IColor(255, 36, 37, 44), IRECT(mcx - 9.0f, mcy - 9.0f, mcx + 9.0f, mcy + 9.0f));
      if (hasB)
      {
        const float mrad = (mangle - 90.0f) * 3.14159265f / 180.0f;
        const float mpx = mcx + 6.0f * std::cos(mrad);
        const float mpy = mcy + 6.0f * std::sin(mrad);
        g.FillEllipse(morphCol, IRECT(mpx - 2.0f, mpy - 2.0f, mpx + 2.0f, mpy + 2.0f));
      }
      else
      {
        const IText addText(12.0f, accent.WithOpacity(0.85f), "Inter-Bold", EAlign::Center, EVAlign::Middle);
        g.DrawText(addText, "+", IRECT(mcx - 9.0f, mcy - 9.0f, mcx + 9.0f, mcy + 9.0f));
      }
      const IText mLabel(
        6.5f, (enabled && hasB) ? accent : namtheme::TEXT_FAINT, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(mLabel, "MORPH", IRECT(mk.L - 6.0f, mk.T - 4.0f, mk.R + 6.0f, mk.T + 6.0f));
      if (hasB)
      {
        char mtxt[16];
        snprintf(mtxt, sizeof(mtxt), "%.0f%%", morphAmt * 100.0);
        const IText mReadout(7.5f, namtheme::TEXT_DIM, "Inter-Regular", EAlign::Center, EVAlign::Middle);
        g.DrawText(mReadout, mtxt, IRECT(mk.L - 6.0f, mk.B - 6.0f, mk.R + 6.0f, mk.B + 6.0f));
      }
      else
      {
        const IText mReadout(6.0f, namtheme::TEXT_FAINT, "Inter-Regular", EAlign::Center, EVAlign::Middle);
        g.DrawText(mReadout, "ADD B TONE", IRECT(mk.L - 10.0f, mk.B - 6.0f, mk.R + 10.0f, mk.B + 6.0f));
      }
    }

    // Clear button (extra slots only, when loaded)
    if (i > 0 && UnitHasTone(i))
    {
      const IRECT clear = ClearRect(i);
      const IColor xc = mMouseOverClear == i ? IColor(255, 232, 90, 90) : namtheme::TEXT_FAINT;
      const IRECT xr = clear.GetCentredInside(9.0f);
      g.DrawLine(xc, xr.L, xr.T, xr.R, xr.B, nullptr, 1.6f);
      g.DrawLine(xc, xr.L, xr.B, xr.R, xr.T, nullptr, 1.6f);
    }

    // Bypassed: dim the whole unit
    if (!enabled)
      g.FillRect(COLOR_BLACK.WithOpacity(0.45f), face);

    // Signal-flow chevron into the next unit
    if (i < kNumUnits - 1)
    {
      const float mx = face.MW();
      const float my = face.B;
      g.FillTriangle(accent.WithOpacity(0.8f), mx - 6.0f, my - 3.0f, mx + 6.0f, my - 3.0f, mx, my + 4.0f);
    }
  }

  IBitmap* GetImage(const std::string& path)
  {
    if (path.empty())
      return nullptr;
    auto found = mImageCache.find(path);
    if (found != mImageCache.end())
      return found->second.GetAPIBitmap() ? &found->second : nullptr;
    IBitmap bitmap;
    try
    {
      if (std::filesystem::exists(tonegallery::UTF8ToPath(path)))
        bitmap = GetUI()->LoadBitmap(path.c_str());
    }
    catch (const std::exception&)
    {
    }
    auto inserted = mImageCache.insert({path, bitmap});
    return inserted.first->second.GetAPIBitmap() ? &inserted.first->second : nullptr;
  }

  tonegallery::ToneEntry mMainEntry;
  bool mHasMainEntry = false;
  std::string mMainModelPath;
  std::string mMainIRPath;
  tonegallery::ToneEntry mSlotCache[3];
  std::string mSlotCacheKey[3];
  bool mSlotCacheValid[3] = {false, false, false};
  std::map<std::string, IBitmap> mImageCache;
  int mDragUnit = -1;
  int mDragMorphUnit = -1;
  // Rig preset bar state
  ERigEntry mRigEntryMode = ERigEntry::None;
  IPopupMenu mRigMenu;
  std::vector<std::string> mRigMenuPaths;
  std::string mRigStatus;
  int mRigStatusFrames = 0;
  int mBarHover = -1; // 0 prev, 1 next, 2 name, 3 save, 4 save-as, 5 share, 6 search
  bool mMouseOverExpand = false;
  int mMouseOverChoose = -1;
  int mMouseOverLED = -1;
  int mMouseOverClear = -1;
  IFileDialogCompletionHandlerFunc mLoadModelFunc;
  IFileDialogCompletionHandlerFunc mLoadIRFunc;
};

// The wide SIGNAL CHAIN button in the titlebar. Normally it opens the
// stacked chain view; while a rack unit is being edited it turns into an
// accent-colored BACK TO RACK button (and returns you to the rack).
class NAMChainButtonControl : public IControl
{
public:
  NAMChainButtonControl(const IRECT& bounds)
  : IControl(bounds)
  {
    SetTooltip("Signal chain (stacked rack)");
  }

  void Draw(IGraphics& g) override
  {
    const IColor accent = namtheme::Accent();
    const int editUnit = PLUG()->mChainEditSlot;
    const bool editing = editUnit >= 0;

    // Button body
    if (editing)
    {
      g.FillRoundRect(mMouseIsOver ? accent : accent.WithOpacity(0.85f), mRECT, mRECT.H() * 0.5f);
    }
    else
    {
      g.FillRoundRect(IColor(255, 32, 33, 41), mRECT, mRECT.H() * 0.5f);
      g.DrawRoundRect(mMouseIsOver ? accent : IColor(30, 255, 255, 255), mRECT, mRECT.H() * 0.5f);
    }

    // Stacked-rack icon on the left
    const IRECT icon = mRECT.GetFromLeft(30.0f).GetCentredInside(13.0f, 13.0f);
    const IColor iconColor = editing ? COLOR_BLACK : (mMouseIsOver ? COLOR_WHITE : namtheme::TEXT_DIM);
    for (int i = 0; i < 3; i++)
    {
      const float yy = icon.T + i * 5.0f;
      g.DrawRoundRect(iconColor, IRECT(icon.L, yy, icon.R, yy + 3.2f), 1.0f, nullptr, 1.2f);
    }

    // Label
    const IRECT textArea = mRECT.GetReducedFromLeft(28.0f).GetReducedFromRight(8.0f);
    if (editing)
    {
      const IText mainText(9.0f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      char label[32];
      snprintf(label, sizeof(label), "BACK TO RACK (%d)", editUnit == 0 ? 1 : editUnit + 1);
      g.DrawText(mainText, label, textArea);
    }
    else
    {
      const IText mainText(
        9.0f, mMouseIsOver ? COLOR_WHITE : namtheme::TEXT_MAIN, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(mainText, "SIGNAL CHAIN", textArea);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (IControl* pChain = GetUI()->GetControlWithTag(kCtrlTagChainView))
    {
      PLUG()->EndChainKnobEdit();
      PLUG()->mToneChainMode = true;
      PLUG()->mToneRackMode = false;
      if (IControl* pRack = GetUI()->GetControlWithTag(kCtrlTagRackView))
        pRack->Hide(true);
      pChain->Hide(false);
      GetUI()->Resize(PLUG_WIDTH, (int)kChainViewHeight, GetUI()->GetDrawScale());
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    SetDirty(false);
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    SetDirty(false);
  }
};
