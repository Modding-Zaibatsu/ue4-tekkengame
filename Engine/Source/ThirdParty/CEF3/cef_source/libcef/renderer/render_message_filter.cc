/// Copyright (c) 2012 The Chromium Embedded Framework Authors.
// Portions (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/renderer/render_message_filter.h"
#include "libcef/renderer/thread_util.h"
#include "libcef/common/cef_messages.h"

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "content/common/devtools_messages.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/web/WebSecurityPolicy.h"
#include "url/gurl.h"
#include "url/url_util.h"

CefRenderMessageFilter::CefRenderMessageFilter()
    : sender_(NULL) {
}

CefRenderMessageFilter::~CefRenderMessageFilter() {
}

void CefRenderMessageFilter::OnFilterAdded(IPC::Sender* sender) {
  sender_ = sender;
}

void CefRenderMessageFilter::OnFilterRemoved() {
}

bool CefRenderMessageFilter::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  if (message.type() == DevToolsAgentMsg_Attach::ID ||
      message.type() == DevToolsAgentMsg_Detach::ID) {
    // Observe the DevTools messages but don't handle them.
    handled = false;
  }

  IPC_BEGIN_MESSAGE_MAP(CefRenderMessageFilter, message)
    IPC_MESSAGE_HANDLER(DevToolsAgentMsg_Attach, OnDevToolsAgentAttach)
    IPC_MESSAGE_HANDLER(DevToolsAgentMsg_Detach, OnDevToolsAgentDetach)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void CefRenderMessageFilter::OnDevToolsAgentAttach(
    const std::string& host_id) {
  CEF_POST_TASK_RT(
      base::Bind(&CefRenderMessageFilter::OnDevToolsAgentAttach_RT, this));
}

void CefRenderMessageFilter::OnDevToolsAgentDetach() {
  // CefContentRendererClient::DevToolsAgentDetached() needs to be called after
  // the IPC message has been handled by DevToolsAgent. A workaround for this is
  // to first post to the IO thread and then post to the renderer thread.
  base::MessageLoop::current()->PostTask(FROM_HERE,
      base::Bind(&CefRenderMessageFilter::OnDevToolsAgentDetach_IOT, this));
}

void CefRenderMessageFilter::OnDevToolsAgentAttach_RT() {
  CEF_REQUIRE_RT();
  CefContentRendererClient::Get()->DevToolsAgentAttached();
}

void CefRenderMessageFilter::OnDevToolsAgentDetach_IOT() {
  CEF_POST_TASK_RT(
      base::Bind(&CefRenderMessageFilter::OnDevToolsAgentDetach_RT, this));
}

void CefRenderMessageFilter::OnDevToolsAgentDetach_RT() {
  CEF_REQUIRE_RT();
  CefContentRendererClient::Get()->DevToolsAgentDetached();
}
