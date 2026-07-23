//
// Colors.h
// NeuralAmpModeler-macOS
//
// Created by Steven Atkinson on 12/27/22.
//
// Store the defined colors for the plugin in one place

#ifndef Colors_h
#define Colors_h

#include "IGraphicsStructs.h"

namespace PluginColors
{
// HINT: ARGB
// COLORS!
const iplug::igraphics::IColor OFF_WHITE(255, 243, 246, 249); // Material UI because Heidi said so

// Blue mode (kept for compatibility with upstream references)
const iplug::igraphics::IColor NAM_1(255, 29, 26, 31); // Raisin Black
const iplug::igraphics::IColor NAM_2(255, 80, 133, 232); // Azure
const iplug::igraphics::IColor NAM_3(255, 162, 178, 191); // Cadet Blue Crayola

// Evan Heritage theme colors
const iplug::igraphics::IColor NAM_0(0, 18, 17, 19); // Transparent
// AMPRYX skin ("Nightfall" theme): gold accent on near-black.
// Upstream was Azure (255, 80, 133, 232); Tone Gallery fork was Violet.
const iplug::igraphics::IColor NAM_THEMECOLOR(255, 233, 195, 74); // Gold (#e9c34a)
const iplug::igraphics::IColor NAM_THEMEFONTCOLOR(255, 236, 230, 212); // #ece6d4

// Misc
const iplug::igraphics::IColor MOUSEOVER = NAM_THEMEFONTCOLOR.WithOpacity(0.1);
const iplug::igraphics::IColor HELP_TEXT = iplug::igraphics::COLOR_WHITE;
const iplug::igraphics::IColor HELP_TEXT_MO = iplug::igraphics::COLOR_WHITE.WithOpacity(0.9);
const iplug::igraphics::IColor HELP_TEXT_CLICKED = iplug::igraphics::COLOR_WHITE.WithOpacity(0.8);

}; // namespace PluginColors

#endif /* Colors_h */
