// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/browser/url_request_context_getter_impl.h"

#if defined(OS_WIN)
#include <winhttp.h>
#endif
#include <string>
#include <vector>

#include "libcef/browser/scheme_handler.h"
#include "libcef/browser/thread_util.h"
#include "libcef/browser/url_network_delegate.h"
#include "libcef/browser/url_request_interceptor.h"
#include "libcef/common/cef_switches.h"
#include "libcef/common/content_client.h"

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/worker_pool.h"
#include "chrome/browser/net/proxy_service_factory.h"
#include "content/browser/net/sqlite_persistent_cookie_store.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/url_constants.h"
#include "net/cert/cert_verifier.h"
#include "net/cookies/cookie_monster.h"
#include "net/dns/host_resolver.h"
#include "net/ftp/ftp_network_layer.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/http_cache.h"
#include "net/http/http_server_properties_impl.h"
#include "net/http/http_util.h"
#include "net/http/transport_security_state.h"
#include "net/proxy/proxy_service.h"
#include "net/ssl/channel_id_service.h"
#include "net/ssl/default_channel_id_store.h"
#include "net/ssl/ssl_config_service_defaults.h"
#include "url/url_constants.h"
#include "net/url_request/http_user_agent_settings.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_storage.h"
#include "net/url_request/url_request_intercepting_job_factory.h"
#include "net/url_request/url_request_job_factory_impl.h"
#include "net/url_request/url_request_job_manager.h"

#if defined(USE_NSS)
#include "net/ocsp/nss_ocsp.h"
#endif

using content::BrowserThread;

#if defined(OS_WIN)
#pragma comment(lib, "winhttp.lib")
#endif

namespace {

// An implementation of |HttpUserAgentSettings| that provides a static
// HTTP Accept-Language header value and uses |content::GetUserAgent|
// to provide the HTTP User-Agent header value.
class CefHttpUserAgentSettings : public net::HttpUserAgentSettings {
 public:
  explicit CefHttpUserAgentSettings(const std::string& raw_language_list)
      : http_accept_language_(net::HttpUtil::GenerateAcceptLanguageHeader(
            raw_language_list)) {
    CEF_REQUIRE_IOT();
  }

  // net::HttpUserAgentSettings implementation
  std::string GetAcceptLanguage() const override {
    CEF_REQUIRE_IOT();
    return http_accept_language_;
  }

  std::string GetUserAgent() const override {
    CEF_REQUIRE_IOT();
    return CefContentClient::Get()->GetUserAgent();
  }

 private:
  const std::string http_accept_language_;

  DISALLOW_COPY_AND_ASSIGN(CefHttpUserAgentSettings);
};

}  // namespace

CefURLRequestContextGetterImpl::CefURLRequestContextGetterImpl(
    const CefRequestContextSettings& settings,
    base::MessageLoop* io_loop,
    base::MessageLoop* file_loop,
    content::ProtocolHandlerMap* protocol_handlers,
    scoped_ptr<net::ProxyConfigService> proxy_config_service,
    content::URLRequestInterceptorScopedVector request_interceptors)
    : settings_(settings),
      io_loop_(io_loop),
      file_loop_(file_loop),
      proxy_config_service_(proxy_config_service.Pass()),
      request_interceptors_(request_interceptors.Pass()) {
  // Must first be created on the UI thread.
  CEF_REQUIRE_UIT();

  std::swap(protocol_handlers_, *protocol_handlers);
}

CefURLRequestContextGetterImpl::~CefURLRequestContextGetterImpl() {
  CEF_REQUIRE_IOT();

  // Delete the ProxyService object here so that any pending requests will be
  // canceled before the associated URLRequestContext is destroyed in this
  // object's destructor.
  storage_->set_proxy_service(NULL);
}

net::URLRequestContext* CefURLRequestContextGetterImpl::GetURLRequestContext() {
  CEF_REQUIRE_IOT();

  if (!url_request_context_.get()) {
    const base::CommandLine* command_line =
        base::CommandLine::ForCurrentProcess();

    base::FilePath cache_path;
    if (settings_.cache_path.length > 0)
      cache_path = base::FilePath(CefString(&settings_.cache_path));

    url_request_context_.reset(new CefURLRequestContextImpl());
    storage_.reset(
        new net::URLRequestContextStorage(url_request_context_.get()));

    SetCookieStoragePath(cache_path,
                         settings_.persist_session_cookies ? true : false);

    storage_->set_network_delegate(new CefNetworkDelegate);

    storage_->set_channel_id_service(make_scoped_ptr(
        new net::ChannelIDService(
            new net::DefaultChannelIDStore(NULL),
            base::WorkerPool::GetTaskRunner(true))));

    const std::string& accept_language =
        settings_.accept_language_list.length > 0 ?
            CefString(&settings_.accept_language_list): "en-US,en";
    storage_->set_http_user_agent_settings(
        new CefHttpUserAgentSettings(accept_language));

    storage_->set_host_resolver(net::HostResolver::CreateDefaultResolver(NULL));
    storage_->set_cert_verifier(net::CertVerifier::CreateDefault());
    storage_->set_transport_security_state(new net::TransportSecurityState);

    scoped_ptr<net::ProxyService> system_proxy_service;
    system_proxy_service.reset(
        ProxyServiceFactory::CreateProxyService(
            NULL,
            url_request_context_.get(),
            url_request_context_->network_delegate(),
            proxy_config_service_.release(),
            *command_line,
            true));
    storage_->set_proxy_service(system_proxy_service.release());

    storage_->set_ssl_config_service(new net::SSLConfigServiceDefaults);

    // Add support for single sign-on.
    url_security_manager_.reset(net::URLSecurityManager::Create(NULL, NULL));

    std::vector<std::string> supported_schemes;
    supported_schemes.push_back("basic");
    supported_schemes.push_back("digest");
    supported_schemes.push_back("ntlm");
    supported_schemes.push_back("negotiate");

    storage_->set_http_auth_handler_factory(
        net::HttpAuthHandlerRegistryFactory::Create(
            supported_schemes,
            url_security_manager_.get(),
            url_request_context_->host_resolver(),
            std::string(),
            false,
            false));
    storage_->set_http_server_properties(
        make_scoped_ptr<net::HttpServerProperties>(
            new net::HttpServerPropertiesImpl));

    net::HttpCache::DefaultBackend* main_backend =
        new net::HttpCache::DefaultBackend(
            cache_path.empty() ? net::MEMORY_CACHE : net::DISK_CACHE,
            net::CACHE_BACKEND_DEFAULT,
            cache_path,
            0,
            BrowserThread::GetMessageLoopProxyForThread(
                BrowserThread::CACHE));

    net::HttpNetworkSession::Params network_session_params;
    network_session_params.host_resolver =
        url_request_context_->host_resolver();
    network_session_params.cert_verifier =
        url_request_context_->cert_verifier();
    network_session_params.transport_security_state =
        url_request_context_->transport_security_state();
    network_session_params.channel_id_service =
        url_request_context_->channel_id_service();
    network_session_params.proxy_service =
        url_request_context_->proxy_service();
    network_session_params.ssl_config_service =
        url_request_context_->ssl_config_service();
    network_session_params.http_auth_handler_factory =
        url_request_context_->http_auth_handler_factory();
    network_session_params.network_delegate =
        url_request_context_->network_delegate();
    network_session_params.http_server_properties =
        url_request_context_->http_server_properties();
    network_session_params.ignore_certificate_errors =
        settings_.ignore_certificate_errors ? true : false;

    net::HttpCache* main_cache = new net::HttpCache(network_session_params,
                                                    main_backend);
    storage_->set_http_transaction_factory(main_cache);

#if !defined(DISABLE_FTP_SUPPORT)
    ftp_transaction_factory_.reset(
        new net::FtpNetworkLayer(network_session_params.host_resolver));
#endif

    scoped_ptr<net::URLRequestJobFactoryImpl> job_factory(
        new net::URLRequestJobFactoryImpl());
    url_request_manager_.reset(new CefURLRequestManager(job_factory.get()));

    // Install internal scheme handlers that cannot be overridden.
    scheme::InstallInternalProtectedHandlers(job_factory.get(),
                                             url_request_manager_.get(),
                                             &protocol_handlers_,
                                             ftp_transaction_factory_.get());
    protocol_handlers_.clear();

    // Register internal scheme handlers that can be overridden.
    scheme::RegisterInternalHandlers(url_request_manager_.get());

    request_interceptors_.push_back(new CefRequestInterceptor());

    // Set up interceptors in the reverse order.
    scoped_ptr<net::URLRequestJobFactory> top_job_factory =
        job_factory.Pass();
    for (content::URLRequestInterceptorScopedVector::reverse_iterator i =
             request_interceptors_.rbegin();
         i != request_interceptors_.rend();
         ++i) {
      top_job_factory.reset(new net::URLRequestInterceptingJobFactory(
          top_job_factory.Pass(), make_scoped_ptr(*i)));
    }
    request_interceptors_.weak_clear();

    storage_->set_job_factory(top_job_factory.release());

#if defined(USE_NSS)
    // Only do this for the first (global) request context.
    static bool request_context_for_nss_set = false;
    if (!request_context_for_nss_set) {
      net::SetURLRequestContextForNSSHttpIO(url_request_context_.get());
      request_context_for_nss_set = true;
    }
#endif
  }

  return url_request_context_.get();
}

scoped_refptr<base::SingleThreadTaskRunner>
    CefURLRequestContextGetterImpl::GetNetworkTaskRunner() const {
  return BrowserThread::GetMessageLoopProxyForThread(CEF_IOT);
}

net::HostResolver* CefURLRequestContextGetterImpl::GetHostResolver() const {
  return url_request_context_->host_resolver();
}

void CefURLRequestContextGetterImpl::SetCookieStoragePath(
    const base::FilePath& path,
    bool persist_session_cookies) {
  CEF_REQUIRE_IOT();

  if (url_request_context_->cookie_store() &&
      ((cookie_store_path_.empty() && path.empty()) ||
       cookie_store_path_ == path)) {
    // The path has not changed so don't do anything.
    return;
  }

  scoped_refptr<content::SQLitePersistentCookieStore> persistent_store;
  if (!path.empty()) {
    // TODO(cef): Move directory creation to the blocking pool instead of
    // allowing file IO on this thread.
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    if (base::DirectoryExists(path) ||
        base::CreateDirectory(path)) {
      const base::FilePath& cookie_path = path.AppendASCII("Cookies");
      persistent_store =
          new content::SQLitePersistentCookieStore(
              cookie_path,
              BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO),
              BrowserThread::GetMessageLoopProxyForThread(BrowserThread::DB),
              persist_session_cookies,
              NULL,
              NULL);
    } else {
      NOTREACHED() << "The cookie storage directory could not be created";
    }
  }

  // Set the new cookie store that will be used for all new requests. The old
  // cookie store, if any, will be automatically flushed and closed when no
  // longer referenced.
  scoped_refptr<net::CookieMonster> cookie_monster =
      new net::CookieMonster(persistent_store.get(), NULL);
  storage_->set_cookie_store(cookie_monster.get());
  if (persistent_store.get() && persist_session_cookies)
      cookie_monster->SetPersistSessionCookies(true);
  cookie_store_path_ = path;

  // Restore the previously supported schemes.
  SetCookieSupportedSchemes(cookie_supported_schemes_);
}

void CefURLRequestContextGetterImpl::SetCookieSupportedSchemes(
    const std::vector<std::string>& schemes) {
  CEF_REQUIRE_IOT();

  cookie_supported_schemes_ = schemes;

  if (cookie_supported_schemes_.empty()) {
    cookie_supported_schemes_.push_back("http");
    cookie_supported_schemes_.push_back("https");
  }

  std::set<std::string> scheme_set;
  std::vector<std::string>::const_iterator it =
      cookie_supported_schemes_.begin();
  for (; it != cookie_supported_schemes_.end(); ++it)
    scheme_set.insert(*it);

  const char** arr = new const char*[scheme_set.size()];
  std::set<std::string>::const_iterator it2 = scheme_set.begin();
  for (int i = 0; it2 != scheme_set.end(); ++it2, ++i)
    arr[i] = it2->c_str();

  url_request_context_->cookie_store()->GetCookieMonster()->
      SetCookieableSchemes(arr, scheme_set.size());

  delete [] arr;
}

void CefURLRequestContextGetterImpl::AddHandler(
    CefRefPtr<CefRequestContextHandler> handler) {
  if (!CEF_CURRENTLY_ON_IOT()) {
    CEF_POST_TASK(CEF_IOT,
        base::Bind(&CefURLRequestContextGetterImpl::AddHandler, this, handler));
    return;
  }
  handler_list_.push_back(handler);
}

void CefURLRequestContextGetterImpl::CreateProxyConfigService() {
  if (proxy_config_service_.get())
    return;

  proxy_config_service_.reset(
      net::ProxyService::CreateSystemProxyConfigService(
          io_loop_->message_loop_proxy(), file_loop_->message_loop_proxy()));
}
