// Copyright (c) 2014 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/browser/render_widget_host_view_osr.h"
#include "libcef/browser/browser_host_impl.h"
#include "libcef/browser/software_output_device_osr.h"
#include "libcef/browser/thread_util.h"

#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "cc/output/copy_output_request.h"
#include "cc/scheduler/delay_based_time_source.h"
#include "content/browser/bad_message.h"
#include "content/browser/compositor/image_transport_factory.h"
#include "content/browser/compositor/resize_lock.h"
#include "content/browser/renderer_host/dip_util.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/common/gpu/client/gl_helper.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/context_factory.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view_frame_subscriber.h"
#include "content/public/common/content_switches.h"
#include "third_party/WebKit/public/platform/WebScreenInfo.h"
#include "ui/gfx/geometry/dip_util.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/image/image_skia_operations.h"

namespace {

const float kDefaultScaleFactor = 1.0;

// The rate at which new calls to OnPaint will be generated.
const int kDefaultFrameRate = 30;
const int kMaximumFrameRate = 60;

// The maximum number of retry counts if frame capture fails.
const int kFrameRetryLimit = 2;

// When accelerated compositing is enabled and a widget resize is pending,
// we delay further resizes of the UI. The following constant is the maximum
// length of time that we should delay further UI resizes while waiting for a
// resized frame from a renderer.
const int kResizeLockTimeoutMs = 67;

static blink::WebScreenInfo webScreenInfoFrom(const CefScreenInfo& src) {
  blink::WebScreenInfo webScreenInfo;
  webScreenInfo.deviceScaleFactor = src.device_scale_factor;
  webScreenInfo.depth = src.depth;
  webScreenInfo.depthPerComponent = src.depth_per_component;
  webScreenInfo.isMonochrome = src.is_monochrome ? true : false;
  webScreenInfo.rect = blink::WebRect(src.rect.x, src.rect.y,
                                      src.rect.width, src.rect.height);
  webScreenInfo.availableRect = blink::WebRect(src.available_rect.x,
                                               src.available_rect.y,
                                               src.available_rect.width,
                                               src.available_rect.height);

  return webScreenInfo;
}

// Used to prevent further resizes while a resize is pending.
class CefResizeLock : public content::ResizeLock {
 public:
  CefResizeLock(CefRenderWidgetHostViewOSR* host,
                const gfx::Size new_size,
                bool defer_compositor_lock,
                double timeout)
      : ResizeLock(new_size, defer_compositor_lock),
        host_(host),
        cancelled_(false),
        weak_ptr_factory_(this) {
    DCHECK(host_);
    host_->HoldResize();

    CEF_POST_DELAYED_TASK(
        CEF_UIT,
        base::Bind(&CefResizeLock::CancelLock,
                   weak_ptr_factory_.GetWeakPtr()),
        timeout);
  }

  ~CefResizeLock() override {
    CancelLock();
  }

  bool GrabDeferredLock() override {
    return ResizeLock::GrabDeferredLock();
  }

  void UnlockCompositor() override {
    ResizeLock::UnlockCompositor();
    compositor_lock_ = NULL;
  }

 protected:
  void LockCompositor() override {
    ResizeLock::LockCompositor();
    compositor_lock_ = host_->compositor()->GetCompositorLock();
  }

  void CancelLock() {
    if (cancelled_)
      return;
    cancelled_ = true;
    UnlockCompositor();
    host_->ReleaseResize();
  }

 private:
  CefRenderWidgetHostViewOSR* host_;
  scoped_refptr<ui::CompositorLock> compositor_lock_;
  bool cancelled_;
  base::WeakPtrFactory<CefResizeLock> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(CefResizeLock);
};

}  // namespace

// Used for managing copy requests when GPU compositing is enabled. Based on
// RendererOverridesHandler::InnerSwapCompositorFrame and
// DelegatedFrameHost::CopyFromCompositingSurface.
class CefCopyFrameGenerator {
 public:
  CefCopyFrameGenerator(int frame_rate_threshold_ms,
                        CefRenderWidgetHostViewOSR* view)
    : frame_rate_threshold_ms_(frame_rate_threshold_ms),
      view_(view),
      frame_pending_(false),
      frame_in_progress_(false),
      frame_retry_count_(0),
      weak_ptr_factory_(this) {
  }

  void GenerateCopyFrame(
      bool force_frame,
      const gfx::Rect& damage_rect) {
    if (force_frame && !frame_pending_)
      frame_pending_ = true;

    // No frame needs to be generated at this time.
    if (!frame_pending_)
      return;

    // Keep track of |damage_rect| for when the next frame is generated.
    if (!damage_rect.IsEmpty())
      pending_damage_rect_.Union(damage_rect);

    // Don't attempt to generate a frame while one is currently in-progress.
    if (frame_in_progress_)
      return;
    frame_in_progress_ = true;

    // Don't exceed the frame rate threshold.
    const int64 frame_rate_delta =
        (base::TimeTicks::Now() - frame_start_time_).InMilliseconds();
    if (frame_rate_delta < frame_rate_threshold_ms_) {
      // Generate the frame after the necessary time has passed.
      CEF_POST_DELAYED_TASK(CEF_UIT,
          base::Bind(&CefCopyFrameGenerator::InternalGenerateCopyFrame,
                     weak_ptr_factory_.GetWeakPtr()),
          frame_rate_threshold_ms_ - frame_rate_delta);
      return;
    }

    InternalGenerateCopyFrame();
  }

  bool frame_pending() const { return frame_pending_; }

 private:
  void InternalGenerateCopyFrame() {
    frame_pending_ = false;
    frame_start_time_ = base::TimeTicks::Now();

    if (!view_->render_widget_host())
      return;

    const gfx::Rect damage_rect = pending_damage_rect_;
    pending_damage_rect_.SetRect(0, 0, 0, 0);

    // The below code is similar in functionality to
    // DelegatedFrameHost::CopyFromCompositingSurface but we reuse the same
    // SkBitmap in the GPU codepath and avoid scaling where possible.
    scoped_ptr<cc::CopyOutputRequest> request =
        cc::CopyOutputRequest::CreateRequest(base::Bind(
            &CefCopyFrameGenerator::CopyFromCompositingSurfaceHasResult,
            weak_ptr_factory_.GetWeakPtr(),
            damage_rect));

    request->set_area(gfx::Rect(view_->GetPhysicalBackingSize()));
    view_->DelegatedFrameHostGetLayer()->RequestCopyOfOutput(request.Pass());
  }

  void CopyFromCompositingSurfaceHasResult(
      const gfx::Rect& damage_rect,
      scoped_ptr<cc::CopyOutputResult> result) {
    if (result->IsEmpty() || result->size().IsEmpty() ||
        !view_->render_widget_host()) {
      OnCopyFrameCaptureFailure(damage_rect);
      return;
    }

    if (result->HasTexture()) {
      PrepareTextureCopyOutputResult(damage_rect, result.Pass());
      return;
    }

    DCHECK(result->HasBitmap());
    PrepareBitmapCopyOutputResult(damage_rect, result.Pass());
  }

  void PrepareTextureCopyOutputResult(
      const gfx::Rect& damage_rect,
      scoped_ptr<cc::CopyOutputResult> result) {
    DCHECK(result->HasTexture());
    base::ScopedClosureRunner scoped_callback_runner(
        base::Bind(&CefCopyFrameGenerator::OnCopyFrameCaptureFailure,
                   weak_ptr_factory_.GetWeakPtr(),
                   damage_rect));

    const gfx::Size& result_size = result->size();
    SkIRect bitmap_size;
    if (bitmap_)
      bitmap_->getBounds(&bitmap_size);

    if (!bitmap_ ||
        bitmap_size.width() != result_size.width() ||
        bitmap_size.height() != result_size.height()) {
      // Create a new bitmap if the size has changed.
      bitmap_.reset(new SkBitmap);
      bitmap_->allocN32Pixels(result_size.width(),
                              result_size.height(),
                              true);
      if (bitmap_->drawsNothing())
        return;
    }

    content::ImageTransportFactory* factory =
        content::ImageTransportFactory::GetInstance();
    content::GLHelper* gl_helper = factory->GetGLHelper();
    if (!gl_helper)
      return;

    scoped_ptr<SkAutoLockPixels> bitmap_pixels_lock(
        new SkAutoLockPixels(*bitmap_));
    uint8* pixels = static_cast<uint8*>(bitmap_->getPixels());

    cc::TextureMailbox texture_mailbox;
    scoped_ptr<cc::SingleReleaseCallback> release_callback;
    result->TakeTexture(&texture_mailbox, &release_callback);
    DCHECK(texture_mailbox.IsTexture());
    if (!texture_mailbox.IsTexture())
      return;

    ignore_result(scoped_callback_runner.Release());

    gl_helper->CropScaleReadbackAndCleanMailbox(
        texture_mailbox.mailbox(),
        texture_mailbox.sync_point(),
        result_size,
        gfx::Rect(result_size),
        result_size,
        pixels,
        kN32_SkColorType,
        base::Bind(
            &CefCopyFrameGenerator::CopyFromCompositingSurfaceFinishedProxy,
            weak_ptr_factory_.GetWeakPtr(),
            base::Passed(&release_callback),
            damage_rect,
            base::Passed(&bitmap_),
            base::Passed(&bitmap_pixels_lock)),
        content::GLHelper::SCALER_QUALITY_FAST);
  }

  static void CopyFromCompositingSurfaceFinishedProxy(
      base::WeakPtr<CefCopyFrameGenerator> generator,
      scoped_ptr<cc::SingleReleaseCallback> release_callback,
      const gfx::Rect& damage_rect,
      scoped_ptr<SkBitmap> bitmap,
      scoped_ptr<SkAutoLockPixels> bitmap_pixels_lock,
      bool result) {
    // This method may be called after the view has been deleted.
    uint32 sync_point = 0;
    if (result) {
      content::GLHelper* gl_helper =
          content::ImageTransportFactory::GetInstance()->GetGLHelper();
      sync_point = gl_helper->InsertSyncPoint();
    }
    bool lost_resource = sync_point == 0;
    release_callback->Run(sync_point, lost_resource);

    if (generator) {
      generator->CopyFromCompositingSurfaceFinished(
          damage_rect, bitmap.Pass(), bitmap_pixels_lock.Pass(), result);
    } else {
      bitmap_pixels_lock.reset();
      bitmap.reset();
    }
  }

  void CopyFromCompositingSurfaceFinished(
      const gfx::Rect& damage_rect,
      scoped_ptr<SkBitmap> bitmap,
      scoped_ptr<SkAutoLockPixels> bitmap_pixels_lock,
      bool result) {
    // Restore ownership of the bitmap to the view.
    DCHECK(!bitmap_);
    bitmap_ = bitmap.Pass();

    if (result) {
      OnCopyFrameCaptureSuccess(damage_rect, *bitmap_,
                                bitmap_pixels_lock.Pass());
    } else {
      bitmap_pixels_lock.reset();
      OnCopyFrameCaptureFailure(damage_rect);
    }
  }

  void PrepareBitmapCopyOutputResult(
      const gfx::Rect& damage_rect,
      scoped_ptr<cc::CopyOutputResult> result) {
    DCHECK(result->HasBitmap());
    scoped_ptr<SkBitmap> source = result->TakeBitmap();
    DCHECK(source);
    if (source) {
      scoped_ptr<SkAutoLockPixels> bitmap_pixels_lock(
          new SkAutoLockPixels(*source));
      OnCopyFrameCaptureSuccess(damage_rect, *source,
                                bitmap_pixels_lock.Pass());
    } else {
      OnCopyFrameCaptureFailure(damage_rect);
    }
  }

  void OnCopyFrameCaptureFailure(
      const gfx::Rect& damage_rect) {
    // Retry with the same |damage_rect|.
    pending_damage_rect_.Union(damage_rect);

    const bool force_frame = (++frame_retry_count_ <= kFrameRetryLimit);
    OnCopyFrameCaptureCompletion(force_frame);
  }

  void OnCopyFrameCaptureSuccess(
      const gfx::Rect& damage_rect,
      const SkBitmap& bitmap,
      scoped_ptr<SkAutoLockPixels> bitmap_pixels_lock) {
    view_->OnPaint(damage_rect, bitmap.width(), bitmap.height(),
                   bitmap.getPixels());
    bitmap_pixels_lock.reset();

    // Reset the frame retry count on successful frame generation.
    if (frame_retry_count_ > 0)
      frame_retry_count_ = 0;

    OnCopyFrameCaptureCompletion(false);
  }

  void OnCopyFrameCaptureCompletion(bool force_frame) {
    frame_in_progress_ = false;

    if (frame_pending_) {
      // Another frame was requested while the current frame was in-progress.
      // Generate the pending frame now.
      CEF_POST_TASK(CEF_UIT,
          base::Bind(&CefCopyFrameGenerator::GenerateCopyFrame,
                     weak_ptr_factory_.GetWeakPtr(),
                     force_frame,
                     gfx::Rect()));
    }
  }

  const int frame_rate_threshold_ms_;
  CefRenderWidgetHostViewOSR* view_;

  base::TimeTicks frame_start_time_;
  bool frame_pending_;
  bool frame_in_progress_;
  int frame_retry_count_;
  scoped_ptr<SkBitmap> bitmap_;
  gfx::Rect pending_damage_rect_;

  base::WeakPtrFactory<CefCopyFrameGenerator> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(CefCopyFrameGenerator);
};

// Used to control the VSync rate in subprocesses when BeginFrame scheduling is
// enabled.
class CefBeginFrameTimer : public cc::TimeSourceClient {
 public:
  CefBeginFrameTimer(int frame_rate_threshold_ms,
                     const base::Closure& callback)
      : callback_(callback) {
    time_source_ = cc::DelayBasedTimeSource::Create(
        base::TimeDelta::FromMilliseconds(frame_rate_threshold_ms), 
        content::BrowserThread::GetMessageLoopProxyForThread(CEF_UIT).get());
    time_source_->SetClient(this);
  }

  void SetActive(bool active) {
    time_source_->SetActive(active);
  }

  bool IsActive() const {
    return time_source_->Active();
  }

 private:
  // cc::TimerSourceClient implementation.
  void OnTimerTick() override {
    callback_.Run();
  }

  const base::Closure callback_;
  scoped_refptr<cc::DelayBasedTimeSource> time_source_;

  DISALLOW_COPY_AND_ASSIGN(CefBeginFrameTimer);
};


CefRenderWidgetHostViewOSR::CefRenderWidgetHostViewOSR(
    content::RenderWidgetHost* widget)
    : scale_factor_(kDefaultScaleFactor),
      frame_rate_threshold_ms_(0),
      delegated_frame_host_(new content::DelegatedFrameHost(this)),
      compositor_widget_(gfx::kNullAcceleratedWidget),
      software_output_device_(NULL),
      hold_resize_(false),
      pending_resize_(false),
      render_widget_host_(content::RenderWidgetHostImpl::From(widget)),
      parent_host_view_(NULL),
      popup_host_view_(NULL),
      is_showing_(true),
      is_destroyed_(false),
      is_scroll_offset_changed_pending_(false),
#if defined(OS_MACOSX)
      text_input_context_osr_mac_(NULL),
#endif
      weak_ptr_factory_(this) {
  DCHECK(render_widget_host_);
  render_widget_host_->SetView(this);

  // CefBrowserHostImpl might not be created at this time for popups.
  if (render_widget_host_->IsRenderView()) {
    browser_impl_ = CefBrowserHostImpl::GetBrowserForHost(
        content::RenderViewHost::From(render_widget_host_));
  }

  root_layer_.reset(new ui::Layer(ui::LAYER_SOLID_COLOR));

  PlatformCreateCompositorWidget();
#if !defined(OS_MACOSX)
  // On OS X the ui::Compositor is created/owned by the platform view.
  compositor_.reset(
      new ui::Compositor(compositor_widget_,
                         content::GetContextFactory(),
                         base::MessageLoopProxy::current()));
#endif
  compositor_->SetDelegate(this);
  compositor_->SetRootLayer(root_layer_.get());

  if (browser_impl_.get())
    ResizeRootLayer();
}

CefRenderWidgetHostViewOSR::~CefRenderWidgetHostViewOSR() {
  // Marking the DelegatedFrameHost as removed from the window hierarchy is
  // necessary to remove all connections to its old ui::Compositor.
  if (is_showing_)
    delegated_frame_host_->WasHidden();
  delegated_frame_host_->ResetCompositor();

  PlatformDestroyCompositorWidget();

  if (copy_frame_generator_.get())
    copy_frame_generator_.reset(NULL);

  delegated_frame_host_.reset(NULL);
  compositor_.reset(NULL);
  root_layer_.reset(NULL);
}

void CefRenderWidgetHostViewOSR::InitAsChild(gfx::NativeView parent_view) {
}

content::RenderWidgetHost*
    CefRenderWidgetHostViewOSR::GetRenderWidgetHost() const {
  return render_widget_host_;
}

void CefRenderWidgetHostViewOSR::SetSize(const gfx::Size& size) {
}

void CefRenderWidgetHostViewOSR::SetBounds(const gfx::Rect& rect) {
}

gfx::Vector2dF CefRenderWidgetHostViewOSR::GetLastScrollOffset() const {
  return last_scroll_offset_;
}

gfx::NativeView CefRenderWidgetHostViewOSR::GetNativeView() const {
  return gfx::NativeView();
}

gfx::NativeViewId CefRenderWidgetHostViewOSR::GetNativeViewId() const {
  return gfx::NativeViewId();
}

gfx::NativeViewAccessible
    CefRenderWidgetHostViewOSR::GetNativeViewAccessible() {
  return gfx::NativeViewAccessible();
}

ui::TextInputClient* CefRenderWidgetHostViewOSR::GetTextInputClient() {
  return NULL;
}

void CefRenderWidgetHostViewOSR::Focus() {
}

bool CefRenderWidgetHostViewOSR::HasFocus() const {
  return false;
}

bool CefRenderWidgetHostViewOSR::IsSurfaceAvailableForCopy() const {
  return delegated_frame_host_->CanCopyToBitmap();
}

void CefRenderWidgetHostViewOSR::Show() {
  if (is_showing_)
    return;

  is_showing_ = true;
  if (render_widget_host_)
    render_widget_host_->WasShown(ui::LatencyInfo());
  delegated_frame_host_->SetCompositor(compositor_.get());
  delegated_frame_host_->WasShown(ui::LatencyInfo());
}

void CefRenderWidgetHostViewOSR::Hide() {
  if (!is_showing_)
    return;

  if (browser_impl_.get())
    browser_impl_->CancelContextMenu();

  if (render_widget_host_)
    render_widget_host_->WasHidden();
  delegated_frame_host_->WasHidden();
  delegated_frame_host_->ResetCompositor();
  is_showing_ = false;
}

bool CefRenderWidgetHostViewOSR::IsShowing() {
  return is_showing_;
}

gfx::Rect CefRenderWidgetHostViewOSR::GetViewBounds() const {
  if (IsPopupWidget())
    return popup_position_;

  if (!browser_impl_.get())
    return gfx::Rect();

  CefRect rc;
  CefRefPtr<CefRenderHandler> handler =
      browser_impl_->GetClient()->GetRenderHandler();
  if (handler.get())
    handler->GetViewRect(browser_impl_.get(), rc);
  return gfx::Rect(rc.x, rc.y, rc.width, rc.height);
}

void CefRenderWidgetHostViewOSR::SetBackgroundColor(SkColor color) {
  content::RenderWidgetHostViewBase::SetBackgroundColor(color);
  bool opaque = GetBackgroundOpaque();
  if (render_widget_host_)
    render_widget_host_->SetBackgroundOpaque(opaque);
}

bool CefRenderWidgetHostViewOSR::LockMouse() {
  return false;
}

void CefRenderWidgetHostViewOSR::UnlockMouse() {
}

void CefRenderWidgetHostViewOSR::OnSwapCompositorFrame(
    uint32 output_surface_id,
    scoped_ptr<cc::CompositorFrame> frame) {
  TRACE_EVENT0("libcef", "CefRenderWidgetHostViewOSR::OnSwapCompositorFrame");

  if (frame->metadata.root_scroll_offset != last_scroll_offset_) {
    last_scroll_offset_ = frame->metadata.root_scroll_offset;

    if (!is_scroll_offset_changed_pending_) {
      // Send the notification asnychronously.
      CEF_POST_TASK(CEF_UIT,
          base::Bind(&CefRenderWidgetHostViewOSR::OnScrollOffsetChanged,
                     weak_ptr_factory_.GetWeakPtr()));
    }
  }

  if (frame->delegated_frame_data) {
    if (software_output_device_) {
      if (!begin_frame_timer_.get()) {
        // If BeginFrame scheduling is enabled SoftwareOutputDevice activity
        // will be controlled via OnSetNeedsBeginFrames. Otherwise, activate
        // the SoftwareOutputDevice now (when the first frame is generated).
        software_output_device_->SetActive(true);
      }

      // The compositor will draw directly to the SoftwareOutputDevice which
      // then calls OnPaint.
      delegated_frame_host_->SwapDelegatedFrame(
          output_surface_id,
          frame->delegated_frame_data.Pass(),
          frame->metadata.device_scale_factor,
          frame->metadata.latency_info);
    } else {
      if (!copy_frame_generator_.get()) {
        copy_frame_generator_.reset(
            new CefCopyFrameGenerator(frame_rate_threshold_ms_, this));
      }

      // Determine the damage rectangle for the current frame. This is the same
      // calculation that SwapDelegatedFrame uses.
      cc::RenderPass* root_pass =
          frame->delegated_frame_data->render_pass_list.back();
      gfx::Size frame_size = root_pass->output_rect.size();
      gfx::Rect damage_rect = gfx::ToEnclosingRect(root_pass->damage_rect);
      damage_rect.Intersect(gfx::Rect(frame_size));

      delegated_frame_host_->SwapDelegatedFrame(
          output_surface_id,
          frame->delegated_frame_data.Pass(),
          frame->metadata.device_scale_factor,
          frame->metadata.latency_info);

      // Request a copy of the last compositor frame which will eventually call
      // OnPaint asynchronously.
      copy_frame_generator_->GenerateCopyFrame(true, damage_rect);
    }

    return;
  }

  if (frame->software_frame_data) {
    DLOG(ERROR) << "Unable to use software frame in CEF windowless rendering";
    if (render_widget_host_) {
      content::bad_message::ReceivedBadMessage(
          render_widget_host_->GetProcess(),
          content::bad_message::RWHVM_UNEXPECTED_FRAME_TYPE);
    }
    return;
  }
}

void CefRenderWidgetHostViewOSR::InitAsPopup(
    content::RenderWidgetHostView* parent_host_view,
    const gfx::Rect& pos) {
  parent_host_view_ = static_cast<CefRenderWidgetHostViewOSR*>(
      parent_host_view);
  browser_impl_ = parent_host_view_->browser_impl();
  DCHECK(browser_impl_.get());

  if (parent_host_view_->popup_host_view_) {
    // Cancel the previous popup widget.
    parent_host_view_->popup_host_view_->CancelPopupWidget();
  }

  parent_host_view_->set_popup_host_view(this);

  CefRefPtr<CefRenderHandler> handler =
      browser_impl_->GetClient()->GetRenderHandler();
  if (handler.get())
    handler->OnPopupShow(browser_impl_.get(), true);

  popup_position_ = pos;

  CefRect widget_pos(pos.x(), pos.y(), pos.width(), pos.height());
  if (handler.get())
    handler->OnPopupSize(browser_impl_.get(), widget_pos);

  ResizeRootLayer();
  Show();
}

void CefRenderWidgetHostViewOSR::InitAsFullscreen(
    content::RenderWidgetHostView* reference_host_view) {
  NOTREACHED() << "Fullscreen widgets are not supported in OSR";
}

void CefRenderWidgetHostViewOSR::MovePluginWindows(
    const std::vector<content::WebPluginGeometry>& moves) {
}

void CefRenderWidgetHostViewOSR::Blur() {
}

void CefRenderWidgetHostViewOSR::UpdateCursor(
    const content::WebCursor& cursor) {
  TRACE_EVENT0("libcef", "CefRenderWidgetHostViewOSR::UpdateCursor");
  if (!browser_impl_.get())
    return;

  CefRefPtr<CefRenderHandler> handler =
      browser_impl_->GetClient()->GetRenderHandler();
  if (!handler.get())
    return;

  content::WebCursor::CursorInfo cursor_info;
  cursor.GetCursorInfo(&cursor_info);

  const cef_cursor_type_t cursor_type =
      static_cast<cef_cursor_type_t>(cursor_info.type);
  CefCursorInfo custom_cursor_info;
  if (cursor.IsCustom()) {
    custom_cursor_info.hotspot.x = cursor_info.hotspot.x();
    custom_cursor_info.hotspot.y = cursor_info.hotspot.y();
    custom_cursor_info.image_scale_factor = cursor_info.image_scale_factor;
    custom_cursor_info.buffer = cursor_info.custom_image.getPixels();
    custom_cursor_info.size.width = cursor_info.custom_image.width();
    custom_cursor_info.size.height = cursor_info.custom_image.height();
  }

#if defined(USE_AURA)
  content::WebCursor web_cursor = cursor;

  ui::PlatformCursor platform_cursor;
  if (web_cursor.IsCustom()) {
    // |web_cursor| owns the resulting |platform_cursor|.
    platform_cursor = web_cursor.GetPlatformCursor();
  } else {
    platform_cursor = browser_impl_->GetPlatformCursor(cursor_info.type);
  }

  handler->OnCursorChange(browser_impl_.get(), platform_cursor, cursor_type,
                          custom_cursor_info);
#elif defined(OS_MACOSX)
  // |web_cursor| owns the resulting |native_cursor|.
  content::WebCursor web_cursor = cursor;
  CefCursorHandle native_cursor = web_cursor.GetNativeCursor();
  handler->OnCursorChange(browser_impl_.get(), native_cursor, cursor_type,
                          custom_cursor_info);
#else
  // TODO(port): Implement this method to work on other platforms as part of
  // off-screen rendering support.
  NOTREACHED();
#endif
}

void CefRenderWidgetHostViewOSR::SetIsLoading(bool is_loading) {
}

#if !defined(OS_MACOSX)
void CefRenderWidgetHostViewOSR::TextInputTypeChanged(ui::TextInputType type,
                                                      ui::TextInputMode mode,
                                                      bool can_compose_inline,
                                                      int flags) {
}

void CefRenderWidgetHostViewOSR::ImeCancelComposition() {
}
#endif  // !defined(OS_MACOSX)

void CefRenderWidgetHostViewOSR::RenderProcessGone(
    base::TerminationStatus status,
    int error_code) {
  // TODO(OSR): Need to also clear WebContentsViewOSR::view_?
  Destroy();
  render_widget_host_ = NULL;
  parent_host_view_ = NULL;
  popup_host_view_ = NULL;
}

void CefRenderWidgetHostViewOSR::Destroy() {
  if (!is_destroyed_) {
    is_destroyed_ = true;

    if (IsPopupWidget()) {
      CancelPopupWidget();
    } else {
      if (popup_host_view_)
        popup_host_view_->CancelPopupWidget();
      Hide();
    }
  }

  delete this;
}

void CefRenderWidgetHostViewOSR::SetTooltipText(
    const base::string16& tooltip_text) {
  if (!browser_impl_.get())
    return;

  CefString tooltip(tooltip_text);
  CefRefPtr<CefDisplayHandler> handler =
      browser_impl_->GetClient()->GetDisplayHandler();
  if (handler.get()) {
    handler->OnTooltip(browser_impl_.get(), tooltip);
  }
}

void CefRenderWidgetHostViewOSR::SelectionChanged(
    const base::string16& text,
    size_t offset,
    const gfx::Range& range) {
}

gfx::Size CefRenderWidgetHostViewOSR::GetRequestedRendererSize() const {
  return delegated_frame_host_->GetRequestedRendererSize();
}

gfx::Size CefRenderWidgetHostViewOSR::GetPhysicalBackingSize() const {
  return gfx::ConvertSizeToPixel(scale_factor_, GetRequestedRendererSize());
}

void CefRenderWidgetHostViewOSR::SelectionBoundsChanged(
    const ViewHostMsg_SelectionBounds_Params& params) {
}

void CefRenderWidgetHostViewOSR::CopyFromCompositingSurface(
    const gfx::Rect& src_subrect,
    const gfx::Size& dst_size,
    content::ReadbackRequestCallback& callback,
    const SkColorType color_type) {
  delegated_frame_host_->CopyFromCompositingSurface(
      src_subrect, dst_size, callback, color_type);
}

void CefRenderWidgetHostViewOSR::CopyFromCompositingSurfaceToVideoFrame(
    const gfx::Rect& src_subrect,
    const scoped_refptr<media::VideoFrame>& target,
    const base::Callback<void(bool)>& callback) {
  delegated_frame_host_->CopyFromCompositingSurfaceToVideoFrame(
      src_subrect, target, callback);
}

bool CefRenderWidgetHostViewOSR::CanCopyToVideoFrame() const {
  return delegated_frame_host_->CanCopyToVideoFrame();
}

bool CefRenderWidgetHostViewOSR::CanSubscribeFrame() const {
  return delegated_frame_host_->CanSubscribeFrame();
}

void CefRenderWidgetHostViewOSR::BeginFrameSubscription(
    scoped_ptr<content::RenderWidgetHostViewFrameSubscriber> subscriber) {
  delegated_frame_host_->BeginFrameSubscription(subscriber.Pass());
}

void CefRenderWidgetHostViewOSR::EndFrameSubscription() {
  delegated_frame_host_->EndFrameSubscription();
}

void CefRenderWidgetHostViewOSR::AcceleratedSurfaceInitialized(
    int route_id) {
}

bool CefRenderWidgetHostViewOSR::HasAcceleratedSurface(
    const gfx::Size& desired_size) {
  // CEF doesn't use GetBackingStore for accelerated pages, so it doesn't
  // matter what is returned here as GetBackingStore is the only caller of this
  // method.
  NOTREACHED();
  return false;
}

void CefRenderWidgetHostViewOSR::GetScreenInfo(blink::WebScreenInfo* results) {
  if (!browser_impl_.get())
    return;

  CefScreenInfo screen_info(
      kDefaultScaleFactor, 0, 0, false, CefRect(), CefRect());

  CefRefPtr<CefRenderHandler> handler =
      browser_impl_->client()->GetRenderHandler();
  if (handler.get() &&
      (!handler->GetScreenInfo(browser_impl_.get(), screen_info) ||
      screen_info.rect.width == 0 ||
      screen_info.rect.height == 0 ||
      screen_info.available_rect.width == 0 ||
      screen_info.available_rect.height == 0)) {
    // If a screen rectangle was not provided, try using the view rectangle
    // instead. Otherwise, popup views may be drawn incorrectly, or not at all.
    CefRect screenRect;
    if (!handler->GetViewRect(browser_impl_.get(), screenRect)) {
      NOTREACHED();
      screenRect = CefRect();
    }

    if (screen_info.rect.width == 0 && screen_info.rect.height == 0)
      screen_info.rect = screenRect;

    if (screen_info.available_rect.width == 0 &&
        screen_info.available_rect.height == 0)
      screen_info.available_rect = screenRect;
  }

  *results = webScreenInfoFrom(screen_info);
}

gfx::Rect CefRenderWidgetHostViewOSR::GetBoundsInRootWindow() {
  if (!browser_impl_.get())
    return gfx::Rect();

  CefRect rc;
  CefRefPtr<CefRenderHandler> handler =
      browser_impl_->client()->GetRenderHandler();
  if (handler.get() && handler->GetRootScreenRect(browser_impl_.get(), rc))
    return gfx::Rect(rc.x, rc.y, rc.width, rc.height);
  return gfx::Rect();
}

gfx::GLSurfaceHandle CefRenderWidgetHostViewOSR::GetCompositingSurface() {
  return content::ImageTransportFactory::GetInstance()->
      GetSharedSurfaceHandle();
}

content::BrowserAccessibilityManager*
    CefRenderWidgetHostViewOSR::CreateBrowserAccessibilityManager(
        content::BrowserAccessibilityDelegate* delegate) {
  return NULL;
}

#if defined(TOOLKIT_VIEWS) || defined(USE_AURA)
void CefRenderWidgetHostViewOSR::ShowDisambiguationPopup(
    const gfx::Rect& rect_pixels,
    const SkBitmap& zoomed_bitmap) {
}
#endif

#if !defined(OS_MACOSX) && defined(USE_AURA)
void CefRenderWidgetHostViewOSR::ImeCompositionRangeChanged(
    const gfx::Range& range,
    const std::vector<gfx::Rect>& character_bounds) {
}
#endif

bool CefRenderWidgetHostViewOSR::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(CefRenderWidgetHostViewOSR, msg)
    IPC_MESSAGE_HANDLER(ViewHostMsg_SetNeedsBeginFrames,
                        OnSetNeedsBeginFrames)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  if (!handled)
    return content::RenderWidgetHostViewBase::OnMessageReceived(msg);
  return handled;
}

void CefRenderWidgetHostViewOSR::OnSetNeedsBeginFrames(bool enabled) {
  // Start/stop the timer that sends BeginFrame requests.
  begin_frame_timer_->SetActive(enabled);
  if (software_output_device_) {
    // When the SoftwareOutputDevice is active it will call OnPaint for each
    // frame. If the SoftwareOutputDevice is deactivated while an invalidation
    // region is pending it will call OnPaint immediately.
    software_output_device_->SetActive(enabled);
  }
}

scoped_ptr<cc::SoftwareOutputDevice>
CefRenderWidgetHostViewOSR::CreateSoftwareOutputDevice(
    ui::Compositor* compositor) {
  DCHECK_EQ(compositor_.get(), compositor);
  DCHECK(!copy_frame_generator_);
  DCHECK(!software_output_device_);
  software_output_device_ = new CefSoftwareOutputDeviceOSR(
      compositor,
      browser_impl_.get() ? browser_impl_->IsTransparent() : false,
      base::Bind(&CefRenderWidgetHostViewOSR::OnPaint,
                 weak_ptr_factory_.GetWeakPtr()));
  return make_scoped_ptr<cc::SoftwareOutputDevice>(software_output_device_);
}

ui::Layer* CefRenderWidgetHostViewOSR::DelegatedFrameHostGetLayer() const {
  return root_layer_.get();
}

bool CefRenderWidgetHostViewOSR::DelegatedFrameHostIsVisible() const {
  return !render_widget_host_->is_hidden();
}

gfx::Size
CefRenderWidgetHostViewOSR::DelegatedFrameHostDesiredSizeInDIP() const {
  return root_layer_->bounds().size();
}

bool CefRenderWidgetHostViewOSR::DelegatedFrameCanCreateResizeLock() const {
  return !render_widget_host_->auto_resize_enabled();
}

scoped_ptr<content::ResizeLock>
CefRenderWidgetHostViewOSR::DelegatedFrameHostCreateResizeLock(
    bool defer_compositor_lock) {
  const gfx::Size& desired_size = root_layer_->bounds().size();
  return scoped_ptr<content::ResizeLock>(new CefResizeLock(
      this,
      desired_size,
      defer_compositor_lock,
      kResizeLockTimeoutMs));
}

void CefRenderWidgetHostViewOSR::DelegatedFrameHostResizeLockWasReleased() {
  return render_widget_host_->WasResized();
}

void CefRenderWidgetHostViewOSR::DelegatedFrameHostSendCompositorSwapAck(
    int output_surface_id,
    const cc::CompositorFrameAck& ack) {
  render_widget_host_->Send(new ViewMsg_SwapCompositorFrameAck(
      render_widget_host_->GetRoutingID(),
      output_surface_id, ack));
}

void
CefRenderWidgetHostViewOSR::DelegatedFrameHostSendReclaimCompositorResources(
    int output_surface_id,
    const cc::CompositorFrameAck& ack) {
  render_widget_host_->Send(new ViewMsg_ReclaimCompositorResources(
      render_widget_host_->GetRoutingID(),
      output_surface_id, ack));
}

void CefRenderWidgetHostViewOSR::DelegatedFrameHostOnLostCompositorResources() {
  render_widget_host_->ScheduleComposite();
}

void CefRenderWidgetHostViewOSR::DelegatedFrameHostUpdateVSyncParameters(
    const base::TimeTicks& timebase,
    const base::TimeDelta& interval) {
  render_widget_host_->UpdateVSyncParameters(timebase, interval);
}

bool CefRenderWidgetHostViewOSR::InstallTransparency() {
  if (browser_impl_.get() && browser_impl_->IsTransparent()) {
    SetBackgroundColor(SkColorSetARGB(SK_AlphaTRANSPARENT, 0, 0, 0));
    compositor_->SetHostHasTransparentBackground(true);
    return true;
  }
  return false;
}

void CefRenderWidgetHostViewOSR::WasResized() {
  if (hold_resize_) {
    if (!pending_resize_)
      pending_resize_ = true;
    return;
  }

  ResizeRootLayer();
  if (render_widget_host_)
    render_widget_host_->WasResized();
  delegated_frame_host_->WasResized();
}

void CefRenderWidgetHostViewOSR::OnScreenInfoChanged() {
  TRACE_EVENT0("libcef", "CefRenderWidgetHostViewOSR::OnScreenInfoChanged");
  if (!render_widget_host_)
    return;

  // TODO(OSR): Update the backing store.

  render_widget_host_->NotifyScreenInfoChanged();
  // We might want to change the cursor scale factor here as well - see the
  // cache for the current_cursor_, as passed by UpdateCursor from the renderer
  // in the rwhv_aura (current_cursor_.SetScaleFactor)
}

void CefRenderWidgetHostViewOSR::Invalidate(
    CefBrowserHost::PaintElementType type) {
  TRACE_EVENT1("libcef", "CefRenderWidgetHostViewOSR::Invalidate", "type",
               type); 
  if (!IsPopupWidget() && type == PET_POPUP) {
    if (popup_host_view_)
      popup_host_view_->Invalidate(type);
    return;
  }

  const gfx::Rect& bounds_in_pixels = gfx::Rect(GetPhysicalBackingSize());

  if (software_output_device_) {
    if (IsFramePending()) {
      // Include the invalidated region in the next frame generated.
      software_output_device_->Invalidate(bounds_in_pixels);
    } else {
      // Call OnPaint immediately.
      software_output_device_->OnPaint(bounds_in_pixels);
    }
  } else if (copy_frame_generator_.get()) {
    copy_frame_generator_->GenerateCopyFrame(true, bounds_in_pixels);
  }
}

void CefRenderWidgetHostViewOSR::SendKeyEvent(
    const content::NativeWebKeyboardEvent& event) {
  TRACE_EVENT0("libcef", "CefRenderWidgetHostViewOSR::SendKeyEvent");
  if (!render_widget_host_)
    return;
  render_widget_host_->ForwardKeyboardEvent(event);
}

void CefRenderWidgetHostViewOSR::SendMouseEvent(
    const blink::WebMouseEvent& event) {
  TRACE_EVENT0("libcef", "CefRenderWidgetHostViewOSR::SendMouseEvent");
  if (!IsPopupWidget()) {
    if (browser_impl_.get() && event.type == blink::WebMouseEvent::MouseDown)
      browser_impl_->CancelContextMenu();

    if (popup_host_view_ &&
        popup_host_view_->popup_position_.Contains(event.x, event.y)) {
      blink::WebMouseEvent popup_event(event);
      popup_event.x -= popup_host_view_->popup_position_.x();
      popup_event.y -= popup_host_view_->popup_position_.y();
      popup_event.windowX = popup_event.x;
      popup_event.windowY = popup_event.y;

      popup_host_view_->SendMouseEvent(popup_event);
      return;
    }
  }
  if (!render_widget_host_)
    return;
  render_widget_host_->ForwardMouseEvent(event);
}

void CefRenderWidgetHostViewOSR::SendMouseWheelEvent(
    const blink::WebMouseWheelEvent& event) {
  TRACE_EVENT0("libcef", "CefRenderWidgetHostViewOSR::SendMouseWheelEvent");
  if (!IsPopupWidget()) {
    if (browser_impl_.get())
      browser_impl_->CancelContextMenu();

    if (popup_host_view_) {
      if (popup_host_view_->popup_position_.Contains(event.x, event.y)) {
        blink::WebMouseWheelEvent popup_event(event);
        popup_event.x -= popup_host_view_->popup_position_.x();
        popup_event.y -= popup_host_view_->popup_position_.y();
        popup_event.windowX = popup_event.x;
        popup_event.windowY = popup_event.y;
        popup_host_view_->SendMouseWheelEvent(popup_event);
        return;
      } else {
        // Scrolling outside of the popup widget so destroy it.
        // Execute asynchronously to avoid deleting the widget from inside some
        // other callback.
        CEF_POST_TASK(CEF_UIT,
            base::Bind(&CefRenderWidgetHostViewOSR::CancelPopupWidget,
                       popup_host_view_->weak_ptr_factory_.GetWeakPtr()));
      }
    }
  }
  if (!render_widget_host_)
    return;
  render_widget_host_->ForwardWheelEvent(event);
}

void CefRenderWidgetHostViewOSR::SendFocusEvent(bool focus) {
  if (!render_widget_host_)
    return;

  content::RenderWidgetHostImpl* widget =
      content::RenderWidgetHostImpl::From(render_widget_host_);
  if (focus) {
    widget->GotFocus();
    widget->SetActive(true);
  } else {
    if (browser_impl_.get())
      browser_impl_->CancelContextMenu();

    widget->SetActive(false);
    widget->Blur();
  }
}

void CefRenderWidgetHostViewOSR::HoldResize() {
  if (!hold_resize_)
    hold_resize_ = true;
}

void CefRenderWidgetHostViewOSR::ReleaseResize() {
  if (!hold_resize_)
    return;

  hold_resize_ = false;
  if (pending_resize_) {
    pending_resize_ = false;
    CEF_POST_TASK(CEF_UIT,
        base::Bind(&CefRenderWidgetHostViewOSR::WasResized,
                   weak_ptr_factory_.GetWeakPtr()));
  }
}

void CefRenderWidgetHostViewOSR::OnPaint(
    const gfx::Rect& damage_rect,
    int bitmap_width,
    int bitmap_height,
    void* bitmap_pixels) {
  TRACE_EVENT0("libcef", "CefRenderWidgetHostViewOSR::OnPaint");

  CefRefPtr<CefRenderHandler> handler =
      browser_impl_->client()->GetRenderHandler();
  if (!handler.get())
    return;

  // Don't execute WasResized while the OnPaint callback is pending.
  HoldResize();

  gfx::Rect rect_in_bitmap(0, 0, bitmap_width, bitmap_height);
  rect_in_bitmap.Intersect(damage_rect);

  CefRenderHandler::RectList rcList;
  rcList.push_back(CefRect(rect_in_bitmap.x(), rect_in_bitmap.y(),
                           rect_in_bitmap.width(), rect_in_bitmap.height()));

  handler->OnPaint(
        browser_impl_.get(),
        IsPopupWidget() ? PET_POPUP : PET_VIEW,
        rcList,
        bitmap_pixels,
        bitmap_width,
        bitmap_height);

  ReleaseResize();
}

void CefRenderWidgetHostViewOSR::SetFrameRate() {
  DCHECK(browser_impl_.get());
  if (!browser_impl_.get())
    return;

  // Only set the frame rate one time.
  if (frame_rate_threshold_ms_ != 0)
    return;

  int frame_rate = browser_impl_->settings().windowless_frame_rate;
  if (frame_rate < 1)
    frame_rate = kDefaultFrameRate;
  else if (frame_rate > kMaximumFrameRate)
    frame_rate = kMaximumFrameRate;
  frame_rate_threshold_ms_ = 1000 / frame_rate;

  // Configure the VSync interval for the browser process.
  compositor_->vsync_manager()->SetAuthoritativeVSyncInterval(
      base::TimeDelta::FromMilliseconds(frame_rate_threshold_ms_));

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableBeginFrameScheduling)) {
    DCHECK(!begin_frame_timer_.get());
    begin_frame_timer_.reset(new CefBeginFrameTimer(
        frame_rate_threshold_ms_,
        base::Bind(&CefRenderWidgetHostViewOSR::OnBeginFrameTimerTick,
                   weak_ptr_factory_.GetWeakPtr())));
  }
}

void CefRenderWidgetHostViewOSR::SetDeviceScaleFactor() {
  if (browser_impl_.get())  {
    CefScreenInfo screen_info(
        kDefaultScaleFactor, 0, 0, false, CefRect(), CefRect());
    CefRefPtr<CefRenderHandler> handler =
        browser_impl_->client()->GetRenderHandler();
    if (handler.get() && handler->GetScreenInfo(browser_impl_.get(),
                                                screen_info)) {
      scale_factor_ = screen_info.device_scale_factor;
      return;
    }
  }

  scale_factor_ = kDefaultScaleFactor;
}

void CefRenderWidgetHostViewOSR::ResizeRootLayer() {
  SetFrameRate();
  SetDeviceScaleFactor();

  gfx::Size size;
  if (!IsPopupWidget())
    size = GetViewBounds().size();
  else
    size = popup_position_.size();

  if (size == root_layer_->bounds().size())
    return;

  const gfx::Size& size_in_pixels =
      gfx::ConvertSizeToPixel(scale_factor_, size);

  root_layer_->SetBounds(gfx::Rect(size));
  compositor_->SetScaleAndSize(scale_factor_, size_in_pixels);
}

bool CefRenderWidgetHostViewOSR::IsFramePending() {
  if (!IsShowing())
    return false;

  if (begin_frame_timer_.get())
    return begin_frame_timer_->IsActive();
  else if (copy_frame_generator_.get())
    return copy_frame_generator_->frame_pending();

  return false;
}

void CefRenderWidgetHostViewOSR::OnBeginFrameTimerTick() {
  const base::TimeTicks frame_time = base::TimeTicks::Now();
  const base::TimeDelta vsync_period =
      base::TimeDelta::FromMilliseconds(frame_rate_threshold_ms_);
  SendBeginFrame(frame_time, vsync_period);
}

void CefRenderWidgetHostViewOSR::SendBeginFrame(base::TimeTicks frame_time,
                                                base::TimeDelta vsync_period) {
  TRACE_EVENT1("libcef", "CefRenderWidgetHostViewOSR::SendBeginFrame",
               "frame_time_us", frame_time.ToInternalValue());

  base::TimeTicks display_time = frame_time + vsync_period;

  // TODO(brianderson): Use adaptive draw-time estimation.
  base::TimeDelta estimated_browser_composite_time =
      base::TimeDelta::FromMicroseconds(
          (1.0f * base::Time::kMicrosecondsPerSecond) / (3.0f * 60));

  base::TimeTicks deadline = display_time - estimated_browser_composite_time;

  render_widget_host_->Send(new ViewMsg_BeginFrame(
      render_widget_host_->GetRoutingID(),
      cc::BeginFrameArgs::Create(BEGINFRAME_FROM_HERE, frame_time, deadline,
                                 vsync_period, cc::BeginFrameArgs::NORMAL)));
}

void CefRenderWidgetHostViewOSR::CancelPopupWidget() {
  DCHECK(IsPopupWidget());

  if (render_widget_host_)
    render_widget_host_->LostCapture();

  Hide();

  if (browser_impl_.get()) {
    CefRefPtr<CefRenderHandler> handler =
        browser_impl_->client()->GetRenderHandler();
    if (handler.get())
      handler->OnPopupShow(browser_impl_.get(), false);
    browser_impl_ = NULL;
  }

  if (parent_host_view_) {
    parent_host_view_->set_popup_host_view(NULL);
    parent_host_view_ = NULL;
  }

  if (render_widget_host_ && !is_destroyed_) {
    is_destroyed_ = true;
    // Results in a call to Destroy().
    render_widget_host_->Shutdown();
  }
}

void CefRenderWidgetHostViewOSR::OnScrollOffsetChanged() {
  if (browser_impl_.get()) {
    CefRefPtr<CefRenderHandler> handler =
        browser_impl_->client()->GetRenderHandler();
    if (handler.get()) {
      handler->OnScrollOffsetChanged(browser_impl_.get(),
                                     last_scroll_offset_.x(),
                                     last_scroll_offset_.y());
    }
  }
  is_scroll_offset_changed_pending_ = false;
}
