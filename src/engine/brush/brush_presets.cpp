/**
 * =========================================================================================
 * @file src/engine/brush/brush_presets.cpp
 * @brief Folio Stylus & Expressive Multi-Coat Brush Recipe Library
 * =========================================================================================
 */

#include "engine/brush/brush_composer.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace Folio {

namespace {
constexpr float kPi = 3.14159265358979323846f;
} // namespace

std::vector<BrushRecipe> BrushComposer::CreateFolioPresets() {
    std::vector<BrushRecipe> presets;
    presets.reserve(13);

    // =========================================================================
    // 0. Standard Solid (Baseline)
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Standard Solid";
        r.description = "Baseline single-coat smooth capsule ink.";
        r.AddCoat("Solid Core", CoatRole::BasePigment, 1.0f);
        presets.push_back(r);
    }

    // =========================================================================
    // 1. Graffiti Spray (cahier: graffiti.brushfamily)
    // Opaque halo + black outline + hue-mirrored core
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Graffiti Spray";
        r.description = "Google Graffiti: base-color halo, thick black outline and a core whose hue shifts smoothly along the stroke.";

        r.AddCoat("Outer Halo", CoatRole::UnderlayShadow, 1.30f)
         .BindNoiseToWidth(0.95f, 1.05f, 0.2f, 3u);

        auto& outline = r.AddCoat("Black Outline", CoatRole::UnderlayShadow, 1.05f);
        outline.colorMode = CoatColorMode::FixedColor;
        outline.fixedColor = ColorRgba(12, 12, 16, 255);
        outline.BindNoiseToWidth(0.95f, 1.10f, 0.2f, 3u);

        r.AddCoat("Hue-Shifting Core", CoatRole::BasePigment, 0.55f)
         .BindNoiseToWidth(0.8f, 1.2f, 0.2f, 3u)
         .BindPeriodic(ModulatorSource::ArcLength, ModulatorTarget::HueShiftDeg, ModulatorWaveform::MirrorCycle, 10.0f, 115.0f);

        presets.push_back(r);
    }

    // =========================================================================
    // 2. Flag Banner (cahier: flag_banner.brushfamily)
    // Flat-based rainbow triangles hanging downward from a string
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Flag Banner";
        r.description = "Google Flag Banner: rainbow triangular flags whose flat top sits on the string and hang downward.";

        auto& flags = r.AddParticleCoat("Pennant Flags", 0.95f, 1.20f, 1.25f, ParticleOrientation::NormalHangDown);
        flags.role = CoatRole::Highlight;
        flags.triangleFan = true;
        flags.startSkipBrushSizes = 0.7f;
        flags.endSkipBrushSizes = 0.7f;
        flags.Bind(ModulatorSource::ParticleIndex, ModulatorTarget::HueShiftDeg, ModulatorWaveform::Linear, 0.0f, 60.0f, 1.0f);

        auto& str = r.AddCoat("String", CoatRole::Structure, 0.10f);
        str.colorMode = CoatColorMode::FixedColor;
        str.fixedColor = ColorRgba(15, 15, 18, 255);

        presets.push_back(r);
    }

    // =========================================================================
    // 3. Holiday Lights (cahier: holiday_lights.brushfamily)
    // 4 coats: double wire (+/-0.25 offset), sockets, alternating blinking bulbs
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Holiday Lights";
        r.description = "Google Holiday Lights: twisted wires with sockets and bulbs that blink live while drawing.";
        r.isAnimated = true;
        r.settlingDurationSec = 0.65f;

        for (int w = 0; w < 2; ++w) {
            auto& wire = r.AddCoat((w == 0) ? "Twisted Wire A" : "Twisted Wire B", CoatRole::Structure, 0.12f);
            wire.colorMode = CoatColorMode::FixedColor;
            wire.fixedColor = (w == 0) ? ColorRgba(35, 45, 38, 255) : ColorRgba(25, 35, 28, 255);
            wire.BindPeriodic(
                ModulatorSource::ArcLength,
                ModulatorTarget::LateralOffset,
                ModulatorWaveform::SineWave,
                5.0f,
                0.25f,
                0.0f,
                (w == 0) ? 0.0f : kPi
            );
        }

        auto& sockets = r.AddParticleCoat("Bulb Sockets", 0.32f, 2.0f, 0.40f, ParticleOrientation::PerpendicularAlternate);
        sockets.role = CoatRole::Structure;
        sockets.startSkipBrushSizes = 1.0f;
        sockets.lateralOffsetRatio = 0.20f;
        sockets.alternateLateralByParticle = true;
        sockets.radiusEndRatio = 0.90f;
        sockets.colorMode = CoatColorMode::FixedColor;
        sockets.fixedColor = ColorRgba(20, 28, 22, 255);

        auto& bulbs = r.AddParticleCoat("Glowing Bulbs", 0.40f, 2.0f, 0.80f, ParticleOrientation::PerpendicularAlternate);
        bulbs.role = CoatRole::Highlight;
        bulbs.blendMode = CoatBlendMode::Add;
        bulbs.startSkipBrushSizes = 1.0f;
        bulbs.lateralOffsetRatio = 0.45f;
        bulbs.alternateLateralByParticle = true;
        bulbs.radiusEndRatio = 0.65f;
        bulbs.Bind(ModulatorSource::ParticleIndex, ModulatorTarget::HueShiftDeg, ModulatorWaveform::Linear, 0.0f, 360.0f, 0.20f)
             .Bind(ModulatorSource::TimeSinceInput, ModulatorTarget::LightnessShift, ModulatorWaveform::BlinkPulseClamped, 0.10f, 0.42f, 1.666f);

        presets.push_back(r);
    }

    // =========================================================================
    // 4. Twisted Yarn (cahier: twisted_yarn.brushfamily)
    // Two bumpy strands weaving over/under each other
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Twisted Yarn";
        r.description = "Google Twisted Yarn: two solid-color strands built from stacked round bumps that weave over and under each other.";
        auto& y = r.AddCoat("Yarn Strands", CoatRole::BasePigment, 0.55f);
        y.emissionMode = CoatEmissionMode::YarnStrandBumps;
        y.yarnPeriodBrushSizes = 3.0f;
        y.yarnAmplitudeRatio = 0.38f;
        y.hueOffsetDeg = 180.0f; // second strand = opposite hue
        presets.push_back(r);
    }

    // =========================================================================
    // 5. Wet Paint (cahier: wet_paint.brushfamily)
    // Gravity drips + main paint ribbon
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Wet Paint (Gravity Drips)";
        r.description = "Google Wet Paint: round drops form inside the stroke and slowly slide down over 1s, then stop.";
        r.isAnimated = true;
        r.settlingDurationSec = 1.05f;

        auto& drips = r.AddParticleCoat("Gravity Drips", 0.16f, 0.45f, 0.05f, ParticleOrientation::GravityDown);
        drips.role = CoatRole::UnderlayShadow;
        drips.radiusEndRatio = 1.55f;
        drips.Bind(
            ModulatorSource::TimeSinceInput,
            ModulatorTarget::ParticleLength,
            ModulatorWaveform::EaseOutDripSynced,
            1.0f,
            30.0f,
            1.0f,
            0.0f,
            0x0004071eu
        );

        BrushModulator off;
        off.source = ModulatorSource::TimeSinceInput;
        off.waveform = ModulatorWaveform::EaseOutDripSynced;
        off.target = ModulatorTarget::WorldOffsetY;
        off.frequency = 1.0f;
        off.amplitudeRatio = 0.85f;
        off.seed = 0x0004071eu;
        drips.modulators.push_back(off);

        drips.BindPeriodic(
            ModulatorSource::ArcLength,
            ModulatorTarget::RadiusScale,
            ModulatorWaveform::ValueNoise1D,
            1.40f,
            0.36f,
            1.10f,
            0.0f,
            0x000b79dfu
        );

        r.AddCoat("Paint Base Ribbon", CoatRole::BasePigment, 0.92f);

        presets.push_back(r);
    }

    // =========================================================================
    // 6. 3D Drop Shadow (cahier: shadow.brushfamily)
    // Offset darkened shadow coat beneath foreground stroke
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "3D Drop Shadow";
        r.description = "Google Shadow: offset darkened copy of the base color beneath the stroke.";

        auto& shadow = r.AddUnderlayCoat("Cast Shadow", 1.05f, 0.45f, 0.65f, 0.65f, -0.22f);
        shadow.colorMode = CoatColorMode::InheritBase;

        r.AddCoat("Foreground Stroke", CoatRole::BasePigment, 1.0f);

        presets.push_back(r);
    }

    // =========================================================================
    // 7. Calligraphy Chisel (cahier: calligraphy.brushfamily)
    // Direction-sensitive nib
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Calligraphy Chisel";
        r.description = "Google Calligraphy: Direction-sensitive chisel nib with thick downstrokes and thin cross-strokes.";

        r.AddCoat("Chisel Nib", CoatRole::BasePigment, 0.50f)
         .BindDirectionToWidth(1.15f, 0.85f, -0.785398f);

        presets.push_back(r);
    }

    // =========================================================================
    // 8-11. Standard Pens: Ballpoint, Marker, Fountain, Highlighter
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Ballpoint Pen";
        r.description = "Thin, crisp, near-constant-width line.";
        r.usePressure = true;
        r.AddCoat("Ink", CoatRole::BasePigment, 0.40f);
        presets.push_back(r);
    }
    {
        BrushRecipe r;
        r.name = "Marker";
        r.description = "Wide opaque felt-tip marker with constant width.";
        r.usePressure = false;
        r.AddCoat("Felt", CoatRole::BasePigment, 1.10f);
        presets.push_back(r);
    }
    {
        BrushRecipe r;
        r.name = "Fountain Pen";
        r.description = "Pressure and speed sensitive line: thick when slow, thin when fast.";
        r.usePressure = true;
        r.useVelocity = true;
        r.AddCoat("Nib", CoatRole::BasePigment, 0.80f);
        presets.push_back(r);
    }
    {
        BrushRecipe r;
        r.name = "Highlighter";
        r.description = "Wide translucent stroke; overlaps never darken (set-union), joins are seamless.";
        r.usePressure = false;
        r.opacity = 0.38f;
        r.unionOverlap = true;
        auto& highlight = r.AddCoat("Highlight", CoatRole::BasePigment, 1.9f);
        highlight.blendMode = CoatBlendMode::MaxUnion;
        presets.push_back(r);
    }

    // =========================================================================
    // 12. Shading Pencil (cahier: shading_pencil.brushfamily)
    // Grainy graphite body (pressure -> opacity, tilt -> width) + loose specks
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Shading Pencil";
        r.description = "Google Shading Pencil: paper-grain graphite; light pressure is faint, tilt widens it to shade, specks add texture.";
        r.usePressure = false;
        r.useTilt = true;
        r.unionOverlap = true;
        r.SetTextureGrain(0.85f, 2.4f);

        auto& body = r.AddCoat("Graphite Body", CoatRole::BasePigment, 0.55f);
        body.blendMode = CoatBlendMode::MaxUnion;
        body.BindPressureToOpacity(0.12f, 1.0f)
            .Bind(ModulatorSource::Tilt, ModulatorTarget::RadiusScale, ModulatorWaveform::Linear, 1.0f, 2.0f, 0.83f);

        auto& specks = r.AddParticleCoat("Graphite Specks", 0.10f, 0.22f, 0.0f);
        specks.particleLengthMm = 0.1f;
        specks.opacityScale = 0.55f;

        BrushModulator lat;
        lat.source = ModulatorSource::ParticleIndex;
        lat.waveform = ModulatorWaveform::HashRandom;
        lat.target = ModulatorTarget::LateralOffset;
        lat.amplitudeRatio = 0.9f;
        lat.bias = -0.5f;
        lat.seed = 31u;
        specks.modulators.push_back(lat);

        specks.Bind(ModulatorSource::ParticleIndex, ModulatorTarget::RadiusScale, ModulatorWaveform::HashRandom, 0.4f, 1.2f, 0.5f, 0.0f, 57u);

        presets.push_back(r);
    }

    // =========================================================================
    // 13. Music Staff (cahier: music.brushfamily)
    // Staff lines + treble clef/notes + bar lines
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Music Staff";
        r.description = "Google Music: five staff lines carrying a treble clef, real notes (head, stem, flags) and bar lines.";

        const float staffOffsets[5] = { -0.70f, -0.35f, 0.0f, 0.35f, 0.70f };
        for (int i = 0; i < 5; ++i) {
            auto& line = r.AddCoat(std::string("Staff Line ") + std::to_string(i + 1), CoatRole::Structure, 0.05f);
            line.lateralOffsetRatio = staffOffsets[i];
        }

        auto& notes = r.AddParticleCoat("Clef & Notes", 1.0f, 1.20f, 0.0f, ParticleOrientation::FollowTangent);
        notes.role = CoatRole::BasePigment;
        notes.emissionMode = CoatEmissionMode::MusicalNotes;
        notes.Bind(ModulatorSource::ArcLength, ModulatorTarget::OpacityScale, ModulatorWaveform::SineWave, 1.0f, 0.0f, 0.5f, 0.0f, 4242u);

        auto& bar = r.AddParticleCoat("Bar Lines", 0.05f, 4.0f, 1.40f, ParticleOrientation::PerpendicularAlternate);
        bar.role = CoatRole::Structure;
        bar.lateralOffsetRatio = 0.0f;
        bar.startSkipBrushSizes = 2.5f;

        presets.push_back(r);
    }

    // =========================================================================
    // 14. Jelly Wobble (cahier: jelly_wobble.brushfamily)
    // Single color, smoothly undulating width
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Jelly Wobble";
        r.description = "Google Jelly Wobble: one unified color; width undulates smoothly (0.75x-1.25x) while drawing and settles after pen lift.";
        r.isAnimated = true;
        r.settlingDurationSec = 0.95f;
        r.AddCoat("Jelly", CoatRole::BasePigment, 1.0f)
         .Bind(ModulatorSource::TimeSinceInput, ModulatorTarget::RadiusScale, ModulatorWaveform::TravelingWaveJelly, 1.0f, 0.25f, 0.5f);
        presets.push_back(r);
    }

    // =========================================================================
    // 15. Groovy Disco (cahier: groovy.brushfamily)
    // 4 stacked retro coats with stepped offsets and 1970s hue shifts
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Groovy (1970s Disco)";
        r.description = "Google Groovy: 4 stacked retro ribbons with stepped isometric offsets and 1970s hue shifts.";

        const struct { const char* name; CoatRole role; float ox; float oy; float h; float l; float w; } kGroovy[4] = {
            { "Deep Outline", CoatRole::UnderlayShadow, 0.60f, 1.00f, -35.0f, -0.65f, 1.10f },
            { "Mid Tone",     CoatRole::UnderlayShadow, 0.30f, 0.50f, 180.0f, -0.40f, 1.00f },
            { "Underline",    CoatRole::UnderlayShadow, 0.15f, 0.25f,  90.0f, -0.15f, 0.90f },
            { "Front Stripe", CoatRole::BasePigment,    0.00f, 0.00f,   0.0f,   0.00f, 0.60f }
        };

        for (const auto& layer : kGroovy) {
            auto& c = r.AddUnderlayCoat(layer.name, layer.w, layer.ox, layer.oy, 1.0f, layer.l);
            c.role = layer.role;
            c.hueOffsetDeg = layer.h;
        }
        presets.push_back(r);
    }

    // =========================================================================
    // 16. Lace Ribbon (cahier: lace.brushfamily)
    // 4 filigree strands oscillating in quad phase
    // =========================================================================
    {
        BrushRecipe r;
        r.name = "Lace Ribbon";
        r.description = "Google Lace: 4 interlaced delicate filigree strands oscillating in quad phase.";

        const struct { float phase; float l; } kLace[4] = {
            { 0.0f,         0.0f  },
            { kPi * 0.5f,   0.15f },
            { kPi,         -0.10f },
            { kPi * 1.5f,   0.20f }
        };

        for (int i = 0; i < 4; ++i) {
            auto& c = r.AddCoat(std::string("Lace Strand ") + std::to_string(i + 1), CoatRole::BasePigment, 0.20f);
            c.opacityScale = 0.85f;
            c.lightnessOffset = kLace[i].l;
            c.BindPeriodic(
                ModulatorSource::ArcLength,
                ModulatorTarget::LateralOffset,
                ModulatorWaveform::SineWave,
                3.0f,
                0.35f,
                0.0f,
                kLace[i].phase
            );
        }
        presets.push_back(r);
    }

    return presets;
}

BrushRecipe BrushComposer::GetPresetRecipe(BrushPreset preset) {
    auto allPresets = CreateFolioPresets();

    auto findByName = [&](std::string_view name) -> BrushRecipe {
        for (const auto& p : allPresets) {
            if (p.name == name) return p;
        }
        return allPresets.empty() ? BrushRecipe{} : allPresets.front();
    };

    switch (preset) {
        case BrushPreset::BallpointPen:  return findByName("Ballpoint Pen");
        case BrushPreset::Marker:        return findByName("Marker");
        case BrushPreset::FountainPen:   return findByName("Fountain Pen");
        case BrushPreset::Highlighter:   return findByName("Highlighter");
        case BrushPreset::Pencil:        return findByName("Shading Pencil");
        case BrushPreset::WetPaint:      return findByName("Wet Paint (Gravity Drips)");
        case BrushPreset::Graffiti:      return findByName("Graffiti Spray");
        case BrushPreset::HolidayLights: return findByName("Holiday Lights");
        case BrushPreset::TwistedYarn:   return findByName("Twisted Yarn");
        case BrushPreset::MusicStaff:    return findByName("Music Staff");
        case BrushPreset::Custom: {
            BrushRecipe r;
            r.name = "Custom";
            r.AddCoat("Base Coat", CoatRole::BasePigment, 1.0f);
            return r;
        }
    }
    return findByName("Ballpoint Pen");
}

BrushRecipe BrushComposer::CreateBallpoint() { return GetPresetRecipe(BrushPreset::BallpointPen); }
BrushRecipe BrushComposer::CreateMarker() { return GetPresetRecipe(BrushPreset::Marker); }
BrushRecipe BrushComposer::CreateFountain() { return GetPresetRecipe(BrushPreset::FountainPen); }
BrushRecipe BrushComposer::CreateHighlighter() { return GetPresetRecipe(BrushPreset::Highlighter); }
BrushRecipe BrushComposer::CreatePencil() { return GetPresetRecipe(BrushPreset::Pencil); }

} // namespace Folio
