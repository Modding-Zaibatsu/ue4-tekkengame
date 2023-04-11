// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/common/main_delegate.h"
#include "libcef/browser/content_browser_client.h"
#include "libcef/browser/context.h"
#include "libcef/common/cef_switches.h"
#include "libcef/common/command_line_impl.h"
#include "libcef/common/crash_reporter_client.h"
#include "libcef/renderer/content_renderer_client.h"
#include "libcef/utility/content_utility_client.h"

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/lazy_instance.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/browser_main_runner.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/main_function_params.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/base/ui_base_switches.h"

#if defined(OS_WIN)
#include <Objbase.h>  // NOLINT(build/include_order)
#include "base/win/registry.h"
#include "components/crash/app/breakpad_win.h"
#endif

#if defined(OS_MACOSX)
#include "libcef/common/util_mac.h"
#include "base/mac/os_crash_dumps.h"
#include "base/mac/bundle_locations.h"
#include "base/mac/foundation_util.h"
#include "components/crash/app/breakpad_mac.h"
#include "content/public/common/content_paths.h"
#endif

#if defined(OS_POSIX) && !defined(OS_MACOSX)
#include "components/crash/app/breakpad_linux.h"
#endif

#if defined(OS_LINUX)
#include "base/environment.h"
#include "base/nix/xdg_util.h"
#endif

namespace {

base::LazyInstance<CefCrashReporterClient>::Leaky g_crash_reporter_client =
    LAZY_INSTANCE_INITIALIZER;

#if defined(OS_MACOSX)

base::FilePath GetFrameworksPath() {
  // Start out with the path to the running executable.
  base::FilePath execPath;
  PathService::Get(base::FILE_EXE, &execPath);

  // Get the main bundle path.
  base::FilePath bundlePath = base::mac::GetAppBundlePath(execPath);

  // Go into the Contents/Frameworks directory.
  return bundlePath.Append(FILE_PATH_LITERAL("Contents"))
                   .Append(FILE_PATH_LITERAL("Frameworks"));
}

// The framework bundle path is used for loading resources, libraries, etc.
base::FilePath GetFrameworkBundlePath() {
  return GetFrameworksPath().Append(
      FILE_PATH_LITERAL("Chromium Embedded Framework.framework"));
}

base::FilePath GetResourcesFilePath() {
  return GetFrameworkBundlePath().Append(FILE_PATH_LITERAL("Resources"));
}

void OverrideFrameworkBundlePath() {
  base::mac::SetOverrideFrameworkBundlePath(GetFrameworkBundlePath());
}

void OverrideChildProcessPath() {
  // Retrieve the name of the running executable.
  base::FilePath path;
  PathService::Get(base::FILE_EXE, &path);

  std::string name = path.BaseName().value();

  base::FilePath helper_path = GetFrameworksPath()
      .Append(FILE_PATH_LITERAL(name+" Helper.app"))
      .Append(FILE_PATH_LITERAL("Contents"))
      .Append(FILE_PATH_LITERAL("MacOS"))
      .Append(FILE_PATH_LITERAL(name+" Helper"));

  PathService::Override(content::CHILD_PROCESS_EXE, helper_path);
}

#else  // !defined(OS_MACOSX)

base::FilePath GetResourcesFilePath() {
  base::FilePath pak_dir;
  PathService::Get(base::DIR_MODULE, &pak_dir);
  return pak_dir;
}

#endif  // !defined(OS_MACOSX)

#if defined(OS_WIN)

const wchar_t kFlashRegistryRoot[] = L"SOFTWARE\\Macromedia\\FlashPlayerPepper";
const wchar_t kFlashPlayerPathValueName[] = L"PlayerPath";

// Gets the Flash path if installed on the system.
bool GetSystemFlashDirectory(base::FilePath* out_path) {
  base::win::RegKey path_key(HKEY_LOCAL_MACHINE, kFlashRegistryRoot, KEY_READ);
  base::string16 path_str;
  if (FAILED(path_key.ReadValue(kFlashPlayerPathValueName, &path_str)))
    return false;
  base::FilePath plugin_path = base::FilePath(path_str).DirName();

  *out_path = plugin_path;
  return true;
}

#elif defined(OS_MACOSX)

const base::FilePath::CharType kPepperFlashSystemBaseDirectory[] =
    FILE_PATH_LITERAL("Internet Plug-Ins/PepperFlashPlayer");

#endif

void OverridePepperFlashSystemPluginPath() {
  base::FilePath plugin_path;
#if defined(OS_WIN)
  if (!GetSystemFlashDirectory(&plugin_path))
    return;
#elif defined(OS_MACOSX)
  if (!util_mac::GetLocalLibraryDirectory(&plugin_path))
    return;
  plugin_path = plugin_path.Append(kPepperFlashSystemBaseDirectory);
#else
  // A system plugin is not available on other platforms.
  return;
#endif

  PathService::Override(chrome::DIR_PEPPER_FLASH_SYSTEM_PLUGIN, plugin_path);
}

#if defined(OS_LINUX)

// Based on chrome/common/chrome_paths_linux.cc.
// See http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
// for a spec on where config files go.  The net effect for most
// systems is we use ~/.config/chromium/ for Chromium and
// ~/.config/google-chrome/ for official builds.
// (This also helps us sidestep issues with other apps grabbing ~/.chromium .)
bool GetDefaultUserDataDirectory(base::FilePath* result) {
  scoped_ptr<base::Environment> env(base::Environment::Create());
  base::FilePath config_dir(
      base::nix::GetXDGDirectory(env.get(),
                                 base::nix::kXdgConfigHomeEnvVar,
                                 base::nix::kDotConfigDir));
  *result = config_dir.Append(FILE_PATH_LITERAL("cef_user_data"));
  return true;
}

#elif defined(OS_MACOSX)

// Based on chrome/common/chrome_paths_mac.mm.
bool GetDefaultUserDataDirectory(base::FilePath* result) {
  if (!PathService::Get(base::DIR_APP_DATA, result))
    return false;
  *result = result->Append(FILE_PATH_LITERAL("CEF"));
  *result = result->Append(FILE_PATH_LITERAL("User Data"));
  return true;
}

#elif defined(OS_WIN)

// Based on chrome/common/chrome_paths_win.cc.
bool GetDefaultUserDataDirectory(base::FilePath* result) {
  if (!PathService::Get(base::DIR_LOCAL_APP_DATA, result))
    return false;
  *result = result->Append(FILE_PATH_LITERAL("CEF"));
  *result = result->Append(FILE_PATH_LITERAL("User Data"));
  return true;
}

#endif

base::FilePath GetUserDataPath() {
  const CefSettings& settings = CefContext::Get()->settings();
  if (settings.user_data_path.length > 0)
    return base::FilePath(CefString(&settings.user_data_path));

  base::FilePath result;
  if (GetDefaultUserDataDirectory(&result))
    return result;

  if (PathService::Get(base::DIR_TEMP, &result))
    return result;

  NOTREACHED();
  return result;
}

// Returns true if |scale_factor| is supported by this platform.
// Same as ResourceBundle::IsScaleFactorSupported.
bool IsScaleFactorSupported(ui::ScaleFactor scale_factor) {
  const std::vector<ui::ScaleFactor>& supported_scale_factors =
      ui::GetSupportedScaleFactors();
  return std::find(supported_scale_factors.begin(),
                   supported_scale_factors.end(),
                   scale_factor) != supported_scale_factors.end();
}

// Used to run the UI on a separate thread.
class CefUIThread : public base::Thread {
 public:
  explicit CefUIThread(const content::MainFunctionParams& main_function_params)
    : base::Thread("CefUIThread"),
      main_function_params_(main_function_params) {
  }

  void Init() override {
#if defined(OS_WIN)
    // Initializes the COM library on the current thread.
    CoInitialize(NULL);
#endif

    // Use our own browser process runner.
    browser_runner_.reset(content::BrowserMainRunner::Create());

    // Initialize browser process state. Uses the current thread's mesage loop.
    int exit_code = browser_runner_->Initialize(main_function_params_);
    CHECK_EQ(exit_code, -1);
  }

  void CleanUp() override {
    browser_runner_->Shutdown();
    browser_runner_.reset(NULL);

#if defined(OS_WIN)
    // Closes the COM library on the current thread. CoInitialize must
    // be balanced by a corresponding call to CoUninitialize.
    CoUninitialize();
#endif
  }

 protected:
  content::MainFunctionParams main_function_params_;
  scoped_ptr<content::BrowserMainRunner> browser_runner_;
};

}  // namespace

CefMainDelegate::CefMainDelegate(CefRefPtr<CefApp> application)
    : content_client_(application) {
  // Necessary so that exported functions from base_impl.cc will be included
  // in the binary.
  extern void base_impl_stub();
  base_impl_stub();
}

CefMainDelegate::~CefMainDelegate() {
}

bool CefMainDelegate::BasicStartupComplete(int* exit_code) {
#if defined(OS_MACOSX)
  OverrideFrameworkBundlePath();
#endif

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  std::string process_type =
      command_line->GetSwitchValueASCII(switches::kProcessType);

  if (process_type.empty()) {
    // In the browser process. Populate the global command-line object.
    const CefSettings& settings = CefContext::Get()->settings();

    if (settings.command_line_args_disabled) {
      // Remove any existing command-line arguments.
      base::CommandLine::StringVector argv;
      argv.push_back(command_line->GetProgram().value());
      command_line->InitFromArgv(argv);

      const base::CommandLine::SwitchMap& map = command_line->GetSwitches();
      const_cast<base::CommandLine::SwitchMap*>(&map)->clear();
    }

    if (settings.single_process)
      command_line->AppendSwitch(switches::kSingleProcess);

    bool no_sandbox = settings.no_sandbox ? true : false;

    if (settings.browser_subprocess_path.length > 0) {
      base::FilePath file_path =
          base::FilePath(CefString(&settings.browser_subprocess_path));
      if (!file_path.empty()) {
        command_line->AppendSwitchPath(switches::kBrowserSubprocessPath,
                                       file_path);

#if defined(OS_WIN)
        // The sandbox is not supported when using a separate subprocess
        // executable on Windows.
        no_sandbox = true;
#endif
      }
    }

    if (no_sandbox)
      command_line->AppendSwitch(switches::kNoSandbox);

    if (settings.user_agent.length > 0) {
      command_line->AppendSwitchASCII(switches::kUserAgent,
          CefString(&settings.user_agent));
    } else if (settings.product_version.length > 0) {
      command_line->AppendSwitchASCII(switches::kProductVersion,
          CefString(&settings.product_version));
    }

    if (settings.locale.length > 0) {
      command_line->AppendSwitchASCII(switches::kLang,
          CefString(&settings.locale));
    } else if (!command_line->HasSwitch(switches::kLang)) {
      command_line->AppendSwitchASCII(switches::kLang, "en-US");
    }

    if (settings.log_file.length > 0) {
      base::FilePath file_path = base::FilePath(CefString(&settings.log_file));
      if (!file_path.empty())
        command_line->AppendSwitchPath(switches::kLogFile, file_path);
    }

    if (settings.log_severity != LOGSEVERITY_DEFAULT) {
      std::string log_severity;
      switch (settings.log_severity) {
        case LOGSEVERITY_VERBOSE:
          log_severity = switches::kLogSeverity_Verbose;
          break;
        case LOGSEVERITY_INFO:
          log_severity = switches::kLogSeverity_Info;
          break;
        case LOGSEVERITY_WARNING:
          log_severity = switches::kLogSeverity_Warning;
          break;
        case LOGSEVERITY_ERROR:
          log_severity = switches::kLogSeverity_Error;
          break;
        case LOGSEVERITY_DISABLE:
          log_severity = switches::kLogSeverity_Disable;
          break;
        default:
          break;
      }
      if (!log_severity.empty())
        command_line->AppendSwitchASCII(switches::kLogSeverity, log_severity);
    }

    if (settings.javascript_flags.length > 0) {
      command_line->AppendSwitchASCII(switches::kJavaScriptFlags,
          CefString(&settings.javascript_flags));
    }

    if (settings.pack_loading_disabled) {
      command_line->AppendSwitch(switches::kDisablePackLoading);
    } else {
      if (settings.resources_dir_path.length > 0) {
        base::FilePath file_path =
            base::FilePath(CefString(&settings.resources_dir_path));
        if (!file_path.empty()) {
          command_line->AppendSwitchPath(switches::kResourcesDirPath,
                                         file_path);
        }
      }

      if (settings.locales_dir_path.length > 0) {
        base::FilePath file_path =
            base::FilePath(CefString(&settings.locales_dir_path));
        if (!file_path.empty())
          command_line->AppendSwitchPath(switches::kLocalesDirPath, file_path);
      }
    }

    if (settings.remote_debugging_port >= 1024 &&
        settings.remote_debugging_port <= 65535) {
      command_line->AppendSwitchASCII(switches::kRemoteDebuggingPort,
          base::IntToString(settings.remote_debugging_port));
    }

    if (settings.uncaught_exception_stack_size > 0) {
      command_line->AppendSwitchASCII(switches::kUncaughtExceptionStackSize,
        base::IntToString(settings.uncaught_exception_stack_size));
    }

    if (settings.context_safety_implementation != 0) {
      command_line->AppendSwitchASCII(switches::kContextSafetyImplementation,
          base::IntToString(settings.context_safety_implementation));
    }

    if (settings.windowless_rendering_enabled) {
#if defined(OS_MACOSX)
      // The delegated renderer is not yet enabled by default on OS X.
      command_line->AppendSwitch(switches::kEnableDelegatedRenderer);
#endif
    }
  }

  if (content_client_.application().get()) {
    // Give the application a chance to view/modify the command line.
    CefRefPtr<CefCommandLineImpl> commandLinePtr(
        new CefCommandLineImpl(command_line, false, false));
    content_client_.application()->OnBeforeCommandLineProcessing(
        CefString(process_type), commandLinePtr.get());
    commandLinePtr->Detach(NULL);
  }

  // Initialize logging.
  logging::LoggingSettings log_settings;
  const base::FilePath& log_file =
      command_line->GetSwitchValuePath(switches::kLogFile);
  log_settings.log_file = log_file.value().c_str();
  log_settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  log_settings.delete_old = logging::APPEND_TO_OLD_LOG_FILE;

  logging::LogSeverity log_severity = logging::LOG_INFO;

  std::string log_severity_str =
      command_line->GetSwitchValueASCII(switches::kLogSeverity);
  if (!log_severity_str.empty()) {
    if (LowerCaseEqualsASCII(log_severity_str,
                             switches::kLogSeverity_Verbose)) {
      log_severity = logging::LOG_VERBOSE;
    } else if (LowerCaseEqualsASCII(log_severity_str,
                                    switches::kLogSeverity_Warning)) {
      log_severity = logging::LOG_WARNING;
    } else if (LowerCaseEqualsASCII(log_severity_str,
                                    switches::kLogSeverity_Error)) {
      log_severity = logging::LOG_ERROR;
    } else if (LowerCaseEqualsASCII(log_severity_str,
                                    switches::kLogSeverity_Disable)) {
      log_severity = LOGSEVERITY_DISABLE;
    }
  }

  if (log_severity == LOGSEVERITY_DISABLE) {
    log_settings.logging_dest = logging::LOG_NONE;
  } else {
    log_settings.logging_dest = logging::LOG_TO_ALL;
    logging::SetMinLogLevel(log_severity);
  }

  logging::InitLogging(log_settings);

  content::SetContentClient(&content_client_);

  return false;
}

void CefMainDelegate::PreSandboxStartup() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  const std::string& process_type =
      command_line->GetSwitchValueASCII(switches::kProcessType);

  if (command_line->HasSwitch(switches::kEnableCrashReporter)) {
    crash_reporter::SetCrashReporterClient(g_crash_reporter_client.Pointer());
#if defined(OS_MACOSX)
    base::mac::DisableOSCrashDumps();
    breakpad::InitCrashReporter(process_type);
    breakpad::InitCrashProcessInfo(process_type);
#elif defined(OS_POSIX) && !defined(OS_MACOSX)
    if (process_type != switches::kZygoteProcess)
      breakpad::InitCrashReporter(process_type);
#elif defined(OS_WIN)
    UINT new_flags =
        SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;
    UINT existing_flags = SetErrorMode(new_flags);
    SetErrorMode(existing_flags | new_flags);
    breakpad::InitCrashReporter(process_type);
#endif
  }

  if (!command_line->HasSwitch(switches::kProcessType)) {
    // Only override these paths when executing the main process.
#if defined(OS_MACOSX)
    OverrideChildProcessPath();
#endif

    OverridePepperFlashSystemPluginPath();

    // Paths used to locate spell checking dictionary files.
    const base::FilePath& user_data_path = GetUserDataPath();
    PathService::Override(chrome::DIR_USER_DATA, user_data_path);
    PathService::OverrideAndCreateIfNeeded(
        chrome::DIR_APP_DICTIONARIES,
        user_data_path.AppendASCII("Dictionaries"),
        false,  // May not be an absolute path.
        true);  // Create if necessary.
  }

  if (command_line->HasSwitch(switches::kDisablePackLoading))
    content_client_.set_pack_loading_disabled(true);

  InitializeResourceBundle();
}

int CefMainDelegate::RunProcess(
    const std::string& process_type,
    const content::MainFunctionParams& main_function_params) {
  if (process_type.empty()) {
    const CefSettings& settings = CefContext::Get()->settings();
    if (!settings.multi_threaded_message_loop) {
      // Use our own browser process runner.
      browser_runner_.reset(content::BrowserMainRunner::Create());

      // Initialize browser process state. Results in a call to
      // CefBrowserMain::PreMainMessageLoopStart() which creates the UI message
      // loop.
      int exit_code = browser_runner_->Initialize(main_function_params);
      if (exit_code >= 0)
        return exit_code;
    } else {
      // Run the UI on a separate thread.
      scoped_ptr<base::Thread> thread;
      thread.reset(new CefUIThread(main_function_params));
      base::Thread::Options options;
      options.message_loop_type = base::MessageLoop::TYPE_UI;
      if (!thread->StartWithOptions(options)) {
        NOTREACHED() << "failed to start UI thread";
        return 1;
      }
      ui_thread_.swap(thread);
    }

    return 0;
  }

  return -1;
}

void CefMainDelegate::ProcessExiting(const std::string& process_type) {
  ResourceBundle::CleanupSharedInstance();
}

#if defined(OS_POSIX) && !defined(OS_ANDROID) && !defined(OS_MACOSX)
void CefMainDelegate::ZygoteForked() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableCrashReporter)) {
    const std::string& process_type = command_line->GetSwitchValueASCII(
        switches::kProcessType);
    breakpad::InitCrashReporter(process_type);
  }
}
#endif

content::ContentBrowserClient* CefMainDelegate::CreateContentBrowserClient() {
  browser_client_.reset(new CefContentBrowserClient);
  return browser_client_.get();
}

content::ContentRendererClient*
    CefMainDelegate::CreateContentRendererClient() {
  renderer_client_.reset(new CefContentRendererClient);
  return renderer_client_.get();
}

content::ContentUtilityClient* CefMainDelegate::CreateContentUtilityClient() {
  utility_client_.reset(new CefContentUtilityClient);
  return utility_client_.get();
}

void CefMainDelegate::ShutdownBrowser() {
  if (browser_runner_.get()) {
    browser_runner_->Shutdown();
    browser_runner_.reset(NULL);
  }
  if (ui_thread_.get()) {
    // Blocks until the thread has stopped.
    ui_thread_->Stop();
    ui_thread_.reset();
  }
}

void CefMainDelegate::InitializeResourceBundle() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  base::FilePath cef_pak_file, cef_100_percent_pak_file,
                 cef_200_percent_pak_file, devtools_pak_file, locales_dir;

  if (!content_client_.pack_loading_disabled()) {
    base::FilePath resources_dir;
    if (command_line->HasSwitch(switches::kResourcesDirPath)) {
      resources_dir =
          command_line->GetSwitchValuePath(switches::kResourcesDirPath);
    }
    if (resources_dir.empty())
      resources_dir = GetResourcesFilePath();

    if (!resources_dir.empty()) {
      CHECK(resources_dir.IsAbsolute());
      cef_pak_file = resources_dir.Append(FILE_PATH_LITERAL("cef.pak"));
      cef_100_percent_pak_file =
          resources_dir.Append(FILE_PATH_LITERAL("cef_100_percent.pak"));
      cef_200_percent_pak_file =
          resources_dir.Append(FILE_PATH_LITERAL("cef_200_percent.pak"));
      devtools_pak_file =
          resources_dir.Append(FILE_PATH_LITERAL("devtools_resources.pak"));
    }

    if (command_line->HasSwitch(switches::kLocalesDirPath))
      locales_dir = command_line->GetSwitchValuePath(switches::kLocalesDirPath);

    if (!locales_dir.empty())
      PathService::Override(ui::DIR_LOCALES, locales_dir);
  }

  std::string locale = command_line->GetSwitchValueASCII(switches::kLang);
  DCHECK(!locale.empty());

  const std::string loaded_locale =
      ui::ResourceBundle::InitSharedInstanceWithLocale(
          locale,
          &content_client_,
          ui::ResourceBundle::LOAD_COMMON_RESOURCES);
  ResourceBundle& resource_bundle = ResourceBundle::GetSharedInstance();

  if (!content_client_.pack_loading_disabled()) {
    if (loaded_locale.empty())
      LOG(ERROR) << "Could not load locale pak for " << locale;

    content_client_.set_allow_pack_file_load(true);

    if (base::PathExists(cef_pak_file)) {
      resource_bundle.AddDataPackFromPath(cef_pak_file, ui::SCALE_FACTOR_NONE);
    } else {
      LOG(ERROR) << "Could not load cef.pak";
    }

    // On OS X and Linux/Aura always load the 1x data pack first as the 2x data
    // pack contains both 1x and 2x images.
    const bool load_100_percent =
#if defined(OS_WIN)
        IsScaleFactorSupported(ui::SCALE_FACTOR_100P);
#else
        true;
#endif

    if (load_100_percent) {
      if (base::PathExists(cef_100_percent_pak_file)) {
        resource_bundle.AddDataPackFromPath(
            cef_100_percent_pak_file, ui::SCALE_FACTOR_100P);
      } else {
        LOG(ERROR) << "Could not load cef_100_percent.pak";
      }
    }

    if (IsScaleFactorSupported(ui::SCALE_FACTOR_200P)) {
      if (base::PathExists(cef_200_percent_pak_file)) {
        resource_bundle.AddDataPackFromPath(
            cef_200_percent_pak_file, ui::SCALE_FACTOR_200P);
      } else {
        LOG(ERROR) << "Could not load cef_200_percent.pak";
      }
    }

    if (base::PathExists(devtools_pak_file)) {
      resource_bundle.AddDataPackFromPath(
          devtools_pak_file, ui::SCALE_FACTOR_NONE);
    }

    content_client_.set_allow_pack_file_load(false);
  }
}
