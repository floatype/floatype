#include "floatype/look/window_look.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "protocol/config.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/renderer_style_handler.h"

namespace mozc::floatype {
namespace {

using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::Message;
using ::mozc::config::Config;
using ::mozc::renderer::RendererStyle;
using ::mozc::renderer::RendererStyleHandler;

void SetColor(RendererStyle::RGBAColor* color, uint32_t rgb) {
  color->set_r((rgb >> 16) & 0xff);
  color->set_g((rgb >> 8) & 0xff);
  color->set_b(rgb & 0xff);
}

// Whether `rgb` is a dark color, by its perceived brightness.
bool IsDark(uint32_t rgb) {
  const double brightness = 0.299 * ((rgb >> 16) & 0xff) +
                            0.587 * ((rgb >> 8) & 0xff) + 0.114 * (rgb & 0xff);
  return brightness < 128;
}

// `alpha` of `top` over `bottom`.
uint32_t Blend(uint32_t top, uint32_t bottom, double alpha) {
  uint32_t rgb = 0;
  for (const int shift : {16, 8, 0}) {
    const double channel = alpha * ((top >> shift) & 0xff) +
                           (1 - alpha) * ((bottom >> shift) & 0xff);
    rgb |= static_cast<uint32_t>(std::lround(channel)) << shift;
  }
  return rgb;
}

struct Palette {
  uint32_t background;
  uint32_t text;
  uint32_t secondary_text;
  uint32_t border;
  double border_alpha;
  double soft_shadow_opacity;
  double strong_shadow_opacity;
};

// Floatype's own look: white or near-black, a hairline outline, a soft
// shadow, and a muted blue focused row.
constexpr Palette kLight = {0xffffff, 0x17191e, 0x989ca5, 0x111827,
                            0.07,     0.16,     0.32};
constexpr Palette kDark = {0x26272b, 0xeceef2, 0x80848d, 0xffffff,
                           0.09,     0.55,     0.75};
constexpr uint32_t kFocusedBackground = 0xc4d0e1;
constexpr uint32_t kInk = 0x111318;  // Text on a light focused row.
constexpr uint32_t kShadowColor = 0x111827;

// Visits the text styles anywhere in `message`, nested ones included.
void ScaleIn(Message& message, double factor) {
  if (auto* text = dynamic_cast<RendererStyle::TextStyle*>(&message)) {
    if (text->has_font_size()) {
      text->set_font_size(text->font_size() * factor);
    }
    return;
  }
  std::vector<const FieldDescriptor*> fields;
  message.GetReflection()->ListFields(message, &fields);
  for (const FieldDescriptor* field : fields) {
    if (field->cpp_type() != FieldDescriptor::CPPTYPE_MESSAGE) {
      continue;
    }
    if (field->is_repeated()) {
      for (int i = 0; i < message.GetReflection()->FieldSize(message, field);
           ++i) {
        ScaleIn(*message.GetReflection()->MutableRepeatedMessage(&message,
                                                                 field, i),
                factor);
      }
    } else {
      ScaleIn(*message.GetReflection()->MutableMessage(&message, field),
              factor);
    }
  }
}

}  // namespace

WindowLook ResolveWindowLook(const Config::CandidateStyle& style,
                             bool system_dark) {
  WindowLook look;
  switch (style.theme()) {
    case Config::CandidateStyle::DARK:
      look.dark = true;
      break;
    case Config::CandidateStyle::CUSTOM:
      // The palette under the colors set follows the background, so that the
      // numbers, descriptions and footer stay readable on a dark one.
      look.dark =
          style.has_background_color() && IsDark(style.background_color());
      break;
    case Config::CandidateStyle::LIGHT:
      break;
    default:  // AUTO
      look.dark = system_dark;
      break;
  }
  const Palette& palette = look.dark ? kDark : kLight;
  look.background = palette.background;
  look.text = palette.text;
  look.secondary_text = palette.secondary_text;
  look.focused_background = kFocusedBackground;
  look.border = palette.border;
  look.border_alpha = palette.border_alpha;
  if (style.theme() == Config::CandidateStyle::CUSTOM) {
    look.background = style.has_background_color() ? style.background_color()
                                                   : look.background;
    look.text = style.has_text_color() ? style.text_color() : look.text;
    look.focused_background = style.has_focused_background_color()
                                  ? style.focused_background_color()
                                  : look.focused_background;
    if (style.has_focused_border_color()) {
      look.focused_border = style.focused_border_color();
      look.focused_border_alpha = 1;
    }
    if (style.has_border_color()) {
      look.border = style.border_color();
      look.border_alpha = 1;
    }
  }
  // Ink on a light focused row, white on a dark one, the numbers dimmer.
  const bool dark_focus = IsDark(look.focused_background);
  look.focused_text = dark_focus ? 0xffffff : kInk;
  look.focused_secondary_text = Blend(
      look.focused_text, look.focused_background, dark_focus ? 0.78 : 0.62);
  if (look.focused_border_alpha == 0) {
    look.focused_border = look.focused_background;
  }

  look.size_percent = style.size_percent();
  const double scale = style.size_percent() / 100.0;
  look.window_padding = 4 * scale;
  look.row_height = 22 * scale;
  look.row_padding = 9 * scale;
  look.row_corner_radius = 7 * scale;
  look.number_width = 14 * scale;
  look.footer_top_padding = 2 * scale;
  look.footer_bottom_padding = 1 * scale;
  look.scrollbar_width = 4 * scale;
  look.corner_radius = style.corner_radius();
  look.opacity = style.opacity_percent() / 100.0;
  look.shadow = style.shadow();
  look.shadow_color = kShadowColor;
  switch (style.shadow()) {
    case Config::CandidateStyle::NONE:
      break;
    case Config::CandidateStyle::STRONG:
      look.shadow_radius = 40;
      look.shadow_offset_y = 16;
      look.shadow_opacity = palette.strong_shadow_opacity;
      break;
    default:  // SOFT
      look.shadow_radius = 32;
      look.shadow_offset_y = 12;
      look.shadow_opacity = palette.soft_shadow_opacity;
      break;
  }
  look.font_family = style.font_family();
  return look;
}

RendererStyle RendererStyleFor(const WindowLook& look) {
  RendererStyle style = RendererStyleHandler::GetRendererStyle();
  // Font sizes at 100%; ScaleFonts below scales them.
  style.mutable_candidate_style()->set_font_size(14);
  style.mutable_gap1_style()->set_font_size(14);
  style.mutable_shortcut_style()->set_font_size(12);
  style.mutable_description_style()->set_font_size(11);
  style.mutable_footer_style()->set_font_size(11);
  SetColor(style.mutable_candidate_style()->mutable_background_color(),
           look.background);
  SetColor(style.mutable_candidate_style()->mutable_foreground_color(),
           look.text);
  for (RendererStyle::TextStyle* text :
       {style.mutable_shortcut_style(), style.mutable_description_style(),
        style.mutable_footer_style(), style.mutable_footer_sub_label_style()}) {
    SetColor(text->mutable_foreground_color(), look.secondary_text);
    text->clear_background_color();
  }
  SetColor(style.mutable_focused_background_color(), look.focused_background);
  SetColor(style.mutable_focused_border_color(), look.focused_border);
  style.mutable_focused_border_color()->set_a(look.focused_border_alpha);
  SetColor(style.mutable_border_color(), look.border);
  style.mutable_border_color()->set_a(look.border_alpha);
  SetColor(style.mutable_scrollbar_indicator_color(), look.secondary_text);
  style.clear_footer_border_colors();
  // The paddings Mozc's table layout adds: the row's left one before the
  // number, its right one after the description, the footer's at its ends.
  const auto points = [](double value) {
    return static_cast<int32_t>(std::lround(value));
  };
  style.set_window_border(points(look.window_padding));
  style.set_row_rect_padding(0);
  style.set_scrollbar_width(points(look.scrollbar_width));
  style.mutable_shortcut_style()->set_left_padding(points(look.row_padding));
  style.mutable_shortcut_style()->set_right_padding(0);
  style.mutable_description_style()->set_left_padding(0);
  style.mutable_description_style()->set_right_padding(
      points(look.row_padding));
  style.mutable_footer_style()->set_left_padding(points(look.row_padding));
  style.mutable_footer_style()->set_right_padding(points(look.row_padding));
  ScaleFonts(style, look.size_percent);
  if (!look.font_family.empty()) {
    for (RendererStyle::TextStyle* text :
         {style.mutable_shortcut_style(), style.mutable_gap1_style(),
          style.mutable_candidate_style(), style.mutable_description_style(),
          style.mutable_footer_style(),
          style.mutable_footer_sub_label_style()}) {
      text->set_font_name(look.font_family);
    }
  }
  return style;
}

void ScaleFonts(RendererStyle& style, uint32_t percent) {
  if (percent != 100) {
    ScaleIn(style, percent / 100.0);
  }
}

uint32_t KindMarkColor(const config::Config::CandidateStyle& style,
                       bool prediction, bool dark) {
  if (style.has_kind_mark_color()) {
    return style.kind_mark_color() & 0xffffff;
  }
  if (prediction) {
    return 0x6f8fbf;
  }
  return dark ? 0x5c6068 : 0xc3c7cf;
}

}  // namespace mozc::floatype
