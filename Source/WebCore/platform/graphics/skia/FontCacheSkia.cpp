/*
 * Copyright (C) 2024 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"
#include "FontCache.h"

#include "CharacterProperties.h"
#include "Font.h"
#include "FontDescription.h"
#include "StyleFontSizeFunctions.h"
#include <skia/ports/SkFontMgr_fontconfig.h>
#include <wtf/Assertions.h>
#include <wtf/text/CString.h>
#include <wtf/unicode/CharacterNames.h>

namespace WebCore {

void FontCache::platformInit()
{
    m_fontManager = SkFontMgr_New_FontConfig(nullptr);
}

RefPtr<Font> FontCache::systemFallbackForCharacterCluster(const FontDescription& description, const Font&, IsForPlatformFont, PreferColoredFont, StringView stringView)
{
    // FIXME: matchFamilyStyleCharacter is slow, we need a cache here, see https://bugs.webkit.org/show_bug.cgi?id=203544.
    auto codePoints = stringView.codePoints();
    auto codePointsIterator = codePoints.begin();
    char32_t baseCharacter = *codePointsIterator;
    ++codePointsIterator;
    if (isDefaultIgnorableCodePoint(baseCharacter) || isPrivateUseAreaCharacter(baseCharacter))
        return nullptr;

    bool isEmoji = codePointsIterator != codePoints.end() && *codePointsIterator == emojiVariationSelector;

    // FIXME: handle locale.
    Vector<const char*, 1> bcp47;
    if (isEmoji)
        bcp47.append("und-Zsye");

    // FIXME: handle synthetic properties.
    auto features = computeFeatures(description, { });
    auto typeface = m_fontManager->matchFamilyStyleCharacter(nullptr, { }, bcp47.data(), bcp47.size(), baseCharacter);
    FontPlatformData alternateFontData(WTFMove(typeface), description.computedSize(), false /* syntheticBold */, false /* syntheticOblique */, description.orientation(), description.widthVariant(), description.textRenderingMode(), WTFMove(features));
    return fontForPlatformData(alternateFontData);
}

Vector<String> FontCache::systemFontFamilies()
{
    int count = m_fontManager->countFamilies();
    Vector<String> fontFamilies;
    fontFamilies.reserveInitialCapacity(count);
    for (int i = 0; i < count; ++i) {
        SkString familyName;
        m_fontManager->getFamilyName(i, &familyName);
        fontFamilies.append(String::fromUTF8(familyName.data()));
    }
    return fontFamilies;
}

bool FontCache::isSystemFontForbiddenForEditing(const String&)
{
    return false;
}

Ref<Font> FontCache::lastResortFallbackFont(const FontDescription& fontDescription)
{
    // We want to return a fallback font here, otherwise the logic preventing FontConfig
    // matches for non-fallback fonts might return 0. See isFallbackFontAllowed.
    if (RefPtr<Font> font = fontForFamily(fontDescription, "serif"_s))
        return font.releaseNonNull();

    // This could be reached due to improperly-installed or misconfigured fontconfig.
    RELEASE_ASSERT_NOT_REACHED();
}

Vector<FontSelectionCapabilities> FontCache::getFontSelectionCapabilitiesInFamily(const AtomString&, AllowUserInstalledFonts)
{
    return { };
}

static String getFamilyNameStringFromFamily(const String& family)
{
    // If we're creating a fallback font (e.g. "-webkit-monospace"), convert the name into
    // the fallback name (like "monospace") that fontconfig understands.
    if (family.length() && !family.startsWith("-webkit-"_s))
        return family;

    if (family == familyNamesData->at(FamilyNamesIndex::StandardFamily) || family == familyNamesData->at(FamilyNamesIndex::SerifFamily))
        return "serif"_s;
    if (family == familyNamesData->at(FamilyNamesIndex::SansSerifFamily))
        return "sans-serif"_s;
    if (family == familyNamesData->at(FamilyNamesIndex::MonospaceFamily))
        return "monospace"_s;
    if (family == familyNamesData->at(FamilyNamesIndex::CursiveFamily))
        return "cursive"_s;
    if (family == familyNamesData->at(FamilyNamesIndex::FantasyFamily))
        return "fantasy"_s;

    return emptyString();
}

Vector<hb_feature_t> FontCache::computeFeatures(const FontDescription& fontDescription, const FontCreationContext& fontCreationContext)
{
    FeaturesMap featuresToBeApplied;

    // 7.2. Feature precedence
    // https://www.w3.org/TR/css-fonts-3/#feature-precedence

    // 1. Font features enabled by default, including features required for a given script.

    // FIXME: optical sizing.

    // 2. If the font is defined via an @font-face rule, the font features implied by the
    //    font-feature-settings descriptor in the @font-face rule.
    if (fontCreationContext.fontFaceFeatures()) {
        for (auto& fontFaceFeature : *fontCreationContext.fontFaceFeatures())
            featuresToBeApplied.set(fontFaceFeature.tag(), fontFaceFeature.value());
    }

    // 3. Font features implied by the value of the ‘font-variant’ property, the related ‘font-variant’
    //    subproperties and any other CSS property that uses OpenType features.
    for (auto& newFeature : computeFeatureSettingsFromVariants(fontDescription.variantSettings(), fontCreationContext.fontFeatureValues()))
        featuresToBeApplied.set(newFeature.key, newFeature.value);

    // 4. Feature settings determined by properties other than ‘font-variant’ or ‘font-feature-settings’.
    bool optimizeSpeed = fontDescription.textRenderingMode() == TextRenderingMode::OptimizeSpeed;
    bool shouldDisableLigaturesForSpacing = fontDescription.shouldDisableLigaturesForSpacing();

    // clig and liga are on by default in HarfBuzz.
    auto commonLigatures = fontDescription.variantCommonLigatures();
    if (shouldDisableLigaturesForSpacing || (commonLigatures == FontVariantLigatures::No || (commonLigatures == FontVariantLigatures::Normal && optimizeSpeed))) {
        featuresToBeApplied.set(fontFeatureTag("liga"), 0);
        featuresToBeApplied.set(fontFeatureTag("clig"), 0);
    }

    // dlig is off by default in HarfBuzz.
    auto discretionaryLigatures = fontDescription.variantDiscretionaryLigatures();
    if (!shouldDisableLigaturesForSpacing && discretionaryLigatures == FontVariantLigatures::Yes)
        featuresToBeApplied.set(fontFeatureTag("dlig"), 1);

    // hlig is off by default in HarfBuzz.
    auto historicalLigatures = fontDescription.variantHistoricalLigatures();
    if (!shouldDisableLigaturesForSpacing && historicalLigatures == FontVariantLigatures::Yes)
        featuresToBeApplied.set(fontFeatureTag("hlig"), 1);

    // calt is on by default in HarfBuzz.
    auto contextualAlternates = fontDescription.variantContextualAlternates();
    if (shouldDisableLigaturesForSpacing || (contextualAlternates == FontVariantLigatures::No || (contextualAlternates == FontVariantLigatures::Normal && optimizeSpeed)))
        featuresToBeApplied.set(fontFeatureTag("calt"), 0);

    switch (fontDescription.widthVariant()) {
    case FontWidthVariant::RegularWidth:
        break;
    case FontWidthVariant::HalfWidth:
        featuresToBeApplied.set(fontFeatureTag("hwid"), 1);
        break;
    case FontWidthVariant::ThirdWidth:
        featuresToBeApplied.set(fontFeatureTag("twid"), 1);
        break;
    case FontWidthVariant::QuarterWidth:
        featuresToBeApplied.set(fontFeatureTag("qwid"), 1);
        break;
    }

    switch (fontDescription.variantEastAsianVariant()) {
    case FontVariantEastAsianVariant::Normal:
        break;
    case FontVariantEastAsianVariant::Jis78:
        featuresToBeApplied.set(fontFeatureTag("jp78"), 1);
        break;
    case FontVariantEastAsianVariant::Jis83:
        featuresToBeApplied.set(fontFeatureTag("jp83"), 1);
        break;
    case FontVariantEastAsianVariant::Jis90:
        featuresToBeApplied.set(fontFeatureTag("jp90"), 1);
        break;
    case FontVariantEastAsianVariant::Jis04:
        featuresToBeApplied.set(fontFeatureTag("jp04"), 1);
        break;
    case FontVariantEastAsianVariant::Simplified:
        featuresToBeApplied.set(fontFeatureTag("smpl"), 1);
        break;
    case FontVariantEastAsianVariant::Traditional:
        featuresToBeApplied.set(fontFeatureTag("trad"), 1);
        break;
    }

    switch (fontDescription.variantEastAsianWidth()) {
    case FontVariantEastAsianWidth::Normal:
        break;
    case FontVariantEastAsianWidth::Full:
        featuresToBeApplied.set(fontFeatureTag("fwid"), 1);
        break;
    case FontVariantEastAsianWidth::Proportional:
        featuresToBeApplied.set(fontFeatureTag("pwid"), 1);
        break;
    }

    switch (fontDescription.variantEastAsianRuby()) {
    case FontVariantEastAsianRuby::Normal:
        break;
    case FontVariantEastAsianRuby::Yes:
        featuresToBeApplied.set(fontFeatureTag("ruby"), 1);
        break;
    }

    switch (fontDescription.variantNumericFigure()) {
    case FontVariantNumericFigure::Normal:
        break;
    case FontVariantNumericFigure::LiningNumbers:
        featuresToBeApplied.set(fontFeatureTag("lnum"), 1);
        break;
    case FontVariantNumericFigure::OldStyleNumbers:
        featuresToBeApplied.set(fontFeatureTag("onum"), 1);
        break;
    }

    switch (fontDescription.variantNumericSpacing()) {
    case FontVariantNumericSpacing::Normal:
        break;
    case FontVariantNumericSpacing::ProportionalNumbers:
        featuresToBeApplied.set(fontFeatureTag("pnum"), 1);
        break;
    case FontVariantNumericSpacing::TabularNumbers:
        featuresToBeApplied.set(fontFeatureTag("tnum"), 1);
        break;
    }

    switch (fontDescription.variantNumericFraction()) {
    case FontVariantNumericFraction::Normal:
        break;
    case FontVariantNumericFraction::DiagonalFractions:
        featuresToBeApplied.set(fontFeatureTag("frac"), 1);
        break;
    case FontVariantNumericFraction::StackedFractions:
        featuresToBeApplied.set(fontFeatureTag("afrc"), 1);
        break;
    }

    if (fontDescription.variantNumericOrdinal() == FontVariantNumericOrdinal::Yes)
        featuresToBeApplied.set(fontFeatureTag("ordn"), 1);

    if (fontDescription.variantNumericSlashedZero() == FontVariantNumericSlashedZero::Yes)
        featuresToBeApplied.set(fontFeatureTag("zero"), 1);

    // 5. Font features implied by the value of ‘font-feature-settings’ property.
    for (auto& newFeature : fontDescription.featureSettings())
        featuresToBeApplied.set(newFeature.tag(), newFeature.value());

    if (featuresToBeApplied.isEmpty())
        return { };

    Vector<hb_feature_t> features;
    features.reserveInitialCapacity(featuresToBeApplied.size());
    for (const auto& iter : featuresToBeApplied)
        features.append({ HB_TAG(iter.key[0], iter.key[1], iter.key[2], iter.key[3]), static_cast<uint32_t>(iter.value), 0, static_cast<unsigned>(-1) });
    return features;
}

std::unique_ptr<FontPlatformData> FontCache::createFontPlatformData(const FontDescription& fontDescription, const AtomString& family, const FontCreationContext& fontCreationContext)
{
    auto width = SkFontStyle::kNormal_Width;

    auto weight = SkFontStyle::kNormal_Weight;
    if (isFontWeightBold(fontDescription.weight()))
        weight = SkFontStyle::kBold_Weight;

    auto slant = SkFontStyle::kUpright_Slant;
    if (fontDescription.italic())
        slant = SkFontStyle::kItalic_Slant;

    auto familyName = getFamilyNameStringFromFamily(family);
    auto typeface = m_fontManager->matchFamilyStyle(familyName.utf8().data(), SkFontStyle { weight, width, slant });
    if (!typeface)
        return nullptr;

    auto size = fontDescription.adjustedSizeForFontFace(fontCreationContext.sizeAdjust());
    auto features = computeFeatures(fontDescription, fontCreationContext);
    FontPlatformData platformData(WTFMove(typeface), size, false /* syntheticBold */, false /* syntheticOblique */, fontDescription.orientation(), fontDescription.widthVariant(), fontDescription.textRenderingMode(), WTFMove(features));

    platformData.updateSizeWithFontSizeAdjust(fontDescription.fontSizeAdjust(), fontDescription.computedSize());
    auto platformDataUniquePtr = makeUnique<FontPlatformData>(platformData);

    return platformDataUniquePtr;
}

std::optional<ASCIILiteral> FontCache::platformAlternateFamilyName(const String&)
{
    return std::nullopt;
}

void FontCache::platformInvalidate()
{
}

void FontCache::platformPurgeInactiveFontData()
{
    m_harfBuzzFontCache.clear();
}

} // namespace WebCore
