// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#ifndef BRIGHTRAY_BROWSER_BROWSER_CLIENT_H_
#define BRIGHTRAY_BROWSER_BROWSER_CLIENT_H_

#include "content/public/browser/content_browser_client.h"

namespace brightray {

class BrowserContext;
class BrowserMainParts;
class NotificationPresenter;

class BrowserClient : public content::ContentBrowserClient {
 public:
  static BrowserClient* Get();

  BrowserClient();
  ~BrowserClient();

  BrowserContext* browser_context();
  BrowserMainParts* browser_main_parts() { return browser_main_parts_; }
  NotificationPresenter* notification_presenter();

 protected:
  // Subclasses should override this to provide their own BrowserMainParts
  // implementation. The lifetime of the returned instance is managed by the
  // caller.
  virtual BrowserMainParts* OverrideCreateBrowserMainParts(
      const content::MainFunctionParams&);

  // Subclasses that override this (e.g., to provide their own protocol
  // handlers) should call this implementation after doing their own work.
  virtual net::URLRequestContextGetter* CreateRequestContext(
      content::BrowserContext* browser_context,
      content::ProtocolHandlerMap* protocol_handlers,
      content::ProtocolHandlerScopedVector protocol_interceptors) OVERRIDE;

 private:
  virtual content::BrowserMainParts* CreateBrowserMainParts(
      const content::MainFunctionParams&) OVERRIDE;
  virtual void ShowDesktopNotification(
      const content::ShowDesktopNotificationHostMsgParams&,
      content::RenderFrameHost* render_frame_host,
      content::DesktopNotificationDelegate* delegate,
      base::Closure* cancel_callback) OVERRIDE;
  virtual content::MediaObserver* GetMediaObserver() OVERRIDE;
  virtual void GetAdditionalAllowedSchemesForFileSystem(
      std::vector<std::string>* additional_schemes) OVERRIDE;
  virtual base::FilePath GetDefaultDownloadDirectory() OVERRIDE;

  BrowserMainParts* browser_main_parts_;
  scoped_ptr<NotificationPresenter> notification_presenter_;

  DISALLOW_COPY_AND_ASSIGN(BrowserClient);
};

}  // namespace brightray

#endif
