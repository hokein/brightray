// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#include "browser/url_request_context_getter.h"

#include <algorithm>

#include "browser/network_delegate.h"

#include "base/strings/string_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/worker_pool.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/cookie_store_factory.h"
#include "content/public/common/url_constants.h"
#include "net/cert/cert_verifier.h"
#include "net/cookies/cookie_monster.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/http_cache.h"
#include "net/http/http_server_properties_impl.h"
#include "net/proxy/dhcp_proxy_script_fetcher_factory.h"
#include "net/proxy/proxy_config_service.h"
#include "net/proxy/proxy_script_fetcher_impl.h"
#include "net/proxy/proxy_service.h"
#include "net/proxy/proxy_service_v8.h"
#include "net/ssl/default_server_bound_cert_store.h"
#include "net/ssl/server_bound_cert_service.h"
#include "net/ssl/ssl_config_service_defaults.h"
#include "net/url_request/data_protocol_handler.h"
#include "net/url_request/file_protocol_handler.h"
#include "net/url_request/protocol_intercept_job_factory.h"
#include "net/url_request/static_http_user_agent_settings.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_storage.h"
#include "net/url_request/url_request_job_factory_impl.h"
#include "webkit/browser/quota/special_storage_policy.h"

namespace brightray {

URLRequestContextGetter::URLRequestContextGetter(
    const base::FilePath& base_path,
    base::MessageLoop* io_loop,
    base::MessageLoop* file_loop,
    base::Callback<scoped_ptr<NetworkDelegate>(void)> network_delegate_factory,
    content::ProtocolHandlerMap* protocol_handlers,
    content::ProtocolHandlerScopedVector protocol_interceptors)
    : base_path_(base_path),
      io_loop_(io_loop),
      file_loop_(file_loop),
      network_delegate_factory_(network_delegate_factory),
      protocol_interceptors_(protocol_interceptors.Pass()) {
  // Must first be created on the UI thread.
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  std::swap(protocol_handlers_, *protocol_handlers);

  proxy_config_service_.reset(net::ProxyService::CreateSystemProxyConfigService(
      io_loop_->message_loop_proxy(), file_loop_));
}

URLRequestContextGetter::~URLRequestContextGetter() {
}

net::HostResolver* URLRequestContextGetter::host_resolver() {
  return url_request_context_->host_resolver();
}

net::URLRequestContext* URLRequestContextGetter::GetURLRequestContext() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  if (!url_request_context_.get()) {
    url_request_context_.reset(new net::URLRequestContext());
    network_delegate_ = network_delegate_factory_.Run().Pass();
    url_request_context_->set_network_delegate(network_delegate_.get());
    storage_.reset(
        new net::URLRequestContextStorage(url_request_context_.get()));
    auto cookie_config = content::CookieStoreConfig(
        base_path_.Append(FILE_PATH_LITERAL("Cookies")),
        content::CookieStoreConfig::EPHEMERAL_SESSION_COOKIES,
        nullptr,
        nullptr);
    storage_->set_cookie_store(content::CreateCookieStore(cookie_config));
    storage_->set_server_bound_cert_service(new net::ServerBoundCertService(
        new net::DefaultServerBoundCertStore(NULL),
        base::WorkerPool::GetTaskRunner(true)));
    storage_->set_http_user_agent_settings(
        new net::StaticHttpUserAgentSettings(
            "en-us,en", base::EmptyString()));

    scoped_ptr<net::HostResolver> host_resolver(
        net::HostResolver::CreateDefaultResolver(NULL));

    net::DhcpProxyScriptFetcherFactory dhcp_factory;
    storage_->set_proxy_service(
        net::CreateProxyServiceUsingV8ProxyResolver(
            proxy_config_service_.release(),
            new net::ProxyScriptFetcherImpl(url_request_context_.get()),
            dhcp_factory.Create(url_request_context_.get()),
            host_resolver.get(),
            NULL,
            url_request_context_->network_delegate()));

    storage_->set_cert_verifier(net::CertVerifier::CreateDefault());
    storage_->set_transport_security_state(new net::TransportSecurityState);
    storage_->set_ssl_config_service(new net::SSLConfigServiceDefaults);
    storage_->set_http_auth_handler_factory(
        net::HttpAuthHandlerFactory::CreateDefault(host_resolver.get()));
    scoped_ptr<net::HttpServerProperties> server_properties(
        new net::HttpServerPropertiesImpl);
    storage_->set_http_server_properties(server_properties.Pass());

    base::FilePath cache_path = base_path_.Append(FILE_PATH_LITERAL("Cache"));
    net::HttpCache::DefaultBackend* main_backend =
        new net::HttpCache::DefaultBackend(
            net::DISK_CACHE,
            net::CACHE_BACKEND_DEFAULT,
            cache_path,
            0,
            content::BrowserThread::GetMessageLoopProxyForThread(
                content::BrowserThread::CACHE));

    net::HttpNetworkSession::Params network_session_params;
    network_session_params.cert_verifier =
        url_request_context_->cert_verifier();
    network_session_params.transport_security_state =
        url_request_context_->transport_security_state();
    network_session_params.server_bound_cert_service =
        url_request_context_->server_bound_cert_service();
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
    network_session_params.ignore_certificate_errors = false;

    // Give |storage_| ownership at the end in case it's |mapped_host_resolver|.
    storage_->set_host_resolver(host_resolver.Pass());
    network_session_params.host_resolver =
        url_request_context_->host_resolver();

    net::HttpCache* main_cache = new net::HttpCache(
        network_session_params, main_backend);
    storage_->set_http_transaction_factory(main_cache);

    scoped_ptr<net::URLRequestJobFactoryImpl> job_factory(
        new net::URLRequestJobFactoryImpl());
    for (auto it = protocol_handlers_.begin(),
        end = protocol_handlers_.end(); it != end; ++it) {
      bool set_protocol = job_factory->SetProtocolHandler(
          it->first, it->second.release());
      DCHECK(set_protocol);
      (void)set_protocol;  // silence unused-variable warning in Release builds on Windows
    }
    protocol_handlers_.clear();
    job_factory->SetProtocolHandler(
        content::kDataScheme, new net::DataProtocolHandler);
    job_factory->SetProtocolHandler(
        content::kFileScheme,
        new net::FileProtocolHandler(
            content::BrowserThread::GetBlockingPool()->
                GetTaskRunnerWithShutdownBehavior(
                    base::SequencedWorkerPool::SKIP_ON_SHUTDOWN)));

    // Set up interceptors in the reverse order.
    scoped_ptr<net::URLRequestJobFactory> top_job_factory =
        job_factory.PassAs<net::URLRequestJobFactory>();
    for (content::ProtocolHandlerScopedVector::reverse_iterator i =
             protocol_interceptors_.rbegin();
         i != protocol_interceptors_.rend();
         ++i) {
      top_job_factory.reset(new net::ProtocolInterceptJobFactory(
          top_job_factory.Pass(), make_scoped_ptr(*i)));
    }
    protocol_interceptors_.weak_clear();

    storage_->set_job_factory(top_job_factory.release());
  }

  return url_request_context_.get();
}

scoped_refptr<base::SingleThreadTaskRunner>
    URLRequestContextGetter::GetNetworkTaskRunner() const {
  return content::BrowserThread::GetMessageLoopProxyForThread(
      content::BrowserThread::IO);
}

}  // namespace brightray
