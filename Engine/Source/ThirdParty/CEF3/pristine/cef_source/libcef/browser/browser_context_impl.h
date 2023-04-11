// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_BROWSER_CONTEXT_IMPL_H_
#define CEF_LIBCEF_BROWSER_BROWSER_CONTEXT_IMPL_H_
#pragma once

#include "libcef/browser/browser_context.h"

#include "libcef/browser/url_request_context_getter_impl.h"

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/net/pref_proxy_config_tracker.h"

namespace content {
class DownloadManagerDelegate;
class SpeechRecognitionPreferences;
}

class CefDownloadManagerDelegate;
class CefSSLHostStateDelegate;

// Isolated BrowserContext implementation. Life span is controlled by
// CefRequestContextImpl and (for the main context) CefBrowserMainParts. Only
// accessed on the UI thread unless otherwise indicated. See browser_context.h
// for an object relationship diagram.
class CefBrowserContextImpl : public CefBrowserContext {
 public:
  explicit CefBrowserContextImpl(const CefRequestContextSettings& settings);

  // Returns the existing instance, if any, associated with the specified
  // |cache_path|.
  static scoped_refptr<CefBrowserContextImpl> GetForCachePath(
      const base::FilePath& cache_path);

  // Must be called immediately after this object is created.
  void Initialize();

  // BrowserContext methods.
  base::FilePath GetPath() const override;
  scoped_ptr<content::ZoomLevelDelegate> CreateZoomLevelDelegate(
      const base::FilePath& partition_path) override;
  bool IsOffTheRecord() const override;
  content::DownloadManagerDelegate* GetDownloadManagerDelegate() override;
  net::URLRequestContextGetter* GetRequestContext() override;
  net::URLRequestContextGetter* GetRequestContextForRenderProcess(
      int renderer_child_id) override;
  net::URLRequestContextGetter* GetMediaRequestContext() override;
  net::URLRequestContextGetter* GetMediaRequestContextForRenderProcess(
      int renderer_child_id) override;
  net::URLRequestContextGetter*
      GetMediaRequestContextForStoragePartition(
          const base::FilePath& partition_path,
          bool in_memory) override;
  content::BrowserPluginGuestManager* GetGuestManager() override;
  storage::SpecialStoragePolicy* GetSpecialStoragePolicy() override;
  content::PushMessagingService* GetPushMessagingService() override;
  content::SSLHostStateDelegate* GetSSLHostStateDelegate() override;
  content::PermissionManager* GetPermissionManager() override;

  // CefBrowserContext methods.
  bool IsProxy() const override;
  const CefRequestContextSettings& GetSettings() const override;
  CefRefPtr<CefRequestContextHandler> GetHandler() const override;
  net::URLRequestContextGetter* CreateRequestContext(
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors)
      override;
  net::URLRequestContextGetter* CreateRequestContextForStoragePartition(
      const base::FilePath& partition_path,
      bool in_memory,
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors)
      override;

  // Guaranteed to exist once this object has been initialized.
  scoped_refptr<CefURLRequestContextGetterImpl> request_context() const {
    return url_request_getter_;
  }

 private:
  // Only allow deletion via scoped_refptr().
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::UI>;
  friend class base::DeleteHelper<CefBrowserContextImpl>;

  ~CefBrowserContextImpl() override;

  // Members initialized during construction are safe to access from any thread.
  CefRequestContextSettings settings_;
  base::FilePath cache_path_;

  scoped_ptr<PrefProxyConfigTracker> pref_proxy_config_tracker_;

  scoped_ptr<CefDownloadManagerDelegate> download_manager_delegate_;
  scoped_refptr<CefURLRequestContextGetterImpl> url_request_getter_;
  scoped_ptr<content::PermissionManager> permission_manager_;
  scoped_ptr<CefSSLHostStateDelegate> ssl_host_state_delegate_;

  DISALLOW_COPY_AND_ASSIGN(CefBrowserContextImpl);
};

#endif  // CEF_LIBCEF_BROWSER_BROWSER_CONTEXT_IMPL_H_
