// floatype: puts first the word the user pinned for a reading (#28), in any
// segment whose candidates hold it, and marks it in the candidate window.
// It runs after the rewriters that reorder, so that neither the learned
// history nor anything else moves it down again.

#ifndef MOZC_REWRITER_PINNED_WORD_REWRITER_H_
#define MOZC_REWRITER_PINNED_WORD_REWRITER_H_

#include "converter/candidate.h"
#include "converter/segments.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"

namespace mozc {

class PinnedWordRewriter : public RewriterInterface {
 public:
  int capability(const ConversionRequest& request) const override {
    return RewriterInterface::CONVERSION;
  }

  // shortcut: only a candidate the converter already made is moved up; a
  // pinned word for a reading cut into other segments does not come. Add the
  // candidate if users miss it.
  bool Rewrite(const ConversionRequest& request,
               Segments* segments) const override;

  // Puts the mark on `candidate`'s description, or takes it off.
  static void Mark(converter::Candidate& candidate, bool pinned);
};

}  // namespace mozc

#endif  // MOZC_REWRITER_PINNED_WORD_REWRITER_H_
