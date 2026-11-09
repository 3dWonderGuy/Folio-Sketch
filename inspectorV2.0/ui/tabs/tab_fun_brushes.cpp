/**
 * =========================================================================================
 * @file inspectorV2.0/ui/tabs/tab_fun_brushes.cpp
 * @brief Workspace 6: Universal Multi-Coat Brush Studio & Cahier Presets
 * =========================================================================================
 */

#include "inspectorV2.0/ui/inspector_gui.hpp"
#include "inspectorV2.0/layer_manager.hpp"
#include "folio_sketch/engine/brush_composer.hpp"
#include <imgui.h>
#include <vector>
#include <string>

namespace Folio::V2 {

void RenderTabFunBrushes(InspectorGui& gui, LayerManager& layerManager, bool& needsRedraw) {
    if (ImGui::BeginTabItem("6. Brush Studio (Cahier)")) {
        if (gui.currentWorkspaceTab != 5) {
            gui.currentWorkspaceTab = 5;
            needsRedraw = true;
        }

        static std::vector<Folio::BrushRecipe> s_presets = Folio::BrushComposer::CreateCahierPresets();
        auto& activeRecipe = layerManager.GetActiveRecipe();
        int activeIdx = layerManager.GetActivePresetIndex();

        if (activeRecipe.empty() && !s_presets.empty()) {
            activeRecipe = s_presets[0];
            layerManager.SetActiveRecipe(activeRecipe);
        }

        auto& style = layerManager.GetActiveStyle();
        bool recipeModified = false;

        ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "UNIVERSAL MULTI-COAT BRUSH COMPOSER");
        ImGui::TextDisabled("Procedural multi-coat & signal modulators compiled to 240Hz GPU instances.");

        // Quick action toolbar
        if (ImGui::Button("Inject Scribble")) {
            layerManager.InjectTestScribble();
            needsRedraw = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Canvas (C)")) {
            layerManager.ClearDocument();
            needsRedraw = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Multi-Stroke Mode", &gui.funBrushMultiStroke)) {
            layerManager.SetTestbedMode(!gui.funBrushMultiStroke);
            needsRedraw = true;
        }

        ImGui::SeparatorText("Cahier Preset Gallery");

        // 3-column grid of preset buttons
        ImGui::Columns(3, "PresetGrid", false);
        for (int i = 0; i < static_cast<int>(s_presets.size()); ++i) {
            bool isSelected = (activeIdx == i);
            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.45f, 0.85f, 1.0f));
            }

            if (ImGui::Button(s_presets[i].name.c_str(), ImVec2(-1.0f, 28.0f))) {
                layerManager.SetActivePresetIndex(i);
                layerManager.SetActiveRecipe(s_presets[i]);
                recipeModified = true;
            }

            if (isSelected) {
                ImGui::PopStyleColor();
            }
            ImGui::NextColumn();
        }
        ImGui::Columns(1);

        // Active Preset Description Banner
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.13f, 0.17f, 1.0f));
        ImGui::BeginChild("PresetDesc", ImVec2(0.0f, 54.0f), true, ImGuiWindowFlags_None);
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.2f, 1.0f), "%s (%zu coats)",
                           activeRecipe.name.c_str(), activeRecipe.coats.size());
        ImGui::TextWrapped("%s", activeRecipe.description.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::SeparatorText("Quick Macro Controls");
        recipeModified |= ImGui::SliderFloat("Stroke Base Width", &style.baseWidth, 1.0f, 50.0f, "%.1f px");

        float col[3] = {
            static_cast<float>(style.color.r) / 255.0f,
            static_cast<float>(style.color.g) / 255.0f,
            static_cast<float>(style.color.b) / 255.0f
        };
        if (ImGui::ColorEdit3("Base Pigment Color", col)) {
            style.color = Folio::ColorRgba(
                static_cast<uint8_t>(col[0] * 255.0f),
                static_cast<uint8_t>(col[1] * 255.0f),
                static_cast<uint8_t>(col[2] * 255.0f),
                255
            );
            recipeModified = true;
        }

        static float s_spreadMultiplier = 1.0f;
        if (ImGui::SliderFloat("Effect Spread / Amplitude", &s_spreadMultiplier, 0.2f, 3.0f, "%.2fx")) {
            // Scale all lateral offsets & amplitudes in the active recipe
            for (auto& coat : activeRecipe.coats) {
                coat.lateralOffsetMm *= s_spreadMultiplier;
                for (auto& mod : coat.modulators) {
                    if (mod.target == Folio::ModulatorTarget::LateralOffset ||
                        mod.target == Folio::ModulatorTarget::WorldOffsetX ||
                        mod.target == Folio::ModulatorTarget::WorldOffsetY) {
                        mod.amplitude *= s_spreadMultiplier;
                    }
                }
            }
            recipeModified = true;
            s_spreadMultiplier = 1.0f; // reset slider to 1.0 anchor
        }

        static float s_freqMultiplier = 1.0f;
        if (ImGui::SliderFloat("Pattern Spacing / Wavelength", &s_freqMultiplier, 0.25f, 4.0f, "%.2fx")) {
            for (auto& coat : activeRecipe.coats) {
                coat.particleGapMm *= s_freqMultiplier;
                for (auto& mod : coat.modulators) {
                    if (s_freqMultiplier > 0.01f) {
                        mod.frequency /= s_freqMultiplier;
                    }
                }
            }
            recipeModified = true;
            s_freqMultiplier = 1.0f;
        }

        ImGui::Checkbox("Live Wobble Animation", &activeRecipe.isAnimated);

        ImGui::SeparatorText("Pen Behavior (applies to every brush)");
        recipeModified |= ImGui::Checkbox("Pressure", &activeRecipe.usePressure);
        ImGui::SameLine();
        recipeModified |= ImGui::Checkbox("Velocity", &activeRecipe.useVelocity);
        ImGui::SameLine();
        recipeModified |= ImGui::Checkbox("Tilt", &activeRecipe.useTilt);
        recipeModified |= ImGui::SliderFloat("Stroke Opacity", &activeRecipe.opacity, 0.05f, 1.0f, "%.2f");
        recipeModified |= ImGui::SliderFloat("Paper Grain", &activeRecipe.grain, 0.0f, 1.0f, "%.2f");
        recipeModified |= ImGui::Checkbox("No Overlap Darkening (union)", &activeRecipe.unionOverlap);

        // =====================================================================
        // Interactive Multi-Coat & Modulator Inspector
        // =====================================================================
        if (ImGui::CollapsingHeader("Live Multi-Coat & Modulator Editor")) {
            ImGui::Indent(8.0f);

            if (ImGui::Button("+ Add New Coat Layer")) {
                Folio::BrushCoatSpec newCoat;
                newCoat.name = "Custom Coat " + std::to_string(activeRecipe.coats.size() + 1);
                activeRecipe.coats.push_back(newCoat);
                recipeModified = true;
            }

            int coatToDelete = -1;
            for (int cIdx = 0; cIdx < static_cast<int>(activeRecipe.coats.size()); ++cIdx) {
                auto& coat = activeRecipe.coats[cIdx];
                ImGui::PushID(cIdx);

                char headerTitle[128];
                snprintf(headerTitle, sizeof(headerTitle), "[Coat %d] %s (Mode: %s)###CoatHeader",
                         cIdx + 1, coat.name.c_str(),
                         (coat.emissionMode == Folio::CoatEmissionMode::ContinuousRibbon) ? "Ribbon" : (coat.emissionMode == Folio::CoatEmissionMode::ParticleStamp ? "Particles" : "Special"));

                if (ImGui::TreeNode(headerTitle)) {
                    // Coat Name
                    char nameBuf[64];
                    strncpy(nameBuf, coat.name.c_str(), sizeof(nameBuf) - 1);
                    nameBuf[sizeof(nameBuf) - 1] = '\0';
                    if (ImGui::InputText("Coat Name", nameBuf, sizeof(nameBuf))) {
                        coat.name = nameBuf;
                        recipeModified = true;
                    }

                    // Emission Mode
                    const char* modes[] = { "Continuous Ribbon", "Particle Stamp", "Musical Notes", "Yarn Strand Bumps" };
                    int curMode = static_cast<int>(coat.emissionMode);
                    if (ImGui::Combo("Emission Mode", &curMode, modes, IM_ARRAYSIZE(modes))) {
                        coat.emissionMode = static_cast<Folio::CoatEmissionMode>(curMode);
                        recipeModified = true;
                    }

                    recipeModified |= ImGui::SliderFloat("Width Scale", &coat.widthScale, 0.05f, 4.0f, "%.2fx");
                    recipeModified |= ImGui::SliderFloat("Lateral Offset (Normal)", &coat.lateralOffsetMm, -20.0f, 20.0f, "%.2f mm");
                    recipeModified |= ImGui::SliderFloat("World Offset X", &coat.worldOffsetX, -20.0f, 20.0f, "%.2f mm");
                    recipeModified |= ImGui::SliderFloat("World Offset Y", &coat.worldOffsetY, -20.0f, 20.0f, "%.2f mm");

                    if (coat.emissionMode == Folio::CoatEmissionMode::YarnStrandBumps) {
                        recipeModified |= ImGui::SliderFloat("Twist Length (brush sizes)", &coat.yarnPeriodBrushSizes, 1.0f, 8.0f, "%.1f");
                        recipeModified |= ImGui::SliderFloat("Strand Sway", &coat.yarnAmplitudeRatio, 0.05f, 0.8f, "%.2f");
                    }
                    if (coat.emissionMode != Folio::CoatEmissionMode::ContinuousRibbon && coat.emissionMode != Folio::CoatEmissionMode::YarnStrandBumps) {
                        recipeModified |= ImGui::Checkbox("Flat-Based Triangle (flag)", &coat.triangleFan);
                        recipeModified |= ImGui::SliderFloat("Particle Spacing", &coat.particleGapMm, 0.5f, 30.0f, "%.1f mm");
                        recipeModified |= ImGui::SliderFloat("Particle Length", &coat.particleLengthMm, 0.5f, 30.0f, "%.1f mm");
                        recipeModified |= ImGui::SliderFloat("Tip/Base Radius Ratio", &coat.radiusEndRatio, 0.01f, 3.0f, "%.2f");

                        const char* orients[] = { "Follow Tangent", "Perpendicular Alternate", "Gravity Down (+Y)", "Fixed Angle (45°)", "Hang Down From Stroke (flat base)" };
                        int curOrient = static_cast<int>(coat.particleOrientation);
                        if (ImGui::Combo("Orientation", &curOrient, orients, IM_ARRAYSIZE(orients))) {
                            coat.particleOrientation = static_cast<Folio::ParticleOrientation>(curOrient);
                            recipeModified = true;
                        }
                    }

                    // Color Mode
                    const char* colModes[] = { "Inherit Base", "Fixed Color", "Invert Base Hue" };
                    int curColMode = static_cast<int>(coat.colorMode);
                    if (ImGui::Combo("Color Mode", &curColMode, colModes, IM_ARRAYSIZE(colModes))) {
                        coat.colorMode = static_cast<Folio::CoatColorMode>(curColMode);
                        recipeModified = true;
                    }
                    if (coat.colorMode == Folio::CoatColorMode::FixedColor) {
                        float fc[4] = {
                            coat.fixedColor.r / 255.0f, coat.fixedColor.g / 255.0f,
                            coat.fixedColor.b / 255.0f, coat.fixedColor.a / 255.0f
                        };
                        if (ImGui::ColorEdit4("Fixed Coat Color", fc)) {
                            coat.fixedColor = Folio::ColorRgba(
                                static_cast<uint8_t>(fc[0] * 255.0f), static_cast<uint8_t>(fc[1] * 255.0f),
                                static_cast<uint8_t>(fc[2] * 255.0f), static_cast<uint8_t>(fc[3] * 255.0f)
                            );
                            recipeModified = true;
                        }
                    }

                    recipeModified |= ImGui::SliderFloat("Hue Offset", &coat.hueOffsetDeg, -180.0f, 180.0f, "%.1f°");
                    recipeModified |= ImGui::SliderFloat("Lightness Offset", &coat.lightnessOffset, -1.0f, 1.0f, "%.2f");
                    recipeModified |= ImGui::SliderFloat("Opacity Scale", &coat.opacityScale, 0.0f, 1.0f, "%.2f");

                    // Modulators list
                    ImGui::SeparatorText("Signal Modulators");
                    if (ImGui::Button("+ Add Modulator")) {
                        Folio::BrushModulator m;
                        coat.modulators.push_back(m);
                        recipeModified = true;
                    }

                    int modToDelete = -1;
                    for (int mIdx = 0; mIdx < static_cast<int>(coat.modulators.size()); ++mIdx) {
                        auto& mod = coat.modulators[mIdx];
                        ImGui::PushID(mIdx);

                        ImGui::Text("Modulator #%d", mIdx + 1);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Delete##Mod")) {
                            modToDelete = mIdx;
                        }

                        const char* sources[] = {
                            "Arc Length", "Pressure", "Pressure Integral", "Velocity",
                            "Tilt", "Travel Angle", "Stroke Progress", "Particle Index", "Time (Seconds)",
                            "Time Since Input", "Time Since Stroke End", "Distance Remaining"
                        };
                        int curSrc = static_cast<int>(mod.source);
                        if (ImGui::Combo("Source", &curSrc, sources, IM_ARRAYSIZE(sources))) {
                            mod.source = static_cast<Folio::ModulatorSource>(curSrc);
                            recipeModified = true;
                        }

                        const char* waveforms[] = {
                            "Linear", "Sine Wave", "Triangle Zigzag", "Mirror Cycle",
                            "Alternate Swap (+/-1)", "1D Value Noise", "Hash Random",
                            "Ease Out Clamped", "Ease Out Drip Synced", "Blink Pulse", "Traveling Wave (Jelly)"
                        };
                        int curWf = static_cast<int>(mod.waveform);
                        if (ImGui::Combo("Waveform", &curWf, waveforms, IM_ARRAYSIZE(waveforms))) {
                            mod.waveform = static_cast<Folio::ModulatorWaveform>(curWf);
                            recipeModified = true;
                        }

                        const char* targets[] = {
                            "Radius Scale", "Lateral Offset (Normal)", "World Offset X", "World Offset Y",
                            "Particle Length", "Hue Shift (Deg)", "Lightness Shift", "Opacity Scale"
                        };
                        int curTgt = static_cast<int>(mod.target);
                        if (ImGui::Combo("Target", &curTgt, targets, IM_ARRAYSIZE(targets))) {
                            mod.target = static_cast<Folio::ModulatorTarget>(curTgt);
                            recipeModified = true;
                        }

                        recipeModified |= ImGui::SliderFloat("Frequency", &mod.frequency, 0.001f, 10.0f, "%.4f");
                        recipeModified |= ImGui::SliderFloat("Amplitude", &mod.amplitude, -20.0f, 20.0f, "%.2f");
                        recipeModified |= ImGui::SliderFloat("Phase", &mod.phase, -3.1415f, 3.1415f, "%.2f rad");
                        recipeModified |= ImGui::SliderFloat("Bias", &mod.bias, -10.0f, 10.0f, "%.2f");

                        ImGui::Separator();
                        ImGui::PopID();
                    }

                    if (modToDelete >= 0 && modToDelete < static_cast<int>(coat.modulators.size())) {
                        coat.modulators.erase(coat.modulators.begin() + modToDelete);
                        recipeModified = true;
                    }

                    if (ImGui::Button("Delete This Coat Layer")) {
                        coatToDelete = cIdx;
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (coatToDelete >= 0 && coatToDelete < static_cast<int>(activeRecipe.coats.size())) {
                activeRecipe.coats.erase(activeRecipe.coats.begin() + coatToDelete);
                recipeModified = true;
            }

            ImGui::Unindent(8.0f);
        }

        if (recipeModified) {
            layerManager.ApplyRecipeBrushSettings();
            layerManager.RebuildDocumentCustomInstances();
            needsRedraw = true;
        }

        ImGui::EndTabItem();
    }
}

} // namespace Folio::V2
