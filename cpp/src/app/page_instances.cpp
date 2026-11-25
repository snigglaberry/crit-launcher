// Crit Launcher Native — Instances page + create-instance modal.
#include "app/pages.h"

namespace crit {

namespace {
struct InstancesPage : PageBase {
    void Draw(App& app) override { app.DrawInstances(app.contentRect); }
};
}  // namespace

std::unique_ptr<PageBase> MakeInstancesPage() { return std::make_unique<InstancesPage>(); }

void App::DrawInstances(const Rect& area) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect page(area.x + 28.f, area.y + 28.f, area.w - 56.f, area.h - 56.f);
    float maxW = std::min(980.f, page.w);
    page.x = area.x + (area.w - maxW) * 0.5f;
    page.w = maxW;

    // header
    if (getenv("CRIT_DEBUG_PAGE")) {
        fprintf(stderr, "[page] area=%.1f,%.1f %.1fx%.1f page=%.1f,%.1f %.1fx%.1f clip=%.1f,%.1f %.1fx%.1f batches=%zu\n",
                area.x, area.y, area.w, area.h, page.x, page.y, page.w, page.h,
                ui.ctx.Clip().x, ui.ctx.Clip().y, ui.ctx.Clip().w, ui.ctx.Clip().h, render.canvas.batches.size());
    }
    ctx.Text("Instances", page.x, page.y, 24.f, 700, th.text);
    ctx.Text("Minecraft versions, mod loaders and worlds — one card per install.", page.x, page.y + 34.f, 13.5f, 400,
             th.dim);
    Rect newBtn(page.r() - 158.f, page.y, 158.f, 40.f);
    if (ctx.Button("newinst", newBtn, "New instance", true, false, false, IconTex("plus"))) {
        newInstanceName = "My Instance";
        newInstanceError.clear();
        newVersionIndex = 0;
        newLoaderIndex = 0;
        newLoaderVersionIndex = 0;
        if (!versionsLoaded && !versionsLoading) LoadVersionLists();
        ui.OpenModal("newinst", "Create instance", "Pick a version and a mod loader.");
    }

    float listY = page.y + 74.f;
    Rect list(page.x, listY, page.w, page.b() - listY);
    if (instances.empty()) {
        cv.FillRounded(list.inset(0.f), 14.f, Color(0, 0, 0, 0));
        cv.StrokeRounded(list.inset(0.5f), 13.5f, 1.f, th.border);
        int icon = IconTex("box");
        cv.Image(icon, Rect(list.center().x - 15.f, list.center().y - 44.f, 30.f, 30.f), th.faint);
        ctx.TextCenter("No instances yet. Create one to get started.", Rect(list.x, list.center().y, list.w, 20.f), 13.5f,
                       400, th.dim);
        Rect create(list.center().x - 70.f, list.center().y + 30.f, 140.f, 38.f);
        if (ctx.Button("newinst2", create, "Create instance", true, false, false, IconTex("plus"))) {
            newInstanceName = "My Instance";
            if (!versionsLoaded && !versionsLoading) LoadVersionLists();
            ui.OpenModal("newinst", "Create instance", "Pick a version and a mod loader.");
        }
        return;
    }
    ui.PushClip(list);
    float rowH = 74.f;
    float offset = ui.ctx.ScrollArea("instlist", list, (rowH + 10.f) * (float)instances.size(), pageScroll[Page::Instances]);
    float y = list.y - offset;
    for (size_t i = 0; i < instances.size(); i++) {
        Instance& inst = instances[i];
        Rect row(list.x, y, list.w - 14.f, rowH);
        y += rowH + 10.f;
        if (row.b() < list.y || row.y > list.b()) continue;
        bool active = settings.activeInstance == inst.name;
        bool hover = ctx.Hit(row);
        cv.FillRounded(row, 14.f, hover ? Color::hex(0x14171d) : th.panel);
        cv.StrokeRounded(row.inset(0.5f), 13.5f, 1.f,
                         active ? th.accent.with(0.35f) : (hover ? Color::hex(0x313744) : th.border));
        // icon tile
        Rect tile(row.x + 16.f, row.y + 16.f, 42.f, 42.f);
        cv.FillRounded(tile, 11.f, active ? th.accentSoft : th.panel3);
        int icon = IconTex("box");
        cv.Image(icon, Rect(tile.center().x - 11.f, tile.center().y - 11.f, 22.f, 22.f), active ? th.accent : th.dim);
        // name + meta
        float tx = tile.r() + 14.f;
        ctx.Text(inst.name, tx, row.y + 15.f, 15.f, 600, th.text, row.w - 420.f);
        string status = IsInstanceInstalling(inst.name) ? "Installing" : IsInstanceRunning(inst.name) ? "Running"
                        : inst.installed                            ? "Ready"
                                                                    : "Not installed";
        float metaY = row.y + 38.f;
        float px = tx;
        Color dotCol = IsInstanceRunning(inst.name) ? th.accent : IsInstanceInstalling(inst.name) ? th.warn : th.faint;
        cv.FillCircle({px + 3.f, metaY + 7.f}, 3.f, dotCol);
        px += 12.f;
        ctx.Text(status, px, metaY, 12.5f, 400, th.dim);
        px += ctx.TextW(status, 12.5f, 400) + 12.f;
        string verTxt = (inst.loader.empty() || inst.loader == "vanilla")
                            ? inst.version
                            : (inst.loader == "fabric" ? "Fabric " : inst.loader == "forge" ? "Forge " : "NeoForge ") +
                                  inst.version;
        ctx.Text(verTxt, px, metaY, 12.5f, 400, th.dim);
        if (inst.playtime >= 60) {
            px += ctx.TextW(verTxt, 12.5f, 400) + 12.f;
            ctx.Text(FormatDurationShort(inst.playtime) + " played", px, metaY, 12.5f, 400, th.faint);
        }
        // buttons
        Rect menuBtn(row.r() - 44.f, row.y + 20.f, 32.f, 32.f);
        if (ctx.IconButton("instmenu" + std::to_string(i), menuBtn, IconTex("more"), 16.f, "More")) {
            std::vector<string> items = {"Open folder", "Set as active", "Duplicate", "Delete"};
            int sel = ui.anim.Get("instmenu" + std::to_string(i)) > 0 ? 0 : 0;
            (void)sel;
            pendingFields["instmenu"] = inst.name;
            ui.OpenModal("instmenu", inst.name, "Instance actions");
        }
        bool running = IsInstanceRunning(inst.name);
        bool installing = IsInstanceInstalling(inst.name);
        Rect primary(row.r() - 44.f - 8.f - 122.f, row.y + 20.f, 122.f, 32.f);
        string label = running ? "Stop" : inst.installed ? "Launch" : "Install";
        int picon = running ? IconTex("square") : inst.installed ? IconTex("play") : IconTex("download");
        if (ctx.Button("instplay" + std::to_string(i), primary, label, true, true, installing, picon)) {
            SetActiveInstance(inst.name);
            Launch(inst);
        }
    }
    ui.PopClip();
}

// ---------------------------------------------------------------- create modal
void App::DrawCreateInstanceModal() {
    if (!ui.modal.open || ui.modal.id != "newinst") return;
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect panel = ui.ModalPanel(560.f, 520.f);
    ui.DrawModalChrome(panel);

    // close button
    Rect close(panel.r() - 44.f, panel.y + 16.f, 28.f, 28.f);
    if (ctx.IconButton("newclose", close, IconTex("x"), 16.f)) ui.CloseModal();

    ctx.Text(ui.modal.title, panel.x + 24.f, panel.y + 22.f, 18.f, 680, th.text);
    ctx.Text(ui.modal.subtitle, panel.x + 24.f, panel.y + 48.f, 13.f, 400, th.dim);

    float x = panel.x + 24.f;
    float w = panel.w - 48.f;
    float y = panel.y + 84.f;
    ctx.Text("Instance name", x, y, 12.5f, 500, th.dim);
    Rect nameField(x, y + 20.f, w, 42.f);
    ctx.TextField("newname", nameField, newInstanceName, "My Instance");

    y += 80.f;
    ctx.Text("Minecraft version", x, y, 12.5f, 500, th.dim);
    std::vector<string> versions = versionList;
    if (versions.empty()) versions.push_back(versionsLoading ? "Loading…" : "No versions");
    int vi = std::clamp(newVersionIndex, 0, (int)versions.size() - 1);
    if (ui.Select("newver", Rect(x, y + 20.f, w * 0.48f - 6.f, 42.f), versions, vi)) {
        newVersionIndex = vi;
        if (!versionsLoaded) LoadVersionLists();
    }

    ctx.Text("Mod loader", x + w * 0.52f, y, 12.5f, 500, th.dim);
    std::vector<string> loaders = {"Vanilla", "Fabric", "Forge", "NeoForge"};
    int li = std::clamp(newLoaderIndex, 0, 3);
    if (ui.Select("newloader", Rect(x + w * 0.52f, y + 20.f, w * 0.48f, 42.f), loaders, li)) newLoaderIndex = li;

    y += 80.f;
    // loader version picker (Fabric needs an explicit loader build)
    float pipY = y;
    if (newLoaderIndex == 1) {
        ctx.Text("Fabric loader version", x, y, 12.5f, 500, th.dim);
        std::vector<string> lv = fabricLoaders;
        if (lv.empty()) {
            lv.push_back("latest");
        }
        int lvi = std::clamp(newLoaderVersionIndex, 0, (int)lv.size() - 1);
        if (ui.Select("newloaderver", Rect(x, y + 20.f, w, 42.f), lv, lvi)) newLoaderVersionIndex = lvi;
    } else if (newLoaderIndex == 2 || newLoaderIndex == 3) {
        ctx.Text(newLoaderIndex == 2 ? "Forge version" : "NeoForge version", x, y, 12.5f, 500, th.dim);
        std::vector<string> lv = newLoaderIndex == 2 ? forgeVersions : neoforgeVersions;
        if (lv.empty()) lv.push_back("latest");
        int lvi = std::clamp(newLoaderVersionIndex, 0, (int)lv.size() - 1);
        if (ui.Select("newloaderver2", Rect(x, y + 20.f, w, 42.f), lv, lvi)) newLoaderVersionIndex = lvi;
    }
    (void)pipY;

    if (!newInstanceError.empty()) {
        ctx.Text(newInstanceError, x, panel.b() - 92.f, 12.5f, 500, th.danger);
    }
    Rect cancel(panel.r() - 24.f - 104.f - 12.f - 120.f, panel.b() - 66.f, 120.f, 40.f);
    Rect create(panel.r() - 24.f - 120.f, panel.b() - 66.f, 120.f, 40.f);
    if (ctx.Button("newcancel", cancel, "Cancel")) ui.CloseModal();
    bool canCreate = !versionList.empty();
    if (ctx.Button("newcreate", create, "Create", true, false, !canCreate, IconTex("plus"))) {
        string ver = versionList.empty() ? "" : versionList[(size_t)std::clamp(newVersionIndex, 0, (int)versionList.size() - 1)];
        string loaderName = newLoaderIndex == 0 ? "vanilla" : newLoaderIndex == 1 ? "fabric" : newLoaderIndex == 2 ? "forge" : "neoforge";
        string lv;
        if (newLoaderIndex == 1 && !fabricLoaders.empty())
            lv = fabricLoaders[(size_t)std::clamp(newLoaderVersionIndex, 0, (int)fabricLoaders.size() - 1)];
        else if (newLoaderIndex == 2 && !forgeVersions.empty())
            lv = forgeVersions[(size_t)std::clamp(newLoaderVersionIndex, 0, (int)forgeVersions.size() - 1)];
        else if (newLoaderIndex == 3 && !neoforgeVersions.empty())
            lv = neoforgeVersions[(size_t)std::clamp(newLoaderVersionIndex, 0, (int)neoforgeVersions.size() - 1)];
        if (loaderName == "fabric" && lv.empty()) {
            // resolve "latest" from the fabric meta API on demand
            HttpResult r = HttpGet("https://meta.fabricmc.net/v2/versions/loader");
            if (r.ok) {
                Json j = Json::parse(r.body);
                if (j.isArray() && j.size() > 0) lv = j[0].getString("version");
            }
        }
        if (loaderName != "vanilla" && lv.empty()) lv = "latest";
        CreateInstance(newInstanceName, ver, loaderName, lv);
    }
}

}  // namespace crit
