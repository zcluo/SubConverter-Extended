#include <csignal>
#include <iostream>
#include <string>
#include <unistd.h>

#include <dirent.h>
#include <sys/types.h>

#include "config/ruleset.h"
#include "handler/interfaces.h"
#include "handler/settings.h"
#include "handler/webget.h"
#include "script/cron.h"
#include "server/socket.h"
#include "server/webserver.h"
#include "utils/defer.h"
#include "utils/file_extra.h"
#include "utils/logger.h"
#include "utils/network.h"
#include "utils/rapidjson_extra.h"
#include "utils/system.h"
#include "utils/urlencode.h"
#include "version.h"

// #include "vfs.h"

WebServer webServer;

static const char *VERSION_FAVICON_DARK = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" role="img" aria-label="SubConverter-Extended icon">
  <defs>
    <linearGradient id="bg" x1="10" y1="8" x2="54" y2="56" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#0f172a"/>
      <stop offset="1" stop-color="#1e293b"/>
    </linearGradient>
    <linearGradient id="cyan" x1="18" y1="20" x2="46" y2="20" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#38bdf8"/>
      <stop offset="1" stop-color="#22d3ee"/>
    </linearGradient>
    <linearGradient id="green" x1="46" y1="44" x2="18" y2="44" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#34d399"/>
      <stop offset="1" stop-color="#84cc16"/>
    </linearGradient>
  </defs>
  <rect width="64" height="64" rx="14" fill="url(#bg)"/>
  <path d="M18 20h25" fill="none" stroke="url(#cyan)" stroke-width="6" stroke-linecap="round"/>
  <path d="M40 11l10 9-10 9" fill="none" stroke="#67e8f9" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M46 44H21" fill="none" stroke="url(#green)" stroke-width="6" stroke-linecap="round"/>
  <path d="M24 35l-10 9 10 9" fill="none" stroke="#bef264" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M24 31c0-6 4-10 9-10 3 0 6 1 8 3" fill="none" stroke="#f8fafc" stroke-width="5" stroke-linecap="round"/>
  <path d="M40 33c0 6-4 10-9 10-3 0-6-1-8-3" fill="none" stroke="#f8fafc" stroke-width="5" stroke-linecap="round"/>
</svg>)svg";

static const char *VERSION_FAVICON_LIGHT = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" role="img" aria-label="SubConverter-Extended icon">
  <defs>
    <linearGradient id="bg" x1="10" y1="8" x2="54" y2="56" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#ffffff"/>
      <stop offset="1" stop-color="#e8eef7"/>
    </linearGradient>
    <linearGradient id="cyan" x1="18" y1="20" x2="46" y2="20" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#0284c7"/>
      <stop offset="1" stop-color="#0891b2"/>
    </linearGradient>
    <linearGradient id="green" x1="46" y1="44" x2="18" y2="44" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#059669"/>
      <stop offset="1" stop-color="#65a30d"/>
    </linearGradient>
  </defs>
  <rect width="64" height="64" rx="14" fill="url(#bg)"/>
  <rect x="1.5" y="1.5" width="61" height="61" rx="12.5" fill="none" stroke="#cbd5e1" stroke-width="3"/>
  <path d="M18 20h25" fill="none" stroke="url(#cyan)" stroke-width="6" stroke-linecap="round"/>
  <path d="M40 11l10 9-10 9" fill="none" stroke="#06b6d4" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M46 44H21" fill="none" stroke="url(#green)" stroke-width="6" stroke-linecap="round"/>
  <path d="M24 35l-10 9 10 9" fill="none" stroke="#84cc16" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M24 31c0-6 4-10 9-10 3 0 6 1 8 3" fill="none" stroke="#172033" stroke-width="5" stroke-linecap="round"/>
  <path d="M40 33c0 6-4 10-9 10-3 0-6-1-8-3" fill="none" stroke="#172033" stroke-width="5" stroke-linecap="round"/>
</svg>)svg";

std::string versionFaviconDark(Request &, Response &response) {
  response.headers["Cache-Control"] = "public, max-age=86400";
  return VERSION_FAVICON_DARK;
}

std::string versionFaviconLight(Request &, Response &response) {
  response.headers["Cache-Control"] = "public, max-age=86400";
  return VERSION_FAVICON_LIGHT;
}

#ifndef _WIN32
void SetConsoleTitle(const std::string &title) {
  system(std::string("echo \"\\033]0;" + title + R"(\007\c")").data());
}
#endif // _WIN32

void setcd(std::string &file) {
  char szTemp[4096] = {}, filename[256] = {};
  std::string path;
#ifdef _WIN32
  char *pname = NULL;
  DWORD retVal = GetFullPathName(file.data(), 1023, szTemp, &pname);
  if (!retVal)
    return;
  strcpy(filename, pname);
  strrchr(szTemp, '\\')[1] = '\0';
#else
  char *ret = realpath(file.data(), szTemp);
  if (ret == nullptr)
    return;
  ret = strcpy(filename, strrchr(szTemp, '/') + 1);
  if (ret == nullptr)
    return;
  strrchr(szTemp, '/')[1] = '\0';
#endif // _WIN32
  file.assign(filename);
  path.assign(szTemp);
  chdir(path.data());
}

void chkArg(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-cfw") == 0) {
      global.CFWChildProcess = true;
      global.updateRulesetOnRequest = true;
    } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
      if (i < argc - 1)
        global.prefPath.assign(argv[++i]);
    } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gen") == 0) {
      global.generatorMode = true;
    } else if (strcmp(argv[i], "--artifact") == 0) {
      if (i < argc - 1)
        global.generateProfiles.assign(argv[++i]);
    } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--log") == 0) {
      if (i < argc - 1)
        if (freopen(argv[++i], "a", stderr) == nullptr)
          std::cerr << "Error redirecting output to file.\n";
    }
  }
}

void signal_handler(int sig) {
  // std::cerr<<"Interrupt signal "<<sig<<" received. Exiting gracefully...\n";
  writeLog(0,
           "Interrupt signal " + std::to_string(sig) +
               " received. Exiting gracefully...",
           LOG_LEVEL_FATAL);
  switch (sig) {
#ifndef _WIN32
  case SIGHUP:
  case SIGQUIT:
#endif // _WIN32
  case SIGTERM:
  case SIGINT:
    webServer.stop_web_server();
    break;
  }
}

void cron_tick_caller() {
  if (global.enableCron)
    cron_tick();
}

int main(int argc, char *argv[]) {
#ifndef _DEBUG
  std::string prgpath = argv[0];
  setcd(prgpath); // first switch to program directory
#endif            // _DEBUG
  if (fileExist("pref.toml"))
    global.prefPath = "pref.toml";
  else if (fileExist("pref.yml"))
    global.prefPath = "pref.yml";
  else if (!fileExist("pref.ini")) {
    if (fileExist("pref.example.toml")) {
      fileCopy("pref.example.toml", "pref.toml");
      global.prefPath = "pref.toml";
    } else if (fileExist("pref.example.yml")) {
      fileCopy("pref.example.yml", "pref.yml");
      global.prefPath = "pref.yml";
    } else if (fileExist("pref.example.ini"))
      fileCopy("pref.example.ini", "pref.ini");
  }
  chkArg(argc, argv);
  setcd(global.prefPath); // then switch to pref directory
  writeLog(0, "SubConverter " VERSION " starting up..", LOG_LEVEL_INFO);
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
    // std::cerr<<"WSAStartup failed.\n";
    writeLog(0, "WSAStartup failed.", LOG_LEVEL_FATAL);
    return 1;
  }
  UINT origcp = GetConsoleOutputCP();
  defer(SetConsoleOutputCP(origcp);) SetConsoleOutputCP(65001);
#else
  signal(SIGPIPE, SIG_IGN);
  signal(SIGABRT, SIG_IGN);
  signal(SIGHUP, signal_handler);
  signal(SIGQUIT, signal_handler);
#endif // _WIN32
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  SetConsoleTitle("SubConverter " VERSION);
  readConf();
  // vfs::vfs_read("vfs.ini");
  if (!global.updateRulesetOnRequest)
    refreshRulesets(global.customRulesets, global.rulesetsContent);

  // API_MODE and API_TOKEN environment variables removed
  // APIMode is hardcoded to true for security
  auto normalize_managed_prefix = [](const std::string &raw_value) {
    std::string value = trimWhitespace(raw_value, true, true);
    while (value.size() > 1 && value.back() == '/' && !endsWith(value, "://"))
      value.pop_back();
    return value;
  };
  global.managedConfigPrefix = normalize_managed_prefix(global.managedConfigPrefix);
  std::string env_managed_config_prefix =
      normalize_managed_prefix(getEnv("MANAGED_CONFIG_PREFIX"));
  std::string env_managed_prefix =
      normalize_managed_prefix(getEnv("MANAGED_PREFIX"));
  if (!env_managed_config_prefix.empty() && !env_managed_prefix.empty() &&
      env_managed_config_prefix != env_managed_prefix) {
    writeLog(0,
             "Both MANAGED_CONFIG_PREFIX and MANAGED_PREFIX are set. Using "
             "MANAGED_CONFIG_PREFIX.",
             LOG_LEVEL_WARNING);
  }
  if (!env_managed_config_prefix.empty())
    global.managedConfigPrefix = env_managed_config_prefix;
  else if (!env_managed_prefix.empty())
    global.managedConfigPrefix = env_managed_prefix;
  global.templateVars["managed_config_prefix"] = global.managedConfigPrefix;

  if (global.generatorMode)
    return simpleGenerator();

  /*
  webServer.append_response("GET", "/", "text/plain", [](RESPONSE_CALLBACK_ARGS)
  -> std::string
  {
      return "subconverter " VERSION " backend\n";
  });
  */

  webServer.append_response("GET", "/version/favicon-dark.svg",
                            "image/svg+xml; charset=utf-8",
                            versionFaviconDark);
  webServer.append_response("GET", "/version/favicon-light.svg",
                            "image/svg+xml; charset=utf-8",
                            versionFaviconLight);

  webServer.append_response(
      "GET", "/version", "text/html; charset=utf-8",
      [](RESPONSE_CALLBACK_ARGS) -> std::string {
        response.headers["X-Robots-Tag"] =
            "noindex, nofollow, noarchive, nosnippet, noimageindex";
        std::string build_id = BUILD_ID;
        std::string build_date = BUILD_DATE;
        auto format_build_date = [](const std::string &value) -> std::string {
          if (value.empty())
            return "unknown";
          size_t split_pos = value.find('T');
          if (split_pos == std::string::npos)
            split_pos = value.find(' ');
          std::string candidate = split_pos == std::string::npos
                                      ? value
                                      : value.substr(0, split_pos);
          if (candidate.size() >= 10 && candidate[4] == '-' &&
              candidate[7] == '-' && candidate[0] >= '0' &&
              candidate[0] <= '9' && candidate[1] >= '0' &&
              candidate[1] <= '9' && candidate[2] >= '0' &&
              candidate[2] <= '9' && candidate[3] >= '0' &&
              candidate[3] <= '9' && candidate[5] >= '0' &&
              candidate[5] <= '9' && candidate[6] >= '0' &&
              candidate[6] <= '9' && candidate[8] >= '0' &&
              candidate[8] <= '9' && candidate[9] >= '0' &&
              candidate[9] <= '9') {
            return candidate.substr(0, 10);
          }
          return candidate;
        };
        std::string build_date_display = format_build_date(build_date);
        std::string build_date_value =
            build_date.empty()
                ? R"html(<span data-lang="en">unknown</span><span data-lang="zh">未知</span>)html"
                : build_date_display;
        std::string commit_link =
            build_id.empty()
                ? ""
                : "<a "
                  "href=\"https://github.com/Aethersailor/"
                  "SubConverter-Extended/commit/" +
                      build_id +
                      "\" target=\"_blank\" rel=\"noopener noreferrer\">" +
                      build_id + "</a>";

        return R"html(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta name="robots" content="noindex, nofollow, noarchive, nosnippet, noimageindex">
    <meta name="color-scheme" content="light dark">
    <title>SubConverter-Extended</title>
    <script>
        (function () {
            var languages = navigator.languages && navigator.languages.length
                ? navigator.languages
                : [navigator.language || ""];
            var isChinese = languages.some(function (language) {
                return /^zh\b/i.test(language);
            });
            document.documentElement.lang = isChinese ? "zh-CN" : "en";
        })();
    </script>
    <link rel="icon" type="image/svg+xml" href="/version/favicon-dark.svg">
    <link rel="icon" type="image/svg+xml" href="/version/favicon-light.svg" media="(prefers-color-scheme: light)">
    <link rel="icon" type="image/svg+xml" href="/version/favicon-dark.svg" media="(prefers-color-scheme: dark)">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            /* Light Theme - 精准调优 */
            --bg-gradient: linear-gradient(135deg, #f5f7fa 0%, #e4e7eb 100%);
            --container-bg: rgba(255, 255, 255, 0.85);
            --container-border: rgba(255, 255, 255, 0.4);
            --shadow: 0 12px 40px rgba(31, 38, 135, 0.08);
            --text-primary: #1a202c;
            --text-secondary: #4a5568;
            --divider-bg: linear-gradient(90deg, transparent, rgba(0,0,0,0.08), transparent);
            --info-block-bg: rgba(0, 0, 0, 0.02);
            --info-block-border: rgba(0,0,0,0.04);
            --link-color: #3182ce;
            --link-hover: #2b6cb0;
            --header-gradient: linear-gradient(135deg, #1a202c 0%, #4a5568 100%);
            --control-bg: rgba(255, 255, 255, 0.72);
            --control-hover: rgba(255, 255, 255, 0.92);
            --control-border: rgba(26, 32, 44, 0.12);
        }

        @media (prefers-color-scheme: dark) {
            :root {
                /* Dark Theme - 极黑质感 */
                --bg-gradient: radial-gradient(circle at 50% 50%, #1a1b1e 0%, #000000 100%);
                --container-bg: rgba(28, 28, 30, 0.7);
                --container-border: rgba(255, 255, 255, 0.1);
                --shadow: 0 20px 50px rgba(0, 0, 0, 0.6);
                --text-primary: #f8f9fa;
                --text-secondary: #a0aec0;
                --divider-bg: linear-gradient(90deg, transparent, rgba(255,255,255,0.1), transparent);
                --info-block-bg: rgba(255, 255, 255, 0.04);
                --info-block-border: rgba(255,255,255,0.06);
                --link-color: #63b3ed;
                --link-hover: #90cdf4;
                --header-gradient: linear-gradient(135deg, #ffffff 0%, #90cdf4 100%);
                --control-bg: rgba(255, 255, 255, 0.08);
                --control-hover: rgba(255, 255, 255, 0.14);
                --control-border: rgba(255, 255, 255, 0.16);
            }
        }

        html[lang^="zh"] [data-lang="en"],
        html:not([lang^="zh"]) [data-lang="zh"] {
            display: none;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }
        
        body {
            font-family: 'Outfit', system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC", sans-serif;
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            background: var(--bg-gradient);
            background-attachment: fixed;
            padding: 24px;
            color: var(--text-primary);
            -webkit-font-smoothing: antialiased;
            -moz-osx-font-smoothing: grayscale;
        }
        
        .container {
            background: var(--container-bg);
            backdrop-filter: blur(24px);
            -webkit-backdrop-filter: blur(24px);
            border-radius: 32px;
            padding: 64px 50px 40px;
            max-width: 800px;
            width: 100%;
            box-shadow: var(--shadow);
            border: 1px solid var(--container-border);
            position: relative;
            animation: fadeIn 1s cubic-bezier(0.16, 1, 0.3, 1);
        }

        .container::after {
            content: "";
            position: absolute;
            inset: 0;
            border-radius: 32px;
            padding: 1px;
            background: linear-gradient(135deg, rgba(255,255,255,0.2), transparent, rgba(255,255,255,0.05));
            -webkit-mask: linear-gradient(#fff 0 0) content-box, linear-gradient(#fff 0 0);
            mask: linear-gradient(#fff 0 0) content-box, linear-gradient(#fff 0 0);
            -webkit-mask-composite: xor;
            mask-composite: exclude;
            pointer-events: none;
        }

        .lang-toggle {
            position: absolute;
            top: 18px;
            right: 18px;
            z-index: 2;
            border: 1px solid var(--control-border);
            border-radius: 999px;
            background: var(--control-bg);
            color: var(--text-primary);
            cursor: pointer;
            font: inherit;
            font-size: 0.88rem;
            font-weight: 600;
            line-height: 1;
            min-width: 72px;
            padding: 9px 14px;
            transition: background 0.2s ease, border-color 0.2s ease, transform 0.2s ease;
        }

        .lang-toggle:hover {
            background: var(--control-hover);
            transform: translateY(-1px);
        }

        .lang-toggle:focus-visible {
            outline: 3px solid rgba(99, 179, 237, 0.35);
            outline-offset: 2px;
        }
        
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(30px); }
            to { opacity: 1; transform: translateY(0); }
        }
        
        header {
            text-align: center;
            margin-bottom: 32px;
        }

        h1 {
            background: var(--header-gradient);
            -webkit-background-clip: text;
            background-clip: text;
            -webkit-text-fill-color: transparent;
            font-size: 3em;
            margin-bottom: 8px;
            font-weight: 700;
            letter-spacing: 0;
            line-height: 1.05;
        }
        
        .subtitle {
            color: var(--text-secondary);
            font-size: 1.05em;
            font-weight: 500;
            letter-spacing: 0;
            text-transform: uppercase;
            opacity: 0.6;
        }
        
        .section {
            margin: 20px 0;
            padding: 20px 25px;
            background: var(--info-block-bg);
            border-radius: 20px;
            border: 1px solid var(--info-block-border);
        }

        .section-title {
            font-size: 0.9em;
            font-weight: 700;
            color: var(--text-primary);
            margin-bottom: 15px;
            display: flex;
            align-items: center;
            gap: 8px;
            text-transform: uppercase;
            letter-spacing: 0;
            opacity: 0.8;
        }

        .description {
            color: var(--text-secondary);
            font-size: 1em;
            line-height: 1.8;
            margin-bottom: 12px;
            padding-left: 1.5em;
            position: relative;
        }

        .description::before {
            content: "•";
            position: absolute;
            left: 0.2em;
            color: var(--link-color);
            font-weight: bold;
        }

        .info-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 20px;
            margin: 20px 0;
        }
        
        .info-card {
            background: var(--info-block-bg);
            border: 1px solid var(--info-block-border);
            border-radius: 16px;
            padding: 20px;
            text-align: center;
            transition: transform 0.3s ease, box-shadow 0.3s ease;
        }
        
        .info-card:hover {
            transform: translateY(-5px);
            box-shadow: 0 10px 20px rgba(0,0,0,0.05);
        }

        .info-card .info-label {
            display: block;
            text-transform: uppercase;
            font-size: 0.75rem;
            letter-spacing: 0;
            color: var(--text-secondary);
            margin-bottom: 8px;
            font-weight: 600;
        }
        
        .info-card .info-value {
            font-size: 1.1rem;
            font-weight: 700;
            color: var(--text-primary);
            word-break: break-all;
        }
        
        .info-card .info-value a {
            font-family: 'Outfit', monospace;
            font-weight: 600;
        }
        
        a {
            color: var(--link-color);
            text-decoration: none;
            position: relative;
            transition: all 0.3s ease;
            font-weight: 500;
        }
        
        a::after {
            content: '';
            position: absolute;
            bottom: -2px;
            left: 0;
            width: 0;
            height: 2px;
            background: var(--link-color);
            transition: width 0.3s ease;
        }
        
        a:hover::after {
            width: 100%;
        }
        
        a:hover {
            color: var(--link-hover);
        }
        
        .footer {
            margin-top: 30px;
            text-align: center;
            color: var(--text-secondary);
            font-size: 0.85em;
            opacity: 0.6;
        }
        
        .footer a {
            font-weight: 400;
        }
        
        @media (max-width: 600px) {
            .container { padding: 58px 20px 30px; }
            .lang-toggle { top: 16px; right: 16px; }
            h1 { font-size: 2.2em; }
            .info-grid { grid-template-columns: 1fr; gap: 12px; }
            .section { padding: 15px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <button type="button" class="lang-toggle" id="lang-toggle">中文</button>
        <header>
            <h1>SubConverter-Extended</h1>
            <p class="subtitle">
                <span data-lang="en">A Modern Evolution of Subconverter</span>
                <span data-lang="zh">Subconverter 的现代化演进版本</span>
            </p>
        </header>

        <div class="info-grid">
            <div class="info-card">
                <span class="info-label">
                    <span data-lang="en">Version</span>
                    <span data-lang="zh">版本</span>
                </span>
                <div class="info-value">)html" VERSION R"html(</div>
            </div>
            <div class="info-card">
                <span class="info-label">
                    <span data-lang="en">Build</span>
                    <span data-lang="zh">构建</span>
                </span>
                <div class="info-value">)html" +
               commit_link + R"html(</div>
            </div>
            <div class="info-card">
                <span class="info-label">
                    <span data-lang="en">Build Date</span>
                    <span data-lang="zh">构建日期</span>
                </span>
                <div class="info-value">)html" +
               build_date_value + R"html(</div>
            </div>
        </div>
        
        <div class="section">
            <div class="section-title">
                <span data-lang="en">✨ Overview</span>
                <span data-lang="zh">✨ 项目概览</span>
            </div>
            <p class="description" data-lang="en">An enhanced implementation of subconverter, aligned with the <a href="https://github.com/MetaCubeX/mihomo/tree/Meta" target="_blank" rel="noopener noreferrer">Mihomo</a> <a href="https://wiki.metacubex.one/config/" target="_blank" rel="noopener noreferrer">configuration</a>.</p>
            <p class="description" data-lang="zh">subconverter 的增强实现，适配 <a href="https://github.com/MetaCubeX/mihomo/tree/Meta" target="_blank" rel="noopener noreferrer">Mihomo</a> <a href="https://wiki.metacubex.one/config/" target="_blank" rel="noopener noreferrer">配置规范</a>。</p>
            <p class="description" data-lang="en">Primarily for <a href="https://github.com/vernesong/OpenClash" target="_blank" rel="noopener noreferrer">OpenClash</a>, while compatible with other Clash clients.</p>
            <p class="description" data-lang="zh">主要面向 <a href="https://github.com/vernesong/OpenClash" target="_blank" rel="noopener noreferrer">OpenClash</a>，同时兼容其他 Clash 客户端。</p>
            <p class="description" data-lang="en">Dedicated companion backend for the <a href="https://github.com/Aethersailor/Custom_OpenClash_Rules" target="_blank" rel="noopener noreferrer">Custom_OpenClash_Rules</a> project.</p>
            <p class="description" data-lang="zh">作为 <a href="https://github.com/Aethersailor/Custom_OpenClash_Rules" target="_blank" rel="noopener noreferrer">Custom_OpenClash_Rules</a> 项目的专用配套后端。</p>
        </div>

        <div class="section">
            <div class="section-title">
                <span data-lang="en">🚀 Lineage</span>
                <span data-lang="zh">🚀 项目沿革</span>
            </div>
            <p class="description" data-lang="en">Originated and enhanced from: <a href="https://github.com/asdlokj1qpi233/subconverter" target="_blank" rel="noopener noreferrer">subconverter</a></p>
            <p class="description" data-lang="zh">源自并增强自：<a href="https://github.com/asdlokj1qpi233/subconverter" target="_blank" rel="noopener noreferrer">subconverter</a></p>
            <p class="description" data-lang="en">Modified and evolved by: <a href="https://github.com/Aethersailor" target="_blank" rel="noopener noreferrer">Aethersailor</a></p>
            <p class="description" data-lang="zh">由 <a href="https://github.com/Aethersailor" target="_blank" rel="noopener noreferrer">Aethersailor</a> 修改并持续演进</p>
        </div>

        <div class="footer">
            <span data-lang="en">Source Code: <a href="https://github.com/Aethersailor/SubConverter-Extended" target="_blank" rel="noopener noreferrer">GitHub</a> • License: <a href="https://www.gnu.org/licenses/gpl-3.0.html" target="_blank" rel="noopener noreferrer">GPL-3.0</a></span>
            <span data-lang="zh">源代码：<a href="https://github.com/Aethersailor/SubConverter-Extended" target="_blank" rel="noopener noreferrer">GitHub</a> • 许可证：<a href="https://www.gnu.org/licenses/gpl-3.0.html" target="_blank" rel="noopener noreferrer">GPL-3.0</a></span>
        </div>
    </div>
    <script>
        (function () {
            var toggle = document.getElementById("lang-toggle");
            if (!toggle) {
                return;
            }

            function isChinese() {
                return /^zh\b/i.test(document.documentElement.lang || "");
            }

            function updateToggle() {
                if (isChinese()) {
                    toggle.textContent = "English";
                    toggle.setAttribute("aria-label", "Switch to English");
                    toggle.setAttribute("title", "Switch to English");
                } else {
                    toggle.textContent = "中文";
                    toggle.setAttribute("aria-label", "切换到中文");
                    toggle.setAttribute("title", "切换到中文");
                }
            }

            toggle.addEventListener("click", function () {
                document.documentElement.lang = isChinese() ? "en" : "zh-CN";
                updateToggle();
            });

            updateToggle();
        })();
    </script>
</body>
</html>)html";
      });

  webServer.append_response(
      "GET", "/robots.txt", "text/plain; charset=utf-8",
      [](RESPONSE_CALLBACK_ARGS) -> std::string {
        return "User-agent: *\n"
               "Disallow: /version\n"
               "Disallow: /v\n";
      });

  /*
  webServer.append_response("GET", "/refreshrules", "text/plain",
                            [](RESPONSE_CALLBACK_ARGS) -> std::string {
                              // Token authentication disabled - no
                              // authorization required
                              refreshRulesets(global.customRulesets,
                                              global.rulesetsContent);
                              return "done\n";
                            });
  */

  /*
  webServer.append_response("GET", "/readconf", "text/plain",
                            [](RESPONSE_CALLBACK_ARGS) -> std::string {
                              // Token authentication disabled - no
                              // authorization required
                              readConf();
                              if (!global.updateRulesetOnRequest)
                                refreshRulesets(global.customRulesets,
                                                global.rulesetsContent);
                              return "done\n";
                            });
  */

  /*
  webServer.append_response(
      "POST", "/updateconf", "text/plain",
      [](RESPONSE_CALLBACK_ARGS) -> std::string {
        // Token authentication disabled - no authorization required
        std::string type = getUrlArg(request.argument, "type");
        if (type == "form" || type == "direct") {
          fileWrite(global.prefPath, request.postdata, true);
        } else {
          response.status_code = 501;
          return "Not Implemented\n";
        }

        readConf();
        if (!global.updateRulesetOnRequest)
          refreshRulesets(global.customRulesets, global.rulesetsContent);
        return "done\n";
      });
  */

  /*
  webServer.append_response("GET", "/flushcache", "text/plain",
                            [](RESPONSE_CALLBACK_ARGS) -> std::string {
                              // Token authentication disabled - no
                              // authorization required
                              flushCache();
                              return "done";
                            });
  */

  webServer.append_response("GET", "/sub", "text/plain;charset=utf-8",
                            subconverter);

  webServer.append_response("HEAD", "/sub", "text/plain", subconverter);

  /*
  webServer.append_response("GET", "/sub2clashr", "text/plain;charset=utf-8",
                            simpleToClashR);

  webServer.append_response("GET", "/surge2clash", "text/plain;charset=utf-8",
                            surgeConfToClash);
  */

  webServer.append_response("GET", "/getruleset", "text/plain;charset=utf-8",
                            getRuleset);

  /*
  webServer.append_response("GET", "/getprofile", "text/plain;charset=utf-8",
                            getProfile);

  webServer.append_response("GET", "/render", "text/plain;charset=utf-8",
                            renderTemplate);
  */

  if (!global.APIMode) {
    webServer.append_response("GET", "/get", "text/plain;charset=utf-8",
                              [](RESPONSE_CALLBACK_ARGS) -> std::string {
                                std::string url = urlDecode(
                                    getUrlArg(request.argument, "url"));
                                return webGet(url, "");
                              });

    webServer.append_response(
        "GET", "/getlocal", "text/plain;charset=utf-8",
        [](RESPONSE_CALLBACK_ARGS) -> std::string {
          return fileGet(urlDecode(getUrlArg(request.argument, "path")));
        });
  }

  // webServer.append_response("POST", "/create-profile",
  // "text/plain;charset=utf-8", createProfile);

  // webServer.append_response("GET", "/list-profiles",
  // "text/plain;charset=utf-8", listProfiles);

  std::string env_port = getEnv("PORT");
  if (!env_port.empty())
    global.listenPort = to_int(env_port, global.listenPort);
  listener_args args = {global.listenAddress,   global.listenPort,
                        global.maxPendingConns, global.maxConcurThreads,
                        cron_tick_caller,       200};
  // std::cout<<"Serving HTTP @
  // http://"<<listen_address<<":"<<listen_port<<std::endl;
  writeLog(0,
           "Starting HTTP server at http://" + global.listenAddress + ":" +
               std::to_string(global.listenPort),
           LOG_LEVEL_INFO);
  int ret = webServer.start_web_server_multi(&args);

#ifdef _WIN32
  WSACleanup();
#endif // _WIN32
  return ret;
}
