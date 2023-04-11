// Copyright (c) 2013 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CEF_LIBCEF_RENDERER_CONTENT_RENDERER_CLIENT_H_
#define CEF_LIBCEF_RENDERER_CONTENT_RENDERER_CLIENT_H_
#pragma once

#include <list>
#include <map>
#include <string>
#include <vector>

#include "libcef/renderer/browser_impl.h"

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/sequenced_task_runner.h"
#include "content/public/renderer/content_renderer_client.h"

namespace web_cache {
class WebCacheRenderProcessObserver;
}

class CefRenderProcessObserver;
struct Cef_CrossOriginWhiteListEntry_Params;
class SpellCheck;

class CefContentRendererClient : public content::ContentRendererClient,
                                 public base::MessageLoop::DestructionObserver {
 public:
  CefContentRendererClient();
  ~CefContentRendererClient() override;

  // Returns the singleton CefContentRendererClient instance.
  static CefContentRendererClient* Get();

  // Returns the browser associated with the specified RenderView.
  CefRefPtr<CefBrowserImpl> GetBrowserForView(content::RenderView* view);

  // Returns the browser associated with the specified main WebFrame.
  CefRefPtr<CefBrowserImpl> GetBrowserForMainFrame(blink::WebFrame* frame);

  // Called from CefBrowserImpl::OnDestruct().
  void OnBrowserDestroyed(CefBrowserImpl* browser);

  // Render thread task runner.
  base::SequencedTaskRunner* render_task_runner() const {
    return render_task_runner_.get();
  }

  int uncaught_exception_stack_size() const {
    return uncaught_exception_stack_size_;
  }

  void WebKitInitialized();
  void OnRenderProcessShutdown();

  void DevToolsAgentAttached();
  void DevToolsAgentDetached();

  // Returns the task runner for the current thread. If this is a WebWorker
  // thread and the task runner does not already exist it will be created.
  // Returns NULL if the current thread is not a valid render process thread.
  scoped_refptr<base::SequencedTaskRunner> GetCurrentTaskRunner();

  // Returns the task runner for the specified worker ID or NULL if the
  // specified worker ID is not valid.
  scoped_refptr<base::SequencedTaskRunner> GetWorkerTaskRunner(int worker_id);

  // Remove the task runner associated with the specified worker ID.
  void RemoveWorkerTaskRunner(int worker_id);

  // Perform cleanup work that needs to occur before shutdown when running in
  // single-process mode. Blocks until cleanup is complete.
  void RunSingleProcessCleanup();

  // ContentRendererClient implementation.
  void RenderThreadStarted() override;
  void RenderFrameCreated(content::RenderFrame* render_frame) override;
  void RenderViewCreated(content::RenderView* render_view) override;
  bool OverrideCreatePlugin(
      content::RenderFrame* render_frame,
      blink::WebLocalFrame* frame,
      const blink::WebPluginParams& params,
      blink::WebPlugin** plugin) override;
  bool HandleNavigation(content::RenderFrame* render_frame,
                        content::DocumentState* document_state,
                        int opener_id,
                        blink::WebFrame* frame,
                        const blink::WebURLRequest& request,
                        blink::WebNavigationType type,
                        blink::WebNavigationPolicy default_policy,
                        bool is_redirect) override;

  // MessageLoop::DestructionObserver implementation.
  void WillDestroyCurrentMessageLoop() override;

 private:
  void BrowserCreated(content::RenderView* render_view,
                      content::RenderFrame* render_frame);

  // Perform cleanup work for single-process mode.
  void RunSingleProcessCleanupOnUIThread();

  scoped_refptr<base::SequencedTaskRunner> render_task_runner_;
  scoped_ptr<CefRenderProcessObserver> observer_;
  scoped_ptr<web_cache::WebCacheRenderProcessObserver> web_cache_observer_;
  scoped_ptr<SpellCheck> spellcheck_;

  // Map of RenderView pointers to CefBrowserImpl references.
  typedef std::map<content::RenderView*, CefRefPtr<CefBrowserImpl> > BrowserMap;
  BrowserMap browsers_;

  // Cross-origin white list entries that need to be registered with WebKit.
  typedef std::vector<Cef_CrossOriginWhiteListEntry_Params> CrossOriginList;
  CrossOriginList cross_origin_whitelist_entries_;

  int devtools_agent_count_;
  int uncaught_exception_stack_size_;

  // Map of worker thread IDs to task runners. Access must be protected by
  // |worker_task_runner_lock_|.
  typedef std::map<int, scoped_refptr<base::SequencedTaskRunner> >
      WorkerTaskRunnerMap;
  WorkerTaskRunnerMap worker_task_runner_map_;
  base::Lock worker_task_runner_lock_;

  // Used in single-process mode to test when cleanup is complete.
  // Access must be protected by |single_process_cleanup_lock_|.
  bool single_process_cleanup_complete_;
  base::Lock single_process_cleanup_lock_;
};

#endif  // CEF_LIBCEF_RENDERER_CONTENT_RENDERER_CLIENT_H_
