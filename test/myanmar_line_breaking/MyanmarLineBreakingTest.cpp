#include <Epub/ParsedText.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Line {
  std::vector<std::string> words;
};

// Fixture metrics (stub renderer): every glyph is 8 px wide and a space is 4 px.
std::vector<Line> layout(const std::vector<std::string>& words, const bool hyphenation, const uint16_t width) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, hyphenation, false, style);
  for (const auto& word : words) text.addWord(word, EpdFontFamily::REGULAR);
  std::vector<Line> lines;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::unique_ptr<TextBlock> block, auto) {
    auto& line = lines.emplace_back();
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      line.words.emplace_back(block->wordText(i));
    }
  });
  return lines;
}

std::vector<std::vector<std::string>> wordsOf(const std::vector<Line>& lines) {
  std::vector<std::vector<std::string>> result;
  result.reserve(lines.size());
  for (const auto& line : lines) result.push_back(line.words);
  return result;
}

// က် = U+1000 KA + U+103A ASAT: one closed syllable, 16 px in the stub renderer.
std::string myanmar(const size_t syllables) {
  std::string out;
  for (size_t i = 0; i < syllables; ++i) out += "\xE1\x80\x80\xE1\x80\xBA";
  return out;
}

// ပျက် = U+1015 PA + U+103B MEDIAL YA + U+1000 KA + U+103A ASAT: one closed syllable "pyak".
const std::string PYAUK = "\xE1\x80\x95\xE1\x80\xBB\xE1\x80\x80\xE1\x80\xBA";

bool hasHyphen(const std::vector<std::vector<std::string>>& lines) {
  for (const auto& line : lines) {
    for (const auto& word : line) {
      if (word.find('-') != std::string::npos) return true;
    }
  }
  return false;
}

bool sameLine(const std::vector<std::vector<std::string>>& lines, const std::string& a, const std::string& b) {
  for (const auto& line : lines) {
    const bool hasA = std::find(line.begin(), line.end(), a) != line.end();
    const bool hasB = std::find(line.begin(), line.end(), b) != line.end();
    if (hasA || hasB) return hasA && hasB;
  }
  return false;
}

}  // namespace

TEST(MyanmarLineBreaking, HyphenationOffSplitsAtSyllables) {
  // 16 px word + 4 px space leaves 24 px: the two-syllable word splits after its first syllable.
  const auto lines = layout({myanmar(1), myanmar(2), myanmar(1)}, false, 44);
  const std::vector<std::vector<std::string>> expected{{myanmar(1), myanmar(1)}, {myanmar(1), myanmar(1)}};
  EXPECT_EQ(wordsOf(lines), expected);
}

TEST(MyanmarLineBreaking, HyphenationOffDoesNotInsertHyphen) {
  const auto lines = wordsOf(layout({myanmar(1), myanmar(2), myanmar(1)}, false, 44));
  EXPECT_FALSE(hasHyphen(lines)) << "Burmese syllables never take a hyphen";
}

TEST(MyanmarLineBreaking, HyphenationOnSplitsAtSyllablesToo) {
  const auto lines = layout({myanmar(1), myanmar(2), myanmar(1)}, true, 44);
  const std::vector<std::vector<std::string>> expected{{myanmar(1), myanmar(1)}, {myanmar(1), myanmar(1)}};
  EXPECT_EQ(wordsOf(lines), expected);
  EXPECT_FALSE(hasHyphen(wordsOf(lines)));
}

TEST(MyanmarLineBreaking, SpaceBreakOnlyWhenNoSyllableFits) {
  // A single syllable is wider than the 10 px line and has no internal break, so it moves whole.
  const auto lines = layout({myanmar(1), myanmar(1)}, false, 10);
  const std::vector<std::vector<std::string>> expected{{myanmar(1)}, {myanmar(1)}};
  EXPECT_EQ(wordsOf(lines), expected);
}

TEST(MyanmarLineBreaking, NeverSplitsInsideAClosedClusterSyllable) {
  // ပျက် must never break after the medial; the whole syllable moves as one unit.
  const auto lines = layout({PYAUK, PYAUK}, false, 24);
  const std::vector<std::vector<std::string>> expected{{PYAUK}, {PYAUK}};
  EXPECT_EQ(wordsOf(lines), expected);
}

TEST(MyanmarLineBreaking, HyphenationOffKeepsLatinWordsWholeInMixedText) {
  Hyphenator::setPreferredLanguage("en");
  // က + space leaves 38 px; "banana" (48 px) does not fit and must not be hyphenated
  // when the hyphenation setting is off, even in a paragraph containing Myanmar.
  const auto lines = wordsOf(layout({myanmar(1), "banana"}, false, 50));
  const std::vector<std::vector<std::string>> expected{{myanmar(1)}, {"banana"}};
  EXPECT_EQ(lines, expected);
  EXPECT_FALSE(hasHyphen(lines));
}

// A publisher can split one Myanmar syllable across tokens (stray space or inline
// markup): န | ယ်, ရ | င်, ဆ | က်. The layout must keep each pair on one line.
TEST(MyanmarLineBreaking, GluesPublisherSplitSyllables) {
  const std::string na = "\xE1\x80\x94", yay = "\xE1\x80\x9A\xE1\x80\xBA";
  const std::string ra = "\xE1\x80\x9B", nga = "\xE1\x80\x84\xE1\x80\xBA";
  const std::string sa = "\xE1\x80\x86", ka = "\xE1\x80\x80\xE1\x80\xBA";
  const std::string filler = myanmar(2);  // 32 px, pushes the pair toward the line end
  for (const bool hyphenation : {false, true}) {
    // 40 px: filler (32) + space (4) leaves 4 px, so the glued pair must move to line 2 whole.
    const auto lines = wordsOf(layout({filler, na, yay, ra, nga, sa, ka}, hyphenation, 40));
    EXPECT_TRUE(sameLine(lines, na, yay)) << "hyphenation=" << hyphenation;
    EXPECT_TRUE(sameLine(lines, ra, nga)) << "hyphenation=" << hyphenation;
    EXPECT_TRUE(sameLine(lines, sa, ka)) << "hyphenation=" << hyphenation;
  }
}

// A real space between two complete syllables must stay a break opportunity.
TEST(MyanmarLineBreaking, SpaceBetweenCompleteWordsStillBreaks) {
  const std::string maa = "\xE1\x80\x99\xE1\x80\xAC";  // မ + aa vowel: a complete open syllable
  const auto lines = layout({maa, maa}, false, 16);
  const std::vector<std::vector<std::string>> expected{{maa}, {maa}};
  EXPECT_EQ(wordsOf(lines), expected);
}
