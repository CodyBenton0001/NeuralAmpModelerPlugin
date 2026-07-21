#pragma once

// NAM Tone Gallery fork -- modern dark theme ("Nightfall")
//
// Vector-drawn replacements for the stock bitmap-based controls: knobs,
// switches, meters, file rows, panels and the title. Include from
// NeuralAmpModeler.cpp after NeuralAmpModelerControls.h.
//
// Font IDs used ("Inter-Regular" / "Inter-Bold") are registered in the layout
// function with a fallback to Roboto if the Inter TTFs aren't bundled.

#include "IControls.h"

using namespace iplug;
using namespace igraphics;

namespace namtheme
{
// Palette (ARGB)
const IColor BG(255, 14, 14, 17); // window background
const IColor PANEL(255, 23, 24, 28); // sidebar / favorites strip
const IColor PANEL2(255, 29, 30, 36); // knob card
const IColor CARD(255, 32, 33, 41); // tone cards, file rows, meters
const IColor KNOB_FACE(255, 24, 25, 30);
const IColor LINE(18, 255, 255, 255); // hairline borders (7% white)
const IColor TRACK(20, 255, 255, 255); // knob track arc
const IColor TEXT_MAIN(255, 236, 238, 242);
const IColor TEXT_DIM(255, 139, 142, 152);
const IColor TEXT_FAINT(255, 90, 93, 102);

// Runtime accent (user-selectable, persisted by the gallery header).
inline IColor Accent()
{
return tonegallery::AccentColor();
}

const char* const kFontBody = "Inter-Regular";
const char* const kFontBold = "Inter-Bold";
} // namespace namtheme

// A rounded card panel (background layer).
class ThemedCardControl : public IControl
{
public:
ThemedCardControl(const IRECT& bounds, const IColor& color, float radius, const IColor& border)
: IControl(bounds)
, mColor(color)
, mRadius(radius)
, mBorder(border)
{
mIgnoreMouse = true;
}

void Draw(IGraphics& g) override
{
g.FillRoundRect(mColor, mRECT, mRadius);
g.DrawRoundRect(mBorder, mRECT, mRadius);
}

private:
IColor mColor;
float mRadius;
IColor mBorder;
};

// "NEURAL AMP MODELER" with the middle word in the accent color.
class ThemedTitleControl : public IControl
{
public:
ThemedTitleControl(const IRECT& bounds)
: IControl(bounds)
{
mIgnoreMouse = true;
}

void Draw(IGraphics& g) override
{
const float size = 17.0f;
IText t(size, namtheme::TEXT_MAIN, namtheme::kFontBold, EAlign::Near, EVAlign::Middle);
const char* seg[3] = {"NEURAL ", "AMP ", "MODELER"};
float widths[3];
float total = 0.0f;
for (int i = 0; i < 3; i++)
{
IRECT r;
g.MeasureText(t, seg[i], r);
widths[i] = r.W();
total += widths[i];
}
float x = mRECT.MW() - 0.5f * total;
for (int i = 0; i < 3; i++)
{
t.mFGColor = (i == 1) ? namtheme::Accent() : namtheme::TEXT_MAIN;
g.DrawText(t, seg[i], IRECT(x, mRECT.T, x + widths[i] + 2.0f, mRECT.B));
x += widths[i];
}
}
};

// Arc knob with glowing indicator dot, in the style of modern soft synths.
class ThemedKnobControl : public IVKnobControl
{
public:
ThemedKnobControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style)
: IVKnobControl(bounds, paramIdx, label, style, true)
{
mInnerPointerFrac = 0.62f;
}

void DrawWidget(IGraphics& g) override
{
const float widgetRadius = GetRadius() * 0.85f;
const auto knobRect = mWidgetBounds.GetCentredInside(mWidgetBounds.W(), mWidgetBounds.W());
const float cx = knobRect.MW(), cy = knobRect.MH();
const float angle = mAngle1 + (static_cast<float>(GetValue()) * (mAngle2 - mAngle1));
const IColor accent = GetColor(kX1);

// Track + value arcs
g.DrawArc(namtheme::TRACK, cx, cy, widgetRadius, mAngle1, mAngle2, &mBlend, 3.5f);
if (angle > mAngle1 + 0.5f)
g.DrawArc(accent, cx, cy, widgetRadius, mAngle1, angle, &mBlend, 3.5f);

// Face
const float capR = widgetRadius - 7.0f;
const IRECT capRect(cx - capR, cy - capR, cx + capR, cy + capR);
g.FillEllipse(namtheme::KNOB_FACE, capRect, &mBlend);
g.DrawEllipse(namtheme::LINE, capRect, &mBlend, 1.0f);
if (mMouseIsOver)
g.FillEllipse(PluginColors::MOUSEOVER, capRect, &mBlend);

// Indicator dot with glow
float data[2][2];
RadialPoints(angle, cx, cy, mInnerPointerFrac * widgetRadius, mInnerPointerFrac * widgetRadius, 2, data);
g.PathCircle(data[1][0], data[1][1], 5.5f);
g.PathFill(IPattern::CreateRadialGradient(
data[1][0], data[1][1], 7.0f, {{accent.WithOpacity(0.5f), 0.f}, {COLOR_TRANSPARENT, 1.0f}}),
{}, &mBlend);
g.FillCircle(accent, data[1][0], data[1][1], 2.5f, &mBlend);
}
};

// Pill toggle switch.
class ThemedSwitchControl : public IVSlideSwitchControl
{
public:
ThemedSwitchControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style)
: IVSlideSwitchControl(
bounds, paramIdx, label,
style.WithShowValue(false).WithDrawFrame(false).WithDrawShadows(false).WithLabelOrientation(EOrientation::South))
{
}

void DrawWidget(IGraphics& g) override
{
const bool on = GetValue() > 0.5;
const IRECT pill = mWidgetBounds.GetCentredInside(34.0f, 17.0f);
const IColor accent = GetColor(kX1);

g.FillRoundRect(on ? accent : IColor(26, 255, 255, 255), pill, pill.H() * 0.5f, &mBlend);
if (mMouseIsOver)
g.FillRoundRect(PluginColors::MOUSEOVER, pill, pill.H() * 0.5f, &mBlend);

const float hr = 6.0f;
const float hx = on ? pill.R - 2.5f - hr : pill.L + 2.5f + hr;
g.FillCircle(on ? COLOR_WHITE : IColor(255, 130, 130, 138), hx, pill.MH(), hr, &mBlend);
}
};

// Slim rounded level meter with a gradient fill.
class ThemedMeterControl : public IVPeakAvgMeterControl<>
{
static constexpr float KMeterMin = -70.0f;
static constexpr float KMeterMax = -0.01f;

public:
ThemedMeterControl(const IRECT& bounds, const IVStyle& style)
: IVPeakAvgMeterControl<>(bounds, "", style.WithShowValue(false).WithDrawFrame(false).WithWidgetFrac(0.8),
EDirection::Vertical, {}, 0, KMeterMin, KMeterMax, {})
{
SetPeakSize(1.0f);
}

void OnResize() override
{
SetTargetRECT(MakeRects(mRECT));
mWidgetBounds = mWidgetBounds.GetMidHPadded(5).GetVPadded(10);
MakeTrackRects(mWidgetBounds);
MakeStepRects(mWidgetBounds, mNSteps);
SetDirty(false);
}

void DrawBackground(IGraphics& g, const IRECT& r) override
{
const IRECT card = r.GetMidHPadded(5.0f);
g.FillRoundRect(namtheme::CARD, card, 5.0f);
g.DrawRoundRect(namtheme::LINE, card, 5.0f);
}

void DrawTrackHandle(IGraphics& g, const IRECT& r, int chIdx, bool aboveBaseValue) override
{
if (r.H() > 2)
{
const IColor accent = GetColor(kX1);
g.PathRoundRect(r, 2.0f);
g.PathFill(
IPattern::CreateLinearGradient(r.L, r.B, r.L, r.T, {{accent, 0.0f}, {accent.WithOpacity(0.35f), 1.0f}}), {},
&mBlend);
}
}

void DrawPeak(IGraphics& g, const IRECT& r, int chIdx, bool aboveBaseValue) override
{
g.FillRect(GetColor(kX3), r, &mBlend);
}
};

// The stock file browser with a themed rounded-card look and an accent tint.
class ThemedFileBrowserControl : public NAMFileBrowserControl
{
public:
ThemedFileBrowserControl(const IRECT& bounds, int clearMsgTag, const char* labelStr, const char* fileExtension,
IFileDialogCompletionHandlerFunc ch, const IVStyle& style, const ISVG& loadSVG,
const ISVG& clearSVG, const ISVG& leftSVG, const ISVG& rightSVG, const IBitmap& bitmap,
const ISVG& globeSVG, const char* getButtonLabel, const char* getButtonURL,
const IColor& accent)
: NAMFileBrowserControl(bounds, clearMsgTag, labelStr, fileExtension, ch, style, loadSVG, clearSVG, leftSVG, rightSVG,
bitmap, globeSVG, getButtonLabel, getButtonURL)
, mAccent(accent)
{
}

void Draw(IGraphics& g) override
{
const IRECT card = mRECT.GetPadded(-1.0f);
g.FillRoundRect(namtheme::CARD, card, 9.0f);
g.DrawRoundRect(mAccent.WithOpacity(0.4f), card, 9.0f);
}

private:
IColor mAccent;
};

// Small round button showing the current accent color; click opens a menu of
// accent choices. Selection is applied live (via the plugin's highlight-color
// message for style-based controls) and persisted to theme.json.
class NAMAccentPickerControl : public IControl
{
public:
NAMAccentPickerControl(const IRECT& bounds)
: IControl(bounds)
{
SetTooltip("Accent color");
}

void Draw(IGraphics& g) override
{
if (mMouseIsOver)
g.FillEllipse(PluginColors::MOUSEOVER, mRECT);
const IRECT dot = mRECT.GetCentredInside(11.0f);
g.FillEllipse(namtheme::Accent(), dot);
g.DrawEllipse(IColor(90, 255, 255, 255), dot);
}

void OnMouseDown(float x, float y, const IMouseMod& mod) override
{
mMenu.Clear();
for (int i = 0; i < kNumChoices; i++)
mMenu.AddItem(kChoiceNames[i]);
GetUI()->CreatePopupMenu(*this, mMenu, IRECT(x, y, x, y));
}

void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
{
if (pSelectedMenu == nullptr || pSelectedMenu->GetChosenItem() == nullptr)
return;
const int idx = pSelectedMenu->GetChosenItemIdx();
if (idx < 0 || idx >= kNumChoices)
return;
const IColor c = kChoiceColors[idx];
tonegallery::SetAccentColor(c);
// Update all style-based (IVectorBase) controls live via the plugin's
// existing highlight-color message.
char code[10];
snprintf(code, sizeof(code), "#%02X%02X%02X", c.R, c.G, c.B);
GetDelegate()->SendArbitraryMsgFromUI(kMsgTagHighlightColor, kNoTag, (int)strlen(code) + 1, code);
GetUI()->SetAllControlsDirty();
}

private:
static constexpr int kNumChoices = 6;
static const char* const kChoiceNames[kNumChoices];
static const IColor kChoiceColors[kNumChoices];
IPopupMenu mMenu;
};

inline const char* const NAMAccentPickerControl::kChoiceNames[NAMAccentPickerControl::kNumChoices] = {
"Violet", "Teal", "Azure", "Orange", "Crimson", "Emerald"};
inline const IColor NAMAccentPickerControl::kChoiceColors[NAMAccentPickerControl::kNumChoices] = {
IColor(255, 139, 124, 246), IColor(255, 46, 230, 200), IColor(255, 80, 133, 232),
IColor(255, 240, 160, 74), IColor(255, 232, 90, 90), IColor(255, 88, 214, 141)};

// Compact 1U-style "rack unit" view: photo of the current tone in an LCD-style
// screen, name, gear chip, favorite buttons and an expand control. Shown by
// shrinking the plugin window to kRackViewHeight; hidden by restoring it.
class NAMRackViewControl : public IControl, public tonegallery::INowPlayingListener
{
public:
NAMRackViewControl(const IRECT& bounds, IFileDialogCompletionHandlerFunc loadModelFunc,
IFileDialogCompletionHandlerFunc loadIRFunc)
: IControl(bounds)
, mLoadModelFunc(loadModelFunc)
, mLoadIRFunc(loadIRFunc)
{
}

void SetNowPlaying(const tonegallery::ToneEntry& entry, const std::string& modelPath,
const std::string& irPath) override
{
mEntry = entry;
mHasEntry = true;
// Remember which variation (file) is actually loaded.
std::string vp = !modelPath.empty() ? modelPath : irPath;
if (vp.empty())
vp = !entry.modelPath.empty() ? entry.modelPath : entry.irPath;
mVariation.clear();
try
{
if (!vp.empty())
mVariation = tonegallery::PathToUTF8(tonegallery::UTF8ToPath(vp).stem());
}
catch (const std::exception&)
{
}
SetDirty(false);
}

void Draw(IGraphics& g) override
{
const IColor accent = namtheme::Accent();
const IRECT face = FaceRect();

// If the host refused to shrink the window, blank out everything under
// the rack face so the full UI doesn't show through.
if (mRECT.H() > kRackViewHeight + 1.0f)
g.FillRect(namtheme::BG, mRECT.GetReducedFromTop(kRackViewHeight));

// Faceplate with a subtle vertical sheen
g.PathRect(face);
g.PathFill(IPattern::CreateLinearGradient(
face.L, face.T, face.L, face.B,
{{IColor(255, 30, 31, 36), 0.0f}, {IColor(255, 20, 21, 25), 0.35f}, {IColor(255, 16, 17, 20), 1.0f}}));
g.FillRect(IColor(30, 255, 255, 255), face.GetFromTop(1.0f));
g.FillRect(COLOR_BLACK.WithOpacity(0.5f), face.GetFromBottom(2.0f));

// Rack ears + screws
for (int side = 0; side < 2; side++)
{
const IRECT ear = side == 0 ? face.GetFromLeft(26.0f) : face.GetFromRight(26.0f);
g.FillRect(IColor(255, 24, 25, 29), ear);
g.FillRect(IColor(20, 255, 255, 255), side == 0 ? ear.GetFromRight(1.0f) : ear.GetFromLeft(1.0f));
for (int screw = 0; screw < 2; screw++)
{
const IRECT sr = (screw == 0 ? ear.GetFromTop(30.0f) : ear.GetFromBottom(30.0f)).GetCentredInside(11.0f);
g.FillEllipse(IColor(255, 48, 50, 58), sr);
g.DrawEllipse(COLOR_BLACK.WithOpacity(0.6f), sr);
g.DrawLine(IColor(255, 90, 93, 104), sr.L + 2.0f, sr.MH(), sr.R - 2.0f, sr.MH(), nullptr, 1.4f);
}
}

// Power LED + branding
const IRECT brand = face.GetReducedFromLeft(38.0f).GetFromLeft(120.0f);
const IRECT led = IRECT(brand.L, face.MH() - 22.0f, brand.L + 10.0f, face.MH() - 12.0f);
g.PathCircle(led.MW(), led.MH(), 7.0f);
g.PathFill(IPattern::CreateRadialGradient(
led.MW(), led.MH(), 9.0f, {{accent.WithOpacity(0.55f), 0.0f}, {COLOR_TRANSPARENT, 1.0f}}));
g.FillEllipse(accent, led.GetCentredInside(7.0f));
const IText brandText(19.0f, namtheme::TEXT_MAIN, "Inter-Bold", EAlign::Near, EVAlign::Middle);
g.DrawText(brandText, "NAM", IRECT(brand.L + 16.0f, face.MH() - 26.0f, brand.R, face.MH() - 6.0f));
const IText subText(7.5f, namtheme::TEXT_FAINT, "Inter-Bold", EAlign::Near, EVAlign::Middle);
g.DrawText(subText, "TONE RACK UNIT", IRECT(brand.L, face.MH() - 2.0f, brand.R, face.MH() + 8.0f));

// LCD screen with the tone photo
const IRECT screen(face.L + 170.0f, face.T + 16.0f, face.L + 340.0f, face.B - 16.0f);
g.FillRoundRect(COLOR_BLACK, screen.GetPadded(4.0f), 6.0f);
g.DrawRoundRect(IColor(40, 255, 255, 255), screen.GetPadded(4.0f), 6.0f);
IBitmap* pBitmap = mHasEntry ? GetImage(mEntry.imagePath) : nullptr;
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
}
else
{
g.FillRect(IColor(255, 10, 10, 13), screen);
const IText idleText(11.0f, accent.WithOpacity(0.7f), "Inter-Bold", EAlign::Center, EVAlign::Middle);
g.DrawText(idleText, "NO TONE LOADED", screen);
}

// Tone name + chip
const IRECT info(screen.R + 18.0f, face.T + 20.0f, face.R - 90.0f, face.B - 20.0f);
if (mHasEntry)
{
const IText nameText(15.0f, COLOR_WHITE, "Inter-Bold", EAlign::Near, EVAlign::Top);
float y = info.T + 6.0f;
for (const auto& line : tonegallery::WrapLines(mEntry.name, 28, 2))
{
g.DrawText(nameText, line.c_str(), IRECT(info.L, y, info.R, y + 18.0f));
y += 18.0f;
}
const IColor gearColor = tonegallery::GearTypeColor(mEntry.gearType);
const char* gearLabel = tonegallery::GearTypeChipLabel(mEntry.gearType);
const float gw = 14.0f + 5.4f * (float)strlen(gearLabel);
const IRECT chip(info.L, y + 6.0f, info.L + gw, y + 22.0f);
g.FillRoundRect(gearColor, chip, 5.0f);
const IText chipText(8.0f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
g.DrawText(chipText, gearLabel, chip);
if (!mEntry.author.empty())
{
const IText authorText(9.0f, namtheme::TEXT_DIM, "Inter-Regular", EAlign::Near, EVAlign::Middle);
g.DrawText(authorText, ("by " + mEntry.author).c_str(), IRECT(info.L + gw + 8.0f, y + 6.0f, info.R, y + 22.0f));
}
// Loaded variation (model/IR file)
if (!mVariation.empty())
{
const IText varText(8.0f, namtheme::TEXT_FAINT, "Inter-Regular", EAlign::Near, EVAlign::Middle);
g.DrawText(varText, ("variation: " + tonegallery::Ellipsize(mVariation, 40)).c_str(),
IRECT(info.L, y + 26.0f, info.R, y + 38.0f));
}
}
else
{
const IText hintText(10.0f, namtheme::TEXT_DIM, "Inter-Regular", EAlign::Near, EVAlign::Middle);
g.DrawText(hintText, "Load a tone, or tap 1 / 2 / 3", info.GetFromTop(40.0f));
}

// Favorite quick buttons
for (int i = 0; i < 3; i++)
{
const IRECT fav = FavRect(i);
const bool over = (i == mMouseOverFav);
g.FillEllipse(over ? accent.WithOpacity(0.35f) : IColor(255, 34, 35, 42), fav);
g.DrawEllipse(accent.WithOpacity(over ? 1.0f : 0.4f), fav);
const IText numText(9.0f, over ? COLOR_WHITE : namtheme::TEXT_DIM, "Inter-Bold", EAlign::Center, EVAlign::Middle);
const char num[2] = {(char)('1' + i), 0};
g.DrawText(numText, num, fav);
}

// Expand button
const IRECT expand = ExpandRect();
if (mMouseOverExpand)
g.FillRoundRect(PluginColors::MOUSEOVER, expand, 5.0f);
g.DrawRoundRect(namtheme::TEXT_DIM, expand.GetCentredInside(13.0f), 2.0f, nullptr, 1.4f);
const IRECT inner = expand.GetCentredInside(13.0f);
g.DrawLine(namtheme::TEXT_DIM, inner.L + 3.0f, inner.MH(), inner.R - 3.0f, inner.MH(), nullptr, 1.2f);
}

void OnMouseDown(float x, float y, const IMouseMod& mod) override
{
if (ExpandRect().Contains(x, y))
{
PLUG()->mToneRackMode = false;
Hide(true);
GetUI()->Resize(PLUG_WIDTH, PLUG_HEIGHT, GetUI()->GetDrawScale());
return;
}
for (int i = 0; i < 3; i++)
{
if (FavRect(i).Contains(x, y))
{
const auto slots = tonegallery::LoadFavorites();
if (i < (int)slots.size() && !slots[i].empty())
{
try
{
const std::filesystem::path dir = tonegallery::GetToneLibraryRoot() / tonegallery::UTF8ToPath(slots[i]);
tonegallery::ToneEntry entry;
if (std::filesystem::exists(dir) && tonegallery::ScanToneFolder(dir, entry))
{
tonegallery::LoadToneEntryFiles(entry, mLoadModelFunc, mLoadIRFunc);
tonegallery::NotifyNowPlaying(GetUI(), entry, entry.modelPath, entry.irPath);
}
}
catch (const std::exception&)
{
}
}
return;
}
}
}

void OnMouseOver(float x, float y, const IMouseMod& mod) override
{
IControl::OnMouseOver(x, y, mod);
const bool overExpand = ExpandRect().Contains(x, y);
int overFav = -1;
for (int i = 0; i < 3; i++)
if (FavRect(i).Contains(x, y))
overFav = i;
if (overExpand != mMouseOverExpand || overFav != mMouseOverFav)
{
mMouseOverExpand = overExpand;
mMouseOverFav = overFav;
SetDirty(false);
}
}

void OnMouseOut() override
{
IControl::OnMouseOut();
mMouseOverExpand = false;
mMouseOverFav = -1;
SetDirty(false);
}

private:
IRECT FaceRect() const { return mRECT.GetFromTop(kRackViewHeight); }
IRECT ExpandRect() const { return FaceRect().GetFromTRHC(56.0f, 46.0f).GetCentredInside(24.0f); }

IRECT FavRect(int i) const
{
const IRECT face = FaceRect();
const float size = 22.0f;
const float x = face.R - 190.0f + i * 28.0f;
const float top = face.B - 62.0f;
return IRECT(x, top, x + size, top + size);
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

tonegallery::ToneEntry mEntry;
bool mHasEntry = false;
std::string mVariation;
bool mMouseOverExpand = false;
int mMouseOverFav = -1;
std::map<std::string, IBitmap> mImageCache;
IFileDialogCompletionHandlerFunc mLoadModelFunc;
IFileDialogCompletionHandlerFunc mLoadIRFunc;
};

// Titlebar button that cycles the UI scale (100% -> 125% -> ... -> 200%).
// At 200% the standalone app fills most of a 1080p+ screen. Right-click
// resets to 100%. The scale is remembered in theme.json.
class NAMZoomButtonControl : public IControl
{
public:
NAMZoomButtonControl(const IRECT& bounds)
: IControl(bounds)
{
SetTooltip("Window size: click to grow, right-click for 100%");
}

void Draw(IGraphics& g) override
{
if (mMouseIsOver)
g.FillRoundRect(PluginColors::MOUSEOVER, mRECT, 4.0f);
const IColor c = mMouseIsOver ? COLOR_WHITE : namtheme::TEXT_DIM;
const IRECT icon = mRECT.GetCentredInside(13.0f);
// Two outward arrows (expand)
g.DrawLine(c, icon.L, icon.T + 4.0f, icon.L, icon.T, nullptr, 1.4f);
g.DrawLine(c, icon.L, icon.T, icon.L + 4.0f, icon.T, nullptr, 1.4f);
g.DrawLine(c, icon.L, icon.T, icon.L + 5.0f, icon.T + 5.0f, nullptr, 1.4f);
g.DrawLine(c, icon.R - 4.0f, icon.B, icon.R, icon.B, nullptr, 1.4f);
g.DrawLine(c, icon.R, icon.B - 4.0f, icon.R, icon.B, nullptr, 1.4f);
g.DrawLine(c, icon.R - 5.0f, icon.B - 5.0f, icon.R, icon.B, nullptr, 1.4f);
}

void OnMouseDown(float x, float y, const IMouseMod& mod) override
{
if (mod.R)
{
ApplyScale(1.0);
return;
}
static const double kScales[] = {1.0, 1.25, 1.5, 1.75, 2.0};
const double cur = GetUI()->GetDrawScale();
int idx = 0;
double best = 1e9;
for (int i = 0; i < 5; i++)
{
const double d = std::abs(kScales[i] - cur);
if (d < best)
{
best = d;
idx = i;
}
}
ApplyScale(kScales[(idx + 1) % 5]);
}

private:
void ApplyScale(double s)
{
GetUI()->Resize(GetUI()->Width(), GetUI()->Height(), (float)s);
tonegallery::SaveUIScale(s);
}
};

// Titlebar button that switches into rack view.
class NAMRackButtonControl : public IControl
{
public:
NAMRackButtonControl(const IRECT& bounds)
: IControl(bounds)
{
SetTooltip("Rack view");
}

void Draw(IGraphics& g) override
{
if (mMouseIsOver)
g.FillRoundRect(PluginColors::MOUSEOVER, mRECT, 4.0f);
const IRECT icon = mRECT.GetCentredInside(14.0f, 12.0f);
const IColor c = namtheme::TEXT_DIM;
g.DrawRoundRect(c, icon, 2.0f, nullptr, 1.4f);
g.DrawLine(c, icon.L + 2.0f, icon.MH(), icon.R - 2.0f, icon.MH(), nullptr, 1.2f);
}

void OnMouseDown(float x, float y, const IMouseMod& mod) override
{
if (IControl* pRack = GetUI()->GetControlWithTag(kCtrlTagRackView))
{
PLUG()->EndChainKnobEdit();
PLUG()->mToneRackMode = true;
PLUG()->mToneChainMode = false;
if (IControl* pChain = GetUI()->GetControlWithTag(kCtrlTagChainView))
pChain->Hide(true);
pRack->Hide(false);
GetUI()->Resize(PLUG_WIDTH, (int)kRackViewHeight, GetUI()->GetDrawScale());
}
}
};
