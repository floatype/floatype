#include "floatype/candidate/candidate_style.h"

#include <cstdint>
#include <vector>

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

}  // namespace

void ScaleFonts(renderer::RendererStyle& style, uint32_t percent) {
  if (percent != 100) {
    ScaleIn(style, percent / 100.0);
  }
}

}  // namespace mozc::floatype
