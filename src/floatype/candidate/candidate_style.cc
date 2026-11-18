#include "floatype/candidate/candidate_style.h"

#include <cstdint>
#include <vector>

#include "protocol/config.pb.h"
#include "protocol/renderer_style.pb.h"

namespace mozc::floatype {
namespace {

using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::Message;

// Visits the text styles anywhere in `message`, nested ones included.
void ScaleIn(Message& message, double factor) {
  if (auto* text =
          dynamic_cast<renderer::RendererStyle::TextStyle*>(&message)) {
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

void SetColor(renderer::RendererStyle::RGBAColor* color, uint32_t rgb) {
  color->set_r((rgb >> 16) & 0xff);
  color->set_g((rgb >> 8) & 0xff);
  color->set_b(rgb & 0xff);
}

}  // namespace

void ApplyColors(renderer::RendererStyle& style,
                 const config::Config::CandidateStyle& colors) {
  if (colors.has_text_color()) {
    SetColor(style.mutable_candidate_style()->mutable_foreground_color(),
             colors.text_color());
  }
  if (colors.has_focused_background_color()) {
    SetColor(style.mutable_focused_background_color(),
             colors.focused_background_color());
  }
  if (colors.has_focused_border_color()) {
    SetColor(style.mutable_focused_border_color(),
             colors.focused_border_color());
  }
  if (colors.has_border_color()) {
    SetColor(style.mutable_border_color(), colors.border_color());
  }
}

void ScaleFonts(renderer::RendererStyle& style, uint32_t percent) {
  if (percent != 100) {
    ScaleIn(style, percent / 100.0);
  }
}

}  // namespace mozc::floatype
