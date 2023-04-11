// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/browser/browser_context.h"
#include "libcef/browser/content_browser_client.h"

#include "base/logging.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_thread.h"

#ifndef NDEBUG
base::AtomicRefCount CefBrowserContext::DebugObjCt = 0;
#endif

CefBrowserContext::CefBrowserContext()
    : resource_context_(new CefResourceContext) {
  BrowserContextDependencyManager::GetInstance()->CreateBrowserContextServices(
      this);

  // Spell checking support and possibly other subsystems retrieve the
  // PrefService associated with a BrowserContext via UserPrefs::Get().
  PrefService* pref_service = CefContentBrowserClient::Get()->pref_service();
  DCHECK(pref_service);
  user_prefs::UserPrefs::Set(this, pref_service);

#ifndef NDEBUG
  base::AtomicRefCountInc(&DebugObjCt);
#endif
}

CefBrowserContext::~CefBrowserContext() {
  if (resource_context_.get()) {
    // Destruction of the ResourceContext will trigger destruction of all
    // associated URLRequests.
    content::BrowserThread::DeleteSoon(
        content::BrowserThread::IO, FROM_HERE, resource_context_.release());
  }

  // Remove any BrowserContextKeyedServiceFactory associations.
  BrowserContextDependencyManager::GetInstance()->DestroyBrowserContextServices(
      this);

#ifndef NDEBUG
  base::AtomicRefCountDec(&DebugObjCt);
#endif
}

content::ResourceContext* CefBrowserContext::GetResourceContext() {
  return resource_context_.get();
}
