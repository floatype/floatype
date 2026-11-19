// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef MOZC_RENDERER_MAC_CANDIDATE_WINDOW_H_
#define MOZC_RENDERER_MAC_CANDIDATE_WINDOW_H_

#import <Carbon/Carbon.h>

#include "renderer/mac/RendererBaseWindow.h"
#include "protocol/config.pb.h"

namespace mozc {
namespace client {
class SendCommandInterface;
}  // namespace client
namespace commands {
class CandidateWindow;
}  // namespace commands
namespace renderer {
class TableLayout;

namespace mac {
// CandidateWindow holds a carbon window and maintains the connection
// between a window and a CandidateView.
class CandidateWindow : public RendererBaseWindow {
 public:
  CandidateWindow();
  CandidateWindow(const CandidateWindow &) = delete;
  CandidateWindow &operator=(const CandidateWindow &) = delete;
  virtual ~CandidateWindow();
  void SetSendCommandInterface(
      client::SendCommandInterface *send_command_interface);
  void SetCandidateWindow(const commands::CandidateWindow &candidate_window);
  // floatype: the looks for the next candidates: `conversion` for the
  // conversion window, `suggest` for predictions and suggestions.
  // `kind_mark` draws the footer's dot (candidate_kind_mark).
  void SetLook(const config::Config::CandidateStyle &conversion,
               const config::Config::CandidateStyle &suggest,
               bool kind_mark = false) {
    conversion_style_ = conversion;
    suggest_style_ = suggest;
    kind_mark_ = kind_mark;
  }
  void Show() override;
  void Hide() override;
  void MoveWindow(const NSPoint &point) override;
  void SetWindowLevel(NSInteger window_level) override;
  NSWindow *WindowForTesting() const { return window_; }
  NSView *ViewForTesting() const { return view_; }
  NSWindow *ShadowWindowForTesting() const { return shadow_window_; }
  const renderer::TableLayout *GetTableLayout() const;

 private:
  void InitWindow();
  void ResetView();
  // floatype: puts the window's part of the look (corners, opacity) on it,
  // and the shadow's window behind it.
  void ApplyWindowLook();
  void PlaceShadow();
  mozc::client::SendCommandInterface *command_sender_;
  config::Config::CandidateStyle conversion_style_;
  config::Config::CandidateStyle suggest_style_;
  bool kind_mark_ = false;
  // Draws the look's shadow, larger than the system's window shadow.
  NSPanel *shadow_window_ = nil;
};

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
#endif  // MOZC_RENDERER_MAC_CANDIDATE_WINDOW_H_
