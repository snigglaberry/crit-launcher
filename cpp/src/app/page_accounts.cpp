// Crit Launcher Native — Accounts page (Microsoft Device Code, offline accounts).
#include "app/pages.h"

namespace crit {

namespace {
struct AccountsPage : PageBase {
    void Draw(App& app) override { app.DrawAccounts(app.contentRect); }
};

// Microsoft device-code sign-in state (kept in the page so it survives frames)
struct MsAuth {
    bool active = false;
    bool busy = false;
    string deviceCode;
    string userCode;
    string verifyUri;
    string message;
    string error;
    double startedAt = 0;
    double expiresAt = 0;
};
MsAuth g_ms;

void PollDeviceCode(App& app, const std::string& clientId, const std::string& tenant);

void StartMsLogin(App& app) {
    if (g_ms.active || g_ms.busy) return;
    g_ms = MsAuth();
    g_ms.busy = true;
    const string clientId = "00000000402b5328";  // Xbox Live / Minecraft launcher public client
    const string tenant = "consumers";
    App* a = &app;
    app.workers.emplace_back([a, clientId, tenant] {
        string body = "client_id=" + UrlEncode(clientId) +
                      "&scope=" + UrlEncode("XboxLive.signin offline_access") + "&prompt=select_account";
        HttpResult r = HttpRequest("POST", "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/devicecode",
                                   body, {{"Content-Type", "application/x-www-form-urlencoded"}});
        a->platform->Post([a, r] {
            g_ms.busy = false;
            if (!r.ok) {
                g_ms.error = "Couldn't reach Microsoft sign-in. Check your connection.";
                return;
            }
            Json j = Json::parse(r.body);
            g_ms.deviceCode = j.getString("device_code");
            g_ms.userCode = j.getString("user_code");
            g_ms.verifyUri = j.getString("verification_uri");
            g_ms.message = j.getString("message");
            g_ms.active = true;
            g_ms.startedAt = a->Time();
            g_ms.expiresAt = a->Time() + j.getNumber("expires_in", 900);
            a->platform->SetClipboard(g_ms.userCode);
            a->OpenUrl(g_ms.verifyUri.empty() ? "https://microsoft.com/link" : g_ms.verifyUri);
        });
    });
}

void PollDeviceCode(App& app, const std::string& clientId, const std::string& tenant) {
    if (!g_ms.active || g_ms.busy) return;
    if (app.Time() > g_ms.expiresAt) {
        g_ms.error = "The code expired. Try again.";
        g_ms.active = false;
        return;
    }
    g_ms.busy = true;
    App* a = &app;
    string code = g_ms.deviceCode;
    app.workers.emplace_back([a, code, clientId, tenant] {
        string body = "grant_type=urn:ietf:params:oauth:grant-type:device_code&client_id=" + UrlEncode(clientId) +
                      "&device_code=" + UrlEncode(code);
        HttpResult r = HttpRequest("POST", "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/token", body,
                                   {{"Content-Type", "application/x-www-form-urlencoded"}});
        Json j = Json::parse(r.body);
        g_ms.busy = false;
        string error = j.getString("error");
        if (error == "authorization_pending") return;
        if (!r.ok || !error.empty()) {
            a->platform->Post([a, error] {
                g_ms.error = error == "expired_token" ? "The code expired. Try again."
                             : error == "authorization_declined" ? "Sign-in was declined."
                                                                 : "Microsoft sign-in failed: " + error;
                g_ms.active = false;
            });
            return;
        }
        string accessToken = j.getString("access_token");
        string refreshToken = j.getString("refresh_token");
        uint64_t expires = UnixMillis() + (uint64_t)j.getNumber("expires_in", 3600) * 1000;
        a->platform->Post([a, accessToken, refreshToken, expires] {
            g_ms.active = false;
            Account acc;
            acc.name = "Microsoft account";
            acc.type = "microsoft";
            acc.accessToken = accessToken;
            acc.refreshToken = refreshToken;
            acc.expiresAt = expires;
            acc.uuid = Format("%08x", HashStr(accessToken));
            const char* base = "Signed in";
            (void)base;
            a->accounts.push_back(acc);
            a->settings.activeAccount = acc.uuid;
            a->SaveAccounts();
            a->SaveSettings();
            a->ShowToast("Microsoft account added.", false, true);
            if (!a->accounts.empty()) a->LoadSkin(a->accounts.back());
        });
    });
}
}  // namespace

std::unique_ptr<PageBase> MakeAccountsPage() { return std::make_unique<AccountsPage>(); }

void App::DrawAccounts(const Rect& area) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect page(area.x + 28.f, area.y + 28.f, area.w - 56.f, area.h - 56.f);
    float maxW = std::min(980.f, page.w);
    page.x = area.x + (area.w - maxW) * 0.5f;
    page.w = maxW;

    ctx.Text("Accounts", page.x, page.y, 24.f, 700, th.text);
    ctx.Text("Microsoft (online) or offline profiles. The active account is used when the game launches.", page.x,
             page.y + 34.f, 13.5f, 400, th.dim);
    Rect addBtn(page.r() - 190.f, page.y, 88.f, 40.f);
    Rect offBtn(page.r() - 96.f, page.y, 96.f, 40.f);
    if (ctx.Button("addms", addBtn, "Add", true, false, g_ms.active, IconTex("plus"))) StartMsLogin(*this);
    if (ctx.Button("addoff", offBtn, "Offline", false, false, false, IconTex("user"))) {
        offlineName.clear();
        acctModalOpen = true;
        ui.OpenModal("offline", "Add offline account", "A local profile for single-player and cracked servers.");
    }

    float y = page.y + 84.f;
    if (g_ms.active || g_ms.busy || !g_ms.error.empty()) {
        Rect box(page.x, y, page.w, 76.f);
        cv.FillRounded(box, 12.f, th.panel);
        cv.StrokeRounded(box.inset(0.5f), 11.5f, 1.f, g_ms.error.empty() ? th.border : th.danger.with(0.4f));
        if (g_ms.busy && g_ms.userCode.empty()) {
            DrawSpinner(cv, Rect(box.x + 18.f, box.center().y - 10.f, 20.f, 20.f), th.accent, (float)Time());
            ctx.Text("Contacting Microsoft…", box.x + 50.f, box.center().y - 9.f, 13.5f, 500, th.text);
        } else if (g_ms.active) {
            ctx.Text("Enter this code at microsoft.com/link", box.x + 18.f, box.y + 14.f, 13.f, 500, th.text);
            Rect codeBox(box.x + 18.f, box.y + 36.f, 160.f, 30.f);
            cv.FillRounded(codeBox, 8.f, th.panel3);
            ctx.TextCenter(g_ms.userCode, codeBox, 16.f, 700, th.accent);
            ctx.Text("Waiting for sign-in… (code copied to clipboard)", box.x + 194.f, box.y + 42.f, 12.5f, 400, th.dim);
            Rect cancel(box.r() - 108.f, box.y + 22.f, 90.f, 32.f);
            if (ctx.Button("mscancel", cancel, "Cancel", false, true)) {
                g_ms.active = false;
                g_ms.userCode.clear();
            }
            if (Time() - g_ms.startedAt > 1.0 && std::fmod((float)Time(), 5.f) < frameTime * 2.f) {
                PollDeviceCode(*this, "00000000402b5328", "consumers");
            }
        } else if (!g_ms.error.empty()) {
            ctx.Text(g_ms.error, box.x + 18.f, box.y + 24.f, 13.f, 500, th.danger);
            Rect dismiss(box.r() - 108.f, box.y + 22.f, 90.f, 32.f);
            if (ctx.Button("msdismiss", dismiss, "Dismiss", false, true)) g_ms = MsAuth();
        }
        y += 90.f;
    }

    Rect list(page.x, y, page.w, page.b() - y);
    ui.PushClip(list);
    float offset = ui.ctx.ScrollArea("acclist", list, (86.f + 12.f) * (float)accounts.size(), pageScroll[Page::Accounts]);
    float ry = list.y - offset;
    for (size_t i = 0; i < accounts.size(); i++) {
        Account& acc = accounts[i];
        Rect row(list.x, ry, list.w - 14.f, 86.f);
        ry += 98.f;
        if (row.b() < list.y || row.y > list.b()) continue;
        bool active = settings.activeAccount == acc.uuid;
        bool hover = ctx.Hit(row);
        cv.FillRounded(row, 14.f, hover ? th.panel2 : th.panel);
        cv.StrokeRounded(row.inset(0.5f), 13.5f, 1.f, active ? th.accent.with(0.35f) : th.border);
        Rect face(row.x + 16.f, row.y + 16.f, 54.f, 54.f);
        DrawFace(cv, acc.skinTex, face, 12.f);
        if (acc.skinTex < 0 && !acc.name.empty()) LoadSkin(acc);
        ctx.Text(acc.name, face.r() + 16.f, row.y + 18.f, 16.f, 600, th.text);
        string type = acc.type == "microsoft" ? "Microsoft" : acc.type == "ely" ? "Ely.by" : "Offline";
        ctx.Text(type, face.r() + 16.f, row.y + 42.f, 12.5f, 500, acc.type == "offline" ? th.dim : th.accent);
        ctx.Text(FormatDurationShort(acc.playtime) + " played", face.r() + 16.f + ctx.TextW(type, 12.5f, 500) + 14.f,
                 row.y + 42.f, 12.5f, 400, th.faint);
        if (active) {
            Rect tag(row.r() - 180.f, row.y + 18.f, 68.f, 24.f);
            cv.FillRounded(tag, 12.f, th.accentSoft);
            ctx.TextCenter("Active", tag, 11.5f, 600, th.accent);
        }
        Rect use(row.r() - 104.f, row.y + 18.f, 88.f, 30.f);
        if (!active && ctx.Button("useacc" + std::to_string(i), use, "Use", false, true)) {
            settings.activeAccount = acc.uuid;
            SaveSettings();
            ShowToast("Switched to " + acc.name, false, true);
        }
        Rect del(row.r() - 104.f, row.y + 52.f - 22.f, 88.f, 28.f);
        if (ctx.Button("delacc" + std::to_string(i), del, "Remove", false, true)) {
            accounts.erase(accounts.begin() + (long)i);
            if (settings.activeAccount == acc.uuid) settings.activeAccount = accounts.empty() ? "" : accounts[0].uuid;
            SaveAccounts();
            SaveSettings();
            break;
        }
    }
    if (accounts.empty()) {
        cv.StrokeRounded(list.inset(0.5f), 13.5f, 1.f, th.border);
        int icon = IconTex("user");
        cv.Image(icon, Rect(list.center().x - 15.f, list.center().y - 44.f, 30.f, 30.f), th.faint);
        ctx.TextCenter("No accounts yet. Add a Microsoft or offline account.", Rect(list.x, list.center().y, list.w, 20.f),
                       13.5f, 400, th.dim);
    }
    ui.PopClip();
}

// offline account modal
void App::DrawAccountModal() {
    if (!ui.modal.open || ui.modal.id != "offline") return;
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect panel = ui.ModalPanel(440.f, 290.f);
    ui.DrawModalChrome(panel);
    ctx.Text(ui.modal.title, panel.x + 24.f, panel.y + 22.f, 18.f, 680, th.text);
    ctx.Text(ui.modal.subtitle, panel.x + 24.f, panel.y + 48.f, 13.f, 400, th.dim, panel.w - 48.f);
    ctx.Text("Username", panel.x + 24.f, panel.y + 92.f, 12.5f, 500, th.dim);
    Rect field(panel.x + 24.f, panel.y + 112.f, panel.w - 48.f, 42.f);
    ctx.TextField("offname", field, offlineName, "Steve");
    Rect cancel(panel.r() - 24.f - 104.f - 12.f - 120.f, panel.b() - 66.f, 120.f, 40.f);
    Rect add(panel.r() - 24.f - 120.f, panel.b() - 66.f, 120.f, 40.f);
    if (ctx.Button("offcancel", cancel, "Cancel")) ui.CloseModal();
    if (ctx.Button("offadd", add, "Add account", true, false, Trim(offlineName).empty(), IconTex("plus"))) {
        Account acc;
        acc.name = Trim(offlineName);
        acc.type = "offline";
        uint32_t h = HashStr(acc.name);
        acc.uuid = Format("%08x%08x%08x%08x", h, h * 2654435761u, h ^ 0x9e3779b9u, h * 40503u);
        accounts.push_back(acc);
        settings.activeAccount = acc.uuid;
        SaveAccounts();
        SaveSettings();
        LoadSkin(accounts.back());
        ui.CloseModal();
        ShowToast("Added " + acc.name, false, true);
    }
    (void)cv;
}

}  // namespace crit
