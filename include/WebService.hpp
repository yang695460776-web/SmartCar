#ifndef Web_Service_hpp
#define Web_Service_hpp

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <MosControl.hpp>
#include <MFRC522.h>
#include <Preferences.h>
#include <ElegantOTA.h>

Preferences nfcPreferences;

// WiFi AP 配置
const char* ssid = "Dao";
const char* password = "695460776";

// OTA 页面认证信息（访问 /update 需要输入）
const char* otaUser = "HHHH";
const char* otaPassword = "Ywh476900";

// 维护模式标记（持久化到 NVS）
const char* systemNamespace = "system";
const char* maintenanceKey = "maintenance";
const unsigned long MAINTENANCE_TIMEOUT_MS = 10UL * 60UL * 1000UL;
const unsigned long BOOT_AP_WINDOW_MS = 180UL * 1000UL;
const unsigned long ACCESS_WINDOW_MS = 900UL * 1000UL;
const bool NFC_ALWAYS_ON = true;

// Web 服务器
WebServer server(80);
DNSServer dnsServer;
const uint16_t DNS_PORT = 53;
extern MosControl mosCtrl;

// In-memory log buffer (viewable from web UI). Keeps last N lines.
static const size_t WEB_LOG_MAX_LINES = 120;
static String webLogLines[WEB_LOG_MAX_LINES];
static size_t webLogHead = 0;
static size_t webLogCount = 0;

inline void webLogAppend(const String& line) {
    // Prefix with millis timestamp for easier field debugging.
    String msg = "[" + String(millis()) + "ms] " + line;

    webLogLines[webLogHead] = msg;
    webLogHead = (webLogHead + 1) % WEB_LOG_MAX_LINES;
    if (webLogCount < WEB_LOG_MAX_LINES) {
        webLogCount++;
    }
}

inline void webLogPrintln(const String& line) {
    Serial.println(line);
    webLogAppend(line);
}

inline void webLogPrint(const String& text) {
    Serial.print(text);
    // For simplicity, only persist println() as full lines.
}

// NFC 相关全局状态
extern MFRC522 mfrc522;
byte authorizedCards[5][4] = {0};  // 最多保存 5 张授权卡
int authorizedCardsCount = 0;
bool isCardRegistrationMode = false;
bool isCardDeletionMode = false;
byte currentCardUID[4];

// 维护模式运行时状态
bool maintenanceMode = false;
unsigned long maintenanceModeStartMs = 0;
bool apEnabled = false;
bool nfcScanEnabled = false;
unsigned long accessWindowUntilMs = 0;

// 从 NVS 读取维护模式标记，重启后仍可恢复状态
bool readMaintenanceFlag() {
    Preferences prefs;
    prefs.begin(systemNamespace, true);
    bool enabled = prefs.getBool(maintenanceKey, false);
    prefs.end();
    return enabled;
}

// 写入维护模式标记
void writeMaintenanceFlag(bool enabled) {
    Preferences prefs;
    prefs.begin(systemNamespace, false);
    prefs.putBool(maintenanceKey, enabled);
    prefs.end();
}

// 启动阶段调用，加载维护模式状态
void initMaintenanceMode() {
    maintenanceMode = readMaintenanceFlag();
    maintenanceModeStartMs = maintenanceMode ? millis() : 0;
}

// 提供给 main.cpp 查询当前模式
bool isMaintenanceMode() {
    return maintenanceMode;
}

void activateAccessWindow(unsigned long durationMs = ACCESS_WINDOW_MS);
void onFingerprintTouchDetected();
bool isNfcScanActive();
void serviceAccessWindow();
bool requireWebAuth();

void onMatched() {
    MosControl::FunctionState action = mosCtrl.toggleOnOffNfc();
    if (action == MosControl::POWER_ON) {
        webLogPrintln("动作：开 (POWER_ON)");
    } else if (action == MosControl::UNLOCK) {
        webLogPrintln("动作：关 (UNLOCK)");
    } else if (action == MosControl::LOCK) {
        webLogPrintln("动作：上锁 (LOCK)");
    } else {
        webLogPrintln("动作：未知");
    }
}

void onFingerprintMatched() {
    activateAccessWindow();
    MosControl::FunctionState action = mosCtrl.toggleOnOffFingerprint();
    if (action == MosControl::POWER_ON) {
        webLogPrintln("指纹动作：开 (POWER_ON)");
    } else if (action == MosControl::UNLOCK) {
        webLogPrintln("指纹动作：关 (UNLOCK)");
    } else if (action == MosControl::LOCK) {
        webLogPrintln("指纹动作：上锁 (LOCK)");
    } else {
        webLogPrintln("指纹动作：未知");
    }
}

void esp32_ap_setup() {
    if (apEnabled) {
        return;
    }
    WiFi.softAP(ssid, password);
    webLogPrint("MAC Address: ");
    webLogPrintln(WiFi.softAPmacAddress());
    webLogPrint("IP Address: ");
    webLogPrintln(WiFi.softAPIP().toString());
    // 将任意域名解析到 AP 网关，触发手机门户检测流程
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    apEnabled = true;
}

void esp32_ap_stop() {
    if (!apEnabled) {
        return;
    }
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    apEnabled = false;
}

void activateAccessWindow(unsigned long durationMs) {
    nfcScanEnabled = true;
    accessWindowUntilMs = millis() + durationMs;
    esp32_ap_setup();
}

void onFingerprintTouchDetected() {
    activateAccessWindow();
}

bool isNfcScanActive() {
    return NFC_ALWAYS_ON || maintenanceMode || nfcScanEnabled;
}

void serviceAccessWindow() {
    if (maintenanceMode || !nfcScanEnabled) {
        return;
    }

    if ((long)(millis() - accessWindowUntilMs) >= 0) {
        // NFC 常开模式下不关闭 NFC，仅关闭 AP
        nfcScanEnabled = false;
        esp32_ap_stop();
        webLogPrintln("访问窗口超时：已关闭 AP（NFC 常开）");
    }
}

bool requireWebAuth() {
    if (!server.authenticate(otaUser, otaPassword)) {
        server.requestAuthentication();
        return false;
    }
    return true;
}

void captive_portal_redirect() {
    String redirectUrl = String("http://") + WiFi.softAPIP().toString() + "/";
    server.sendHeader("Location", redirectUrl, true);
    server.send(302, "text/plain", "");
}

String getCardUIDString(byte* uid) {
    String uidStr;
    for (byte i = 0; i < 4; i++) {
        if (uid[i] < 0x10) uidStr += "0";
        uidStr += String(uid[i], HEX);
        if (i < 3) uidStr += " ";
    }
    return uidStr;
}

String uiPage(const String& title, const String& body, const String& script = "") {
    String html = "<html><head><meta charset='UTF-8'>"
                  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                  "<title>" + title + "</title>"
                  "<style>"
                  ":root{--bg:#f4f8fb;--card:#ffffff;--text:#1f2a37;--muted:#5b6777;--line:#dbe4ee;"
                  "--primary:#1769aa;--primary2:#0f4c81;--ok:#1f8b4c;--warn:#b04a2a;}"
                  "*{box-sizing:border-box;}body{margin:0;font-family:'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif;"
                  "background:radial-gradient(1200px 600px at 10% -20%,#e2f1ff 0%,transparent 60%),"
                  "radial-gradient(900px 500px at 100% 0%,#e8f8ef 0%,transparent 55%),var(--bg);color:var(--text);}"
                  ".wrap{max-width:980px;margin:20px auto;padding:12px;}"
                  ".panel{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:20px;"
                  "box-shadow:0 10px 30px rgba(20,40,80,.08);}"
                  "h1{margin:0 0 10px;font-size:32px;line-height:1.2;}p{color:var(--muted);font-size:18px;}"
                  ".row{display:flex;gap:12px;flex-wrap:wrap;justify-content:center;margin-top:12px;}"
                  "button,.btn{border:none;border-radius:12px;padding:12px 18px;font-size:18px;cursor:pointer;"
                  "text-decoration:none;display:inline-block;}"
                  ".btn-primary{background:linear-gradient(135deg,var(--primary),var(--primary2));color:#fff;}"
                  ".btn-ok{background:linear-gradient(135deg,#2ca15f,#1f8b4c);color:#fff;}"
                  ".btn-warn{background:linear-gradient(135deg,#cc5a36,#a9442a);color:#fff;}"
                  ".btn-ghost{background:#edf3fb;color:#1f4c7a;}"
                  "table{width:100%;border-collapse:collapse;margin-top:10px;font-size:16px;}"
                  "th,td{padding:12px;border-bottom:1px solid var(--line);text-align:left;}"
                  "th{color:#3d4b5f;background:#f7fbff;}"
                  ".status{font-size:20px;color:#1f4c7a;background:#eef6ff;border:1px solid #d7e8fb;border-radius:12px;padding:12px;}"
                  "@media (max-width:700px){h1{font-size:26px;}p{font-size:16px;}button,.btn{width:100%;font-size:17px;}}"
                  "</style>" + script + "</head><body><div class='wrap'><div class='panel'>" + body + "</div></div></body></html>";
    return html;
}

// 将授权卡列表持久化到 NVS
void saveCardsToStorage() {
    nfcPreferences.begin("nfc-cards", false);
    nfcPreferences.putBytes("cards", authorizedCards, sizeof(authorizedCards));
    nfcPreferences.putUInt("count", authorizedCardsCount);
    nfcPreferences.end();
}

// 从 NVS 加载授权卡列表
void loadCardsFromStorage() {
    nfcPreferences.begin("nfc-cards", true);
    size_t len = nfcPreferences.getBytes("cards", authorizedCards, sizeof(authorizedCards));
    authorizedCardsCount = nfcPreferences.getUInt("count", 0);
    nfcPreferences.end();

    if (len != sizeof(authorizedCards)) {
        authorizedCardsCount = 0;
    }
}

void handleCardOperations() {
    if ((isCardRegistrationMode || isCardDeletionMode) &&
        mfrc522.PICC_IsNewCardPresent() &&
        mfrc522.PICC_ReadCardSerial()) {

        memcpy(currentCardUID, mfrc522.uid.uidByte, 4);
        mfrc522.PICC_HaltA();

        if (isCardRegistrationMode) {
            if (authorizedCardsCount < 5) {
                bool cardExists = false;
                for (int i = 0; i < authorizedCardsCount; i++) {
                    if (memcmp(authorizedCards[i], currentCardUID, 4) == 0) {
                        cardExists = true;
                        break;
                    }
                }

                if (!cardExists) {
                    memcpy(authorizedCards[authorizedCardsCount], currentCardUID, 4);
                    authorizedCardsCount++;
                    saveCardsToStorage();
                    Serial.println("新卡片录入成功，UID: " + getCardUIDString(currentCardUID));
                } else {
                    Serial.println("卡片已存在，UID: " + getCardUIDString(currentCardUID));
                }
            } else {
                Serial.println("已达到最大授权卡数量（5张）");
            }
            isCardRegistrationMode = false;
        } else if (isCardDeletionMode) {
            for (int i = 0; i < authorizedCardsCount; i++) {
                if (memcmp(authorizedCards[i], currentCardUID, 4) == 0) {
                    for (int j = i; j < authorizedCardsCount - 1; j++) {
                        memcpy(authorizedCards[j], authorizedCards[j + 1], 4);
                    }
                    authorizedCardsCount--;
                    saveCardsToStorage();
                    Serial.println("卡片已删除，UID: " + getCardUIDString(currentCardUID));
                    break;
                }
            }
            isCardDeletionMode = false;
        }
    }
}

void add_card_callback_func() {
    isCardRegistrationMode = true;
    isCardDeletionMode = false;
    String script = "<script>function checkStatus(){fetch('/check_status?mode=add').then(r=>r.text()).then(t=>{"
                    "document.getElementById('status').innerHTML=t;"
                    "if(!t.includes('等待')&&!t.includes('请将')){setTimeout(()=>{window.location.href='/manage_cards';},1200);}else{setTimeout(checkStatus,1000);}"
                    "});}window.onload=checkStatus;</script>";
    String body = "<h1>卡片录入</h1><p>把卡片贴近识别区，系统会自动完成录入。</p>"
                  "<div id='status' class='status'>请将卡片靠近读卡器...</div>"
                  "<div class='row'><button class='btn btn-ghost' onclick=\"window.location.href='/manage_cards'\">取消并返回</button></div>";
    server.send(200, "text/html", uiPage("卡片录入", body, script));
}

void delete_card_callback_func() {
    isCardDeletionMode = true;
    isCardRegistrationMode = false;
    String script = "<script>function checkStatus(){fetch('/check_status?mode=del').then(r=>r.text()).then(t=>{"
                    "document.getElementById('status').innerHTML=t;"
                    "if(!t.includes('等待')&&!t.includes('请将')){setTimeout(()=>{window.location.href='/manage_cards';},1200);}else{setTimeout(checkStatus,1000);}"
                    "});}window.onload=checkStatus;</script>";
    String body = "<h1>卡片删除</h1><p>把要删除的卡片贴近识别区，完成后会自动返回。</p>"
                  "<div id='status' class='status'>请将要删除的卡片靠近读卡器...</div>"
                  "<div class='row'><button class='btn btn-ghost' onclick=\"window.location.href='/manage_cards'\">取消并返回</button></div>";
    server.send(200, "text/html", uiPage("卡片删除", body, script));
}

void manage_cards_callback_func() {
    String body = "<h1>卡片管理</h1><p>已授权卡片列表</p><table><tr><th>序号</th><th>卡片UID</th><th>操作</th></tr>";

    for (int i = 0; i < authorizedCardsCount; i++) {
        body += "<tr><td>" + String(i + 1) + "</td><td>" + getCardUIDString(authorizedCards[i]) +
                "</td><td><button onclick=\"window.location.href='/delete_card?uid=" +
                getCardUIDString(authorizedCards[i]) + "'\">删除</button></td></tr>";
    }

    body += "</table><div class='row'>"
            "<button class='btn btn-ok' onclick=\"window.location.href='/add_card'\">添加新卡片</button>"
            "<button class='btn btn-primary' onclick=\"window.location.href='/'\">返回主页</button>"
            "</div>";
    server.send(200, "text/html", uiPage("卡片管理", body));
}

void add_fingerprint_callback_func() {
    isCardRegistrationMode = true;
    isCardDeletionMode = false;

    String script = "<script>function checkStatus(){fetch('/check_status?mode=add').then(r=>r.text()).then(t=>{"
                    "document.getElementById('status').innerHTML=t;"
                    "if(!t.includes('等待')&&!t.includes('请将')){setTimeout(()=>{window.location.href='/manage_fingerprints';},1200);}else{setTimeout(checkStatus,1000);}"
                    "});}window.onload=checkStatus;</script>";
    String body = "<h1>指纹录入</h1><p>请按住手指并贴近指纹识别模块。</p>"
                  "<div id='status' class='status'>等待指纹录入...</div>"
                  "<div class='row'><button class='btn btn-ghost' onclick=\"window.location.href='/manage_fingerprints'\">取消并返回</button></div>";
    server.send(200, "text/html", uiPage("指纹录入", body, script));
}

void delete_fingerprint_callback_func() {
    isCardDeletionMode = true;
    isCardRegistrationMode = false;

    String script = "<script>function checkStatus(){fetch('/check_status?mode=del').then(r=>r.text()).then(t=>{"
                    "document.getElementById('status').innerHTML=t;"
                    "if(!t.includes('等待')&&!t.includes('请将')){setTimeout(()=>{window.location.href='/manage_fingerprints';},1200);}else{setTimeout(checkStatus,1000);}"
                    "});}window.onload=checkStatus;</script>";
    String body = "<h1>指纹删除</h1><p>请按要删除的手指并贴近指纹模块。</p>"
                  "<div id='status' class='status'>等待删除指纹...</div>"
                  "<div class='row'><button class='btn btn-ghost' onclick=\"window.location.href='/manage_fingerprints'\">取消并返回</button></div>";
    server.send(200, "text/html", uiPage("指纹删除", body, script));
}

void manage_fingerprints_callback_func() {
    String body = "<h1>指纹管理</h1>"
                  "<p>当前提供指纹录入/删除入口，后续可扩展指纹 ID 明细。</p>"
                  "<div class='row'>"
                  "<button class='btn btn-ok' onclick=\"window.location.href='/add_fingerprint'\">添加新指纹</button>"
                  "<button class='btn btn-warn' onclick=\"window.location.href='/delete_fingerprint'\">删除指纹</button>"
                  "<button class='btn btn-primary' onclick=\"window.location.href='/'\">返回主页</button>"
                  "</div>";
    server.send(200, "text/html", uiPage("指纹管理", body));
}

void check_status_callback_func() {
    String mode = server.arg("mode");

    if (mode == "add") {
        if (!isCardRegistrationMode) {
            if (authorizedCardsCount >= 5) {
                server.send(200, "text/plain", "已达到最大授权卡数量（5张）");
            } else {
                server.send(200, "text/plain", "录入流程已完成");
            }
        } else {
            server.send(200, "text/plain", "请将凭证靠近识别模块，等待完成...");
        }
    } else if (mode == "del") {
        if (!isCardDeletionMode) {
            server.send(200, "text/plain", "删除流程已完成");
        } else {
            server.send(200, "text/plain", "请将要删除的凭证靠近识别模块...");
        }
    } else {
        server.send(400, "text/plain", "无效的模式参数");
    }
}

void web_callback_func() {
    webLogPrintln("网页开门指令触发");
    onMatched();
    webLogPrintln("Door action executed");

    String body = "<h1>门锁动作已执行</h1><p>设备已收到开门指令。</p>"
                  "<div class='row'><a class='btn btn-primary' href='/'>返回主页</a></div>";
    server.send(200, "text/html", uiPage("开门完成", body));
}

void esp32_web_create() {
    loadCardsFromStorage();

    server.on("/", []() {
        if (!requireWebAuth()) return;
        String body = "<h1>智能车锁控制台</h1>"
                      "<p>欢迎回来，选择你要执行的操作。</p>"
                      "<div class='row'>"
                      "<button class='btn btn-primary' onclick=\"window.location.href='/open'\">一键开/关</button>"
                      "<button class='btn btn-ok' onclick=\"window.location.href='/power_on'\">开机 (POWER)</button>"
                      "<button class='btn btn-warn' onclick=\"window.location.href='/unlock'\">关机 (UNLOCK)</button>"
                      "<button class='btn btn-ghost' onclick=\"window.location.href='/lock'\">上锁 (LOCK)</button>"
                      "</div>"
                      "<div class='row'><a class='btn btn-ghost' href='/logs'>查看日志</a></div>"
                      "<div class='row'>"
                      "<button class='btn btn-ok' onclick=\"window.location.href='/add_card'\">添加卡片</button>"
                      "<button class='btn btn-primary' onclick=\"window.location.href='/manage_cards'\">管理卡片</button>"
                      "</div>"
                      "<div class='row'>"
                      "<button class='btn btn-ok' onclick=\"window.location.href='/add_fingerprint'\">添加指纹</button>"
                      "<button class='btn btn-primary' onclick=\"window.location.href='/manage_fingerprints'\">管理指纹</button>"
                      "</div>";
        server.send(200, "text/html", uiPage("智能车锁控制台", body));
    });

    server.on("/open", HTTP_GET, []() { if (!requireWebAuth()) return; web_callback_func(); });

    // Manual MOS actions (for wiring verification)
    server.on("/power_on", HTTP_GET, []() {
        if (!requireWebAuth()) return;
        webLogPrintln("网页手动：开机 (POWER_ON)");
        mosCtrl.doAction(MosControl::POWER_ON);
        server.send(200, "text/html", uiPage("开机", "<h1>已发送开机脉冲</h1><div class='row'><a class='btn btn-primary' href='/'>返回主页</a></div>"));
    });
    server.on("/unlock", HTTP_GET, []() {
        if (!requireWebAuth()) return;
        webLogPrintln("网页手动：关机 (UNLOCK)");
        mosCtrl.doAction(MosControl::UNLOCK);
        server.send(200, "text/html", uiPage("关机", "<h1>已发送关机脉冲 (UNLOCK)</h1><div class='row'><a class='btn btn-primary' href='/'>返回主页</a></div>"));
    });
    server.on("/lock", HTTP_GET, []() {
        if (!requireWebAuth()) return;
        webLogPrintln("网页手动：上锁 (LOCK)");
        mosCtrl.doAction(MosControl::LOCK);
        server.send(200, "text/html", uiPage("上锁", "<h1>已发送上锁脉冲 (LOCK)</h1><div class='row'><a class='btn btn-primary' href='/'>返回主页</a></div>"));
    });

    // Logs viewer
    server.on("/logs", HTTP_GET, []() {
        if (!requireWebAuth()) return;
        String script =
            "<script>"
            "function refresh(){fetch('/logs.txt',{cache:'no-store'}).then(r=>r.text()).then(t=>{"
            "document.getElementById('log').textContent=t;"
            "var el=document.getElementById('log'); el.scrollTop=el.scrollHeight;"
            "});}"
            "window.onload=function(){refresh(); setInterval(refresh,1000);};"
            "</script>";
        String body =
            "<h1>运行日志</h1>"
            "<p>显示最近 " + String((int)WEB_LOG_MAX_LINES) + " 行。打开页面时每秒刷新一次。</p>"
            "<div id='log' style='height:60vh;overflow:auto;white-space:pre-wrap;font-family:Consolas,monospace;"
            "background:#0b1220;color:#d6e3ff;border-radius:12px;padding:12px;border:1px solid #23304a;'></div>"
            "<div class='row'><a class='btn btn-primary' href='/'>返回主页</a></div>";
        server.send(200, "text/html", uiPage("运行日志", body, script));
    });

    server.on("/logs.txt", HTTP_GET, []() {
        if (!requireWebAuth()) return;
        String out;
        out.reserve(8192);
        size_t start = (webLogHead + WEB_LOG_MAX_LINES - webLogCount) % WEB_LOG_MAX_LINES;
        for (size_t i = 0; i < webLogCount; i++) {
            size_t idx = (start + i) % WEB_LOG_MAX_LINES;
            out += webLogLines[idx];
            out += "\n";
        }
        server.send(200, "text/plain; charset=utf-8", out);
    });

    server.on("/add_card", HTTP_GET, []() { if (!requireWebAuth()) return; add_card_callback_func(); });
    server.on("/delete_card", HTTP_GET, []() { if (!requireWebAuth()) return; delete_card_callback_func(); });
    server.on("/manage_cards", HTTP_GET, []() { if (!requireWebAuth()) return; manage_cards_callback_func(); });
    server.on("/add_fingerprint", HTTP_GET, []() { if (!requireWebAuth()) return; add_fingerprint_callback_func(); });
    server.on("/delete_fingerprint", HTTP_GET, []() { if (!requireWebAuth()) return; delete_fingerprint_callback_func(); });
    server.on("/manage_fingerprints", HTTP_GET, []() { if (!requireWebAuth()) return; manage_fingerprints_callback_func(); });
    server.on("/check_status", HTTP_GET, []() { if (!requireWebAuth()) return; check_status_callback_func(); });
    // 常见系统联网探测地址，统一重定向到门户主页
    server.on("/generate_204", HTTP_GET, captive_portal_redirect);
    server.on("/gen_204", HTTP_GET, captive_portal_redirect);
    server.on("/hotspot-detect.html", HTTP_GET, captive_portal_redirect);
    server.on("/connecttest.txt", HTTP_GET, captive_portal_redirect);
    server.on("/ncsi.txt", HTTP_GET, captive_portal_redirect);
    server.on("/fwlink", HTTP_GET, captive_portal_redirect);
    server.onNotFound(captive_portal_redirect);

    server.begin();
    ElegantOTA.begin(&server, otaUser, otaPassword);
    webLogPrintln("[OTA] OTA 服务已启动：访问 http://192.168.4.1/update 上传新固件");
    webLogPrintln("提示：访问 http://192.168.4.1/logs 查看运行日志");

    while (1) {
        serviceAccessWindow();
        dnsServer.processNextRequest();
        server.handleClient();
        handleCardOperations();
        delay(100);
    }
}

#endif
