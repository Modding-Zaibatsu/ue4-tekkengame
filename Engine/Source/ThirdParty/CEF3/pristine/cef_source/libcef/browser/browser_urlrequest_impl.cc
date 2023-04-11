// Copyright (c) 2012 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include "libcef/browser/browser_urlrequest_impl.h"

#include <string>

#include "libcef/browser/browser_context.h"
#include "libcef/browser/content_browser_client.h"
#include "libcef/browser/request_context_impl.h"
#include "libcef/browser/thread_util.h"
#include "libcef/browser/url_request_user_data.h"
#include "libcef/common/http_header_utils.h"
#include "libcef/common/request_impl.h"
#include "libcef/common/response_impl.h"

#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/url_fetcher.h"
#include "net/base/io_buffer.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_fetcher_response_writer.h"
#include "net/url_request/url_request_status.h"

using content::BrowserThread;

namespace {

class CefURLFetcherDelegate : public net::URLFetcherDelegate {
 public:
  CefURLFetcherDelegate(CefBrowserURLRequest::Context* context,
                        int request_flags);
  ~CefURLFetcherDelegate() override;

  // net::URLFetcherDelegate methods.
  void OnURLFetchComplete(const net::URLFetcher* source) override;
  void OnURLFetchDownloadProgress(const net::URLFetcher* source,
                                  int64 current, int64 total) override;
  void OnURLFetchUploadProgress(const net::URLFetcher* source,
                                int64 current, int64 total) override;

 private:
  // The context_ pointer will outlive this object.
  CefBrowserURLRequest::Context* context_;
  int request_flags_;
};

class NET_EXPORT CefURLFetcherResponseWriter :
    public net::URLFetcherResponseWriter {
 public:
  CefURLFetcherResponseWriter(
      CefRefPtr<CefBrowserURLRequest> url_request,
      scoped_refptr<base::MessageLoopProxy> message_loop_proxy)
      : url_request_(url_request),
        message_loop_proxy_(message_loop_proxy) {
  }

  // net::URLFetcherResponseWriter methods.
  int Initialize(const net::CompletionCallback& callback) override {
    return net::OK;
  }

  int Write(net::IOBuffer* buffer,
            int num_bytes,
            const net::CompletionCallback& callback) override {
    if (url_request_.get()) {
      message_loop_proxy_->PostTask(FROM_HERE,
          base::Bind(&CefURLFetcherResponseWriter::WriteOnClientThread,
                     url_request_, scoped_refptr<net::IOBuffer>(buffer),
                     num_bytes, callback,
                     base::MessageLoop::current()->message_loop_proxy()));
      return net::ERR_IO_PENDING;
    }
    return num_bytes;
  }

  int Finish(const net::CompletionCallback& callback) override {
    if (url_request_.get())
      url_request_ = NULL;
    return net::OK;
  }

 private:
  static void WriteOnClientThread(
      CefRefPtr<CefBrowserURLRequest> url_request,
      scoped_refptr<net::IOBuffer> buffer,
      int num_bytes,
      const net::CompletionCallback& callback,
      scoped_refptr<base::MessageLoopProxy> source_message_loop_proxy) {
    CefRefPtr<CefURLRequestClient> client = url_request->GetClient();
    if (client.get())
      client->OnDownloadData(url_request.get(), buffer->data(), num_bytes);

    source_message_loop_proxy->PostTask(FROM_HERE,
        base::Bind(&CefURLFetcherResponseWriter::ContinueOnSourceThread,
                   num_bytes, callback));
  }

  static void ContinueOnSourceThread(
      int num_bytes,
      const net::CompletionCallback& callback) {
    callback.Run(num_bytes);
  }

  CefRefPtr<CefBrowserURLRequest> url_request_;
  scoped_refptr<base::MessageLoopProxy> message_loop_proxy_;

  DISALLOW_COPY_AND_ASSIGN(CefURLFetcherResponseWriter);
};

base::SupportsUserData::Data* CreateURLRequestUserData(
    CefRefPtr<CefURLRequestClient> client) {
  return new CefURLRequestUserData(client);
}

}  // namespace


// CefBrowserURLRequest::Context ----------------------------------------------

class CefBrowserURLRequest::Context
    : public base::RefCountedThreadSafe<CefBrowserURLRequest::Context> {
 public:
  Context(CefRefPtr<CefBrowserURLRequest> url_request,
          CefRefPtr<CefRequest> request,
          CefRefPtr<CefURLRequestClient> client,
          CefRefPtr<CefRequestContext> request_context)
    : url_request_(url_request),
      request_(request),
      client_(client),
      request_context_(request_context),
      message_loop_proxy_(base::MessageLoop::current()->message_loop_proxy()),
    status_(UR_IO_PENDING),
    error_code_(ERR_NONE),
    upload_data_size_(0),
    got_upload_progress_complete_(false) {
    // Mark the request as read-only.
    static_cast<CefRequestImpl*>(request_.get())->SetReadOnly(true);
  }

  inline bool CalledOnValidThread() {
    return message_loop_proxy_->BelongsToCurrentThread();
  }

  bool Start() {
    DCHECK(CalledOnValidThread());

    GURL url = GURL(request_->GetURL().ToString());
    if (!url.is_valid())
      return false;

    std::string method = request_->GetMethod();
    base::StringToLowerASCII(&method);
    net::URLFetcher::RequestType request_type = net::URLFetcher::GET;
    if (LowerCaseEqualsASCII(method, "get")) {
    } else if (LowerCaseEqualsASCII(method, "post")) {
      request_type = net::URLFetcher::POST;
    } else if (LowerCaseEqualsASCII(method, "head")) {
      request_type = net::URLFetcher::HEAD;
    } else if (LowerCaseEqualsASCII(method, "delete")) {
      request_type = net::URLFetcher::DELETE_REQUEST;
    } else if (LowerCaseEqualsASCII(method, "put")) {
      request_type = net::URLFetcher::PUT;
    } else {
      NOTREACHED() << "invalid request type";
      return false;
    }

    BrowserThread::PostTaskAndReply(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&CefBrowserURLRequest::Context::GetRequestContextOnUIThread,
                 this),
      base::Bind(&CefBrowserURLRequest::Context::ContinueOnOriginatingThread,
                 this, url, request_type));

    return true;
  }

  void GetRequestContextOnUIThread() {
    CEF_REQUIRE_UIT();

    // Get or create the request context and browser context.
    CefRefPtr<CefRequestContextImpl> request_context_impl =
        CefRequestContextImpl::GetForRequestContext(request_context_);
    DCHECK(request_context_impl.get());
    scoped_refptr<CefBrowserContext> browser_context =
        request_context_impl->GetBrowserContext();
    DCHECK(browser_context.get());

    if (!request_context_.get())
      request_context_ = request_context_impl.get();

    // The request context is created on the UI thread but accessed and
    // destroyed on the IO thread.
    url_request_getter_ = browser_context->GetRequestContext();
  }

  void ContinueOnOriginatingThread(const GURL& url,
                                   net::URLFetcher::RequestType request_type) {
    DCHECK(CalledOnValidThread());

    fetcher_delegate_.reset(
        new CefURLFetcherDelegate(this, request_->GetFlags()));

    fetcher_.reset(net::URLFetcher::Create(url, request_type,
                                           fetcher_delegate_.get()));

    DCHECK(url_request_getter_.get());
    fetcher_->SetRequestContext(url_request_getter_.get());

    CefRequest::HeaderMap headerMap;
    request_->GetHeaderMap(headerMap);

    // Extract the Referer header value.
    {
      CefString referrerStr;
      referrerStr.FromASCII(net::HttpRequestHeaders::kReferer);
      CefRequest::HeaderMap::iterator it = headerMap.find(referrerStr);
      if (it == headerMap.end()) {
        fetcher_->SetReferrer("");
      } else {
        fetcher_->SetReferrer(it->second);
        headerMap.erase(it);
      }
    }

    std::string content_type;

    // Extract the Content-Type header value.
    {
      CefString contentTypeStr;
      contentTypeStr.FromASCII(net::HttpRequestHeaders::kContentType);
      CefRequest::HeaderMap::iterator it = headerMap.find(contentTypeStr);
      if (it != headerMap.end()) {
        content_type = it->second;
        headerMap.erase(it);
      }
    }

    int64 upload_data_size = 0;

    CefRefPtr<CefPostData> post_data = request_->GetPostData();
    if (post_data.get()) {
      CefPostData::ElementVector elements;
      post_data->GetElements(elements);
      if (elements.size() == 1) {
        // Default to URL encoding if not specified.
        if (content_type.empty())
          content_type = "application/x-www-form-urlencoded";

        CefPostDataElementImpl* impl =
            static_cast<CefPostDataElementImpl*>(elements[0].get());

        switch (elements[0]->GetType())
          case PDE_TYPE_BYTES: {
            upload_data_size = impl->GetBytesCount();
            fetcher_->SetUploadData(content_type,
                std::string(static_cast<char*>(impl->GetBytes()),
                            upload_data_size));
            break;
          case PDE_TYPE_FILE:
            fetcher_->SetUploadFilePath(
                content_type, 
                base::FilePath(impl->GetFile()),
                0, kuint64max,
                content::BrowserThread::GetMessageLoopProxyForThread(
                    content::BrowserThread::FILE).get());
            break;
          case PDE_TYPE_EMPTY:
            break;
        }
      } else if (elements.size() > 1) {
        NOTIMPLEMENTED() << " multi-part form data is not supported";
      }
    }

    std::string first_party_for_cookies = request_->GetFirstPartyForCookies();
    if (!first_party_for_cookies.empty())
      fetcher_->SetFirstPartyForCookies(GURL(first_party_for_cookies));

    int cef_flags = request_->GetFlags();

    if (cef_flags & UR_FLAG_NO_RETRY_ON_5XX)
      fetcher_->SetAutomaticallyRetryOn5xx(false);

    int load_flags = 0;

    if (cef_flags & UR_FLAG_SKIP_CACHE)
      load_flags |= net::LOAD_BYPASS_CACHE;

    if (!(cef_flags & UR_FLAG_ALLOW_CACHED_CREDENTIALS)) {
      load_flags |= net::LOAD_DO_NOT_SEND_AUTH_DATA;
      load_flags |= net::LOAD_DO_NOT_SEND_COOKIES;
      load_flags |= net::LOAD_DO_NOT_SAVE_COOKIES;
    }

    if (cef_flags & UR_FLAG_REPORT_UPLOAD_PROGRESS) {
      upload_data_size_ = upload_data_size;
    }

    if (cef_flags & UR_FLAG_REPORT_RAW_HEADERS)
      load_flags |= net::LOAD_REPORT_RAW_HEADERS;

    fetcher_->SetLoadFlags(load_flags);

    fetcher_->SetExtraRequestHeaders(
        HttpHeaderUtils::GenerateHeaders(headerMap));

    fetcher_->SetURLRequestUserData(
        CefURLRequestUserData::kUserDataKey,
        base::Bind(&CreateURLRequestUserData, client_));

    scoped_ptr<net::URLFetcherResponseWriter> response_writer;
    if (cef_flags & UR_FLAG_NO_DOWNLOAD_DATA) {
      response_writer.reset(new CefURLFetcherResponseWriter(NULL, NULL));
    } else {
      response_writer.reset(
          new CefURLFetcherResponseWriter(url_request_, message_loop_proxy_));
    }
    fetcher_->SaveResponseWithWriter(response_writer.Pass());

    fetcher_->Start();
  }

  void Cancel() {
    DCHECK(CalledOnValidThread());

    // The request may already be complete.
    if (!fetcher_.get())
      return;

    // Cancel the fetch by deleting the fetcher.
    fetcher_.reset(NULL);

    status_ = UR_CANCELED;
    error_code_ = ERR_ABORTED;
    OnComplete();
  }

  void OnComplete() {
    DCHECK(CalledOnValidThread());

    if (fetcher_.get()) {
      const net::URLRequestStatus& status = fetcher_->GetStatus();

      if (status.is_success())
        NotifyUploadProgressIfNecessary();

      switch (status.status()) {
        case net::URLRequestStatus::SUCCESS:
          status_ = UR_SUCCESS;
          break;
        case net::URLRequestStatus::IO_PENDING:
          status_ = UR_IO_PENDING;
          break;
        case net::URLRequestStatus::CANCELED:
          status_ = UR_CANCELED;
          break;
        case net::URLRequestStatus::FAILED:
          status_ = UR_FAILED;
          break;
      }

      error_code_ = static_cast<CefURLRequest::ErrorCode>(status.error());

      if(!response_.get())
        OnResponse();
    }

    DCHECK(url_request_.get());
    client_->OnRequestComplete(url_request_.get());

    if (fetcher_.get())
      fetcher_.reset(NULL);

    // This may result in the Context object being deleted.
    url_request_ = NULL;
  }

  void OnDownloadProgress(int64 current, int64 total) {
    DCHECK(CalledOnValidThread());
    DCHECK(url_request_.get());

    if(!response_.get())
      OnResponse();

    NotifyUploadProgressIfNecessary();

    client_->OnDownloadProgress(url_request_.get(), current, total);
  }

  void OnDownloadData(scoped_ptr<std::string> download_data) {
    DCHECK(CalledOnValidThread());
    DCHECK(url_request_.get());

    if(!response_.get())
      OnResponse();

    client_->OnDownloadData(url_request_.get(), download_data->c_str(),
        download_data->length());
  }

  void OnUploadProgress(int64 current, int64 total) {
    DCHECK(CalledOnValidThread());
    DCHECK(url_request_.get());
    if (current == total)
      got_upload_progress_complete_ = true;
    client_->OnUploadProgress(url_request_.get(), current, total);
  }

  CefRefPtr<CefRequest> request() { return request_; }
  CefRefPtr<CefURLRequestClient> client() { return client_; }
  CefURLRequest::Status status() { return status_; }
  CefURLRequest::ErrorCode error_code() { return error_code_; }
  CefRefPtr<CefResponse> response() { return response_; }

 private:
  friend class base::RefCountedThreadSafe<CefBrowserURLRequest::Context>;

  ~Context() {
    if (fetcher_.get()) {
      // Delete the fetcher object on the thread that created it.
      message_loop_proxy_->DeleteSoon(FROM_HERE, fetcher_.release());
    }
  }

  void NotifyUploadProgressIfNecessary() {
    if (!got_upload_progress_complete_ && upload_data_size_ > 0) {
      // URLFetcher sends upload notifications using a timer and will not send
      // a notification if the request completes too quickly. We therefore
      // send the notification here if necessary.
      client_->OnUploadProgress(url_request_.get(), upload_data_size_,
                                upload_data_size_);
      got_upload_progress_complete_ = true;
    }
  }

  void OnResponse() {
    if (fetcher_.get()) {  
      response_ = new CefResponseImpl();
      CefResponseImpl* responseImpl =
          static_cast<CefResponseImpl*>(response_.get());

      net::HttpResponseHeaders* headers = fetcher_->GetResponseHeaders();
      if (headers)
        responseImpl->SetResponseHeaders(*headers);

      responseImpl->SetReadOnly(true);
    }
  }

  // Members only accessed on the initialization thread.
  CefRefPtr<CefBrowserURLRequest> url_request_;
  CefRefPtr<CefRequest> request_;
  CefRefPtr<CefURLRequestClient> client_;
  CefRefPtr<CefRequestContext> request_context_;
  scoped_refptr<base::MessageLoopProxy> message_loop_proxy_;
  scoped_ptr<net::URLFetcher> fetcher_;
  scoped_ptr<CefURLFetcherDelegate> fetcher_delegate_;
  CefURLRequest::Status status_;
  CefURLRequest::ErrorCode error_code_;
  CefRefPtr<CefResponse> response_;
  int64 upload_data_size_;
  bool got_upload_progress_complete_;

  scoped_refptr<net::URLRequestContextGetter> url_request_getter_;
};


// CefURLFetcherDelegate ------------------------------------------------------

namespace {

CefURLFetcherDelegate::CefURLFetcherDelegate(
    CefBrowserURLRequest::Context* context, int request_flags)
  : context_(context),
    request_flags_(request_flags) {
}

CefURLFetcherDelegate::~CefURLFetcherDelegate() {
}

void CefURLFetcherDelegate::OnURLFetchComplete(
    const net::URLFetcher* source) {
  // Complete asynchronously so as not to delete the URLFetcher while it's still
  // in the call stack.
  base::MessageLoop::current()->PostTask(FROM_HERE,
      base::Bind(&CefBrowserURLRequest::Context::OnComplete, context_));
}

void CefURLFetcherDelegate::OnURLFetchDownloadProgress(
    const net::URLFetcher* source,
    int64 current, int64 total) {
  context_->OnDownloadProgress(current, total);
}

void CefURLFetcherDelegate::OnURLFetchUploadProgress(
    const net::URLFetcher* source,
    int64 current, int64 total) {
  if (request_flags_ & UR_FLAG_REPORT_UPLOAD_PROGRESS)
    context_->OnUploadProgress(current, total);
}

}  // namespace


// CefBrowserURLRequest -------------------------------------------------------

CefBrowserURLRequest::CefBrowserURLRequest(
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefURLRequestClient> client,
    CefRefPtr<CefRequestContext> request_context) {
  context_ = new Context(this, request, client, request_context);
}

CefBrowserURLRequest::~CefBrowserURLRequest() {
}

bool CefBrowserURLRequest::Start() {
  if (!VerifyContext())
    return false;
  return context_->Start();
}

CefRefPtr<CefRequest> CefBrowserURLRequest::GetRequest() {
  if (!VerifyContext())
    return NULL;
  return context_->request();
}

CefRefPtr<CefURLRequestClient> CefBrowserURLRequest::GetClient() {
  if (!VerifyContext())
    return NULL;
  return context_->client();
}

CefURLRequest::Status CefBrowserURLRequest::GetRequestStatus() {
  if (!VerifyContext())
    return UR_UNKNOWN;
  return context_->status();
}

CefURLRequest::ErrorCode CefBrowserURLRequest::GetRequestError() {
  if (!VerifyContext())
    return ERR_NONE;
  return context_->error_code();
}

CefRefPtr<CefResponse> CefBrowserURLRequest::GetResponse() {
  if (!VerifyContext())
    return NULL;
  return context_->response();
}

void CefBrowserURLRequest::Cancel() {
  if (!VerifyContext())
    return;
  return context_->Cancel();
}

bool CefBrowserURLRequest::VerifyContext() {
  DCHECK(context_.get());
  if (!context_->CalledOnValidThread()) {
    NOTREACHED() << "called on invalid thread";
    return false;
  }

  return true;
}
