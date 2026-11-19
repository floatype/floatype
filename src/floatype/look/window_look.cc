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

uint32_t Rgb(const RendererStyle::RGBAColor& color) {
  return (static_cast<uint32_t>(std::lround(color.r())) << 16) |
         (static_cast<uint32_t>(std::lround(color.g())) << 8) |
         static_cast<uint32_t>(std::lround(color.b()));
}

void SetColor(RendererStyle::RGBAColor* color, uint32_t rgb) {
  color->set_r((rgb >> 16) & 0xff);
  color->set_g((rgb >> 8) & 0xff);
  color->set_b(rgb & 0xff);
}

RendererStyle Palette(bool dark) {
  return RendererStyleHandler::GetRendererStyle(
      dark ? RendererStyleHandler::ColorTheme::kDark
           : RendererStyleHandler::ColorTheme::kLight);
}

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
  look.dark = style.theme() == Config::CandidateStyle::DARK ||
              (style.theme() == Config::CandidateStyle::AUTO && system_dark);
  const RendererStyle palette = Palette(look.dark);
  look.background = Rgb(palette.candidate_style().background_color());
  look.text = Rgb(palette.candidate_style().foreground_color());
  look.secondary_text = Rgb(palette.shortcut_style().foreground_color());
  look.number_background = Rgb(palette.shortcut_style().background_color());
  look.focused_background = Rgb(palette.focused_background_color());
  look.focused_border = Rgb(palette.focused_border_color());
  look.border = Rgb(palette.border_color());
  look.footer_top = Rgb(palette.footer_top_color());
  look.footer_bottom = Rgb(palette.footer_bottom_color());
  look.scrollbar_background = Rgb(palette.scrollbar_background_color());
  if (style.theme() == Config::CandidateStyle::CUSTOM) {
    if (style.has_background_color()) {
      // One background throughout, so no light band stays on a dark one.
      look.background = look.number_background = look.footer_top =
          look.footer_bottom = look.scrollbar_background =
              style.background_color();
    }
    look.text = style.has_text_color() ? style.text_color() : look.text;
    look.focused_background = style.has_focused_background_color()
                                  ? style.focused_background_color()
                                  : look.focused_background;
    look.focused_border = style.has_focused_border_color()
                              ? style.focused_border_color()
                              : look.focused_border;
    look.border = style.has_border_color() ? style.border_color() : look.border;
  }
  look.size_percent = style.size_percent();
  look.corner_radius = style.corner_radius();
  look.opacity = style.opacity_percent() / 100.0;
  look.shadow = style.shadow();
  switch (style.shadow()) {
    case Config::CandidateStyle::NONE:
      break;
    case Config::CandidateStyle::STRONG:
      look.shadow_radius = 20;
      look.shadow_offset_y = 8;
      look.shadow_opacity = 0.55;
      break;
    default:  // SOFT
      look.shadow_radius = 12;
      look.shadow_offset_y = 4;
      look.shadow_opacity = 0.3;
      break;
  }
  look.font_family = style.font_family();
  return look;
}

RendererStyle RendererStyleFor(const WindowLook& look) {
  RendererStyle style = Palette(look.dark);
  SetColor(style.mutable_candidate_style()->mutable_background_color(),
           look.background);
  SetColor(style.mutable_candidate_style()->mutable_foreground_color(),
           look.text);
  SetColor(style.mutable_shortcut_style()->mutable_foreground_color(),
           look.secondary_text);
  SetColor(style.mutable_shortcut_style()->mutable_background_color(),
           look.number_background);
  SetColor(style.mutable_focused_background_color(), look.focused_background);
  SetColor(style.mutable_focused_border_color(), look.focused_border);
  SetColor(style.mutable_border_color(), look.border);
  SetColor(style.mutable_footer_top_color(), look.footer_top);
  SetColor(style.mutable_footer_bottom_color(), look.footer_bottom);
  SetColor(style.mutable_scrollbar_background_color(),
           look.scrollbar_background);
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

}  // namespace mozc::floatype
