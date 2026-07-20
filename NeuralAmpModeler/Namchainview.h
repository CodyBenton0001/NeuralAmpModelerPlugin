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

    // Header
    const IRECT header = mRECT.GetFromTop(kChainHeaderHeight);
    const IText titleText(13.0f, namtheme::TEXT_MAIN, "Inter-Bold", EAlign::Near, EVAlign::Middle);
    g.DrawText(titleText, "SIGNAL CHAIN", header.GetReducedFromLeft(16.0f));
    IRECT tBounds;
    g.MeasureText(titleText, "SIGNAL CHAIN", tBounds);
    const IText subText(8.0f, accent, "Inter-Bold", EAlign::Near, EVAlign::Middle);
    g.DrawText(subText, "IN AT THE TOP - OUT AT THE BOTTOM", header.GetReducedFromLeft(26.0f + tBounds.W()));
    const IText hintText(8.0f, namtheme::TEXT_FAINT, "Inter-Regular", EAlign::Far, EVAlign::Middle);
    g.DrawText(hintText, "click a screen to choose a tone", header.GetReducedFromRight(44.0f));

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
  void EnterEditMode(int unit)
  {
    PLUG()->BeginChainKnobEdit(unit); // also points the main knobs at the unit
    PLUG()->mToneChainMode = false;
    Hide(true);
    if (IControl* pBanner = GetUI()->GetControlWithTag(kCtrlTagChainBanner))
      pBanner->Hide(false);
    GetUI()->Resize(PLUG_WIDTH, PLUG_HEIGHT, GetUI()->GetDrawScale());
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (mDragUnit < 0)
      return;
    const double next = std::max(-20.0, std::min(20.0, GetUnitLevel(mDragUnit) - (double)dY * 0.15));
    SetUnitLevel(mDragUnit, next);
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override { mDragUnit = -1; }

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
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    const bool overExpand = ExpandRect().Contains(x, y);
    int overChoose = -1, overLED = -1, overClear = -1;
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
        || overClear != mMouseOverClear)
    {
      mMouseOverExpand = overExpand;
      mMouseOverChoose = overChoose;
      mMouseOverLED = overLED;
      mMouseOverClear = overClear;
      if (overLED >= 0)
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
    SetDirty(false);
  }

private:
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
    return IRECT(face.L + 214.0f, face.T + 12.0f, face.R - 190.0f, face.B - 12.0f);
  }
  // The whole click-to-pick region: screen + text block
  IRECT ChooseRect(int i) const
  {
    const IRECT face = UnitRect(i);
    return IRECT(face.L + 62.0f, face.T + 8.0f, face.R - 190.0f, face.B - 8.0f);
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
    const std::string displayName = pEntry != nullptr ? pEntry->name : (!fallbackName.empty() ? fallbackName : "Empty");
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
  bool mMouseOverExpand = false;
  int mMouseOverChoose = -1;
  int mMouseOverLED = -1;
  int mMouseOverClear = -1;
  IFileDialogCompletionHandlerFunc mLoadModelFunc;
  IFileDialogCompletionHandlerFunc mLoadIRFunc;
};

// The strip shown across the top of the main view while a chain unit's tone
// is being chosen: "EDITING RACK UNIT n ... DONE". Clicking it (or the DONE
// side) returns to the stacked chain view.
class NAMChainEditBannerControl : public IControl
{
public:
  NAMChainEditBannerControl(const IRECT& bounds)
  : IControl(bounds)
  {
    SetTooltip("Done - back to the rack");
  }

  void Draw(IGraphics& g) override
  {
    const IColor accent = namtheme::Accent();
    const int unit = PLUG()->mChainEditSlot;
    if (unit < 0)
      return;

    // Bar
    g.FillRect(accent.WithOpacity(mMouseIsOver ? 0.32f : 0.22f), mRECT);
    g.FillRect(accent, mRECT.GetFromBottom(2.0f));

    // Pulsing-ish label
    const IText labelText(10.0f, COLOR_WHITE, "Inter-Bold", EAlign::Near, EVAlign::Middle);
    char label[96];
    if (unit == 0)
      snprintf(label, sizeof(label), "EDITING RACK UNIT 1 - tones + knobs now target this unit");
    else
      snprintf(label, sizeof(label), "EDITING RACK UNIT %d - tones + knobs now target this unit", unit + 1);
    g.DrawText(labelText, label, mRECT.GetReducedFromLeft(14.0f));

    // DONE button (right)
    const IRECT done = mRECT.GetFromRight(130.0f);
    const IText doneText(10.0f, mMouseIsOver ? COLOR_WHITE : accent, "Inter-Bold", EAlign::Far, EVAlign::Middle);
    g.DrawText(doneText, "DONE - BACK TO RACK", done.GetReducedFromRight(14.0f));
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    PLUG()->EndChainKnobEdit(); // hands the knobs back to the main tone
    PLUG()->mToneChainMode = true;
    Hide(true);
    if (IControl* pChain = GetUI()->GetControlWithTag(kCtrlTagChainView))
      pChain->Hide(false);
    GetUI()->Resize(PLUG_WIDTH, (int)kChainViewHeight, GetUI()->GetDrawScale());
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

// Titlebar button that switches into the stacked chain view.
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
    if (mMouseIsOver)
      g.FillRoundRect(PluginColors::MOUSEOVER, mRECT, 4.0f);
    const IRECT icon = mRECT.GetCentredInside(14.0f, 14.0f);
    const IColor c = namtheme::TEXT_DIM;
    for (int i = 0; i < 3; i++)
    {
      const float y = icon.T + i * 5.0f;
      g.DrawRoundRect(c, IRECT(icon.L, y, icon.R, y + 3.5f), 1.0f, nullptr, 1.2f);
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
      if (IControl* pBanner = GetUI()->GetControlWithTag(kCtrlTagChainBanner))
        pBanner->Hide(true);
      pChain->Hide(false);
      GetUI()->Resize(PLUG_WIDTH, (int)kChainViewHeight, GetUI()->GetDrawScale());
    }
  }
};
