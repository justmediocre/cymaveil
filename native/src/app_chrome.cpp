// Sidebar, title bar, mini player, queue panel, popup menus and toasts —
// ports of Sidebar.tsx, layout/TitleBar.tsx, MiniPlayer.tsx,
// layout/QueuePanel.tsx and AddToPlaylistMenu.tsx.
#include "app.h"

#include <algorithm>
#include <cmath>

#include "raymath.h"
#include "rlgl.h"

#include "ui.h"

namespace {

constexpr float kSidebarW = 260.0f;
constexpr float kQueueW = 320.0f;

}  // namespace

void App::DrawSidebar(Rectangle r) {
    // Content is laid out at the full width and clipped by the sliding edge.
    const float revealed = r.width;
    const float alpha = Clamp(revealed / kSidebarW, 0.0f, 1.0f);
    const Rectangle full{r.x + revealed - kSidebarW, r.y, kSidebarW, r.height};
    backdrop_.DrawGlass(r, ui::theme.glassSurface, config_.glassBlur);
    DrawLineEx(Vector2{r.x + r.width, r.y}, Vector2{r.x + r.width, r.y + r.height}, 1,
               ui::theme.borderSubtle);
    ui::BeginClip(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(std::ceil(r.width)),
                  static_cast<int>(r.height));

    // ── Brand: font-display 28px black, 0.35em tracking, uppercase, scaleX 1.15,
    // letterpress emboss (fill = surface, highlight below, shadow above) ──
    {
        const std::string brand = "CYMAVEIL";
        const float size = 28.0f, tracking = 0.35f * size;
        // raylib's measure already excludes the trailing letter-space; CSS
        // includes it in the box, which is what pulls the glyphs back left
        // against the asymmetric pl-6 pr-4 padding. Emulate that box.
        const float w = ui::Measure(brand, size, ui::Face::DisplayBlack, tracking).x + tracking;
        // pt-6 pb-3 pl-6 pr-4, text centred in the remaining width
        const float cx = full.x + 24 + (kSidebarW - 24 - 16) / 2;
        const float top = full.y + 24;
        rlPushMatrix();
        rlTranslatef(cx, 0, 0);
        rlScalef(1.15f, 1.0f, 1.0f);
        rlTranslatef(-cx, 0, 0);
        const Vector2 pos{cx - w / 2, top};
        // Fill sits a touch above the surface (the CSS 0.3px text-stroke) and
        // the emboss is a little stronger than the web's so it reads on glass.
        const Color hi = ui::theme.light ? Fade(WHITE, 0.9f * alpha) : Fade(WHITE, 0.14f * alpha);
        const Color lo = ui::theme.light ? Fade(BLACK, 0.10f * alpha) : Fade(BLACK, 0.75f * alpha);
        const Color fill = ui::theme.light ? ui::Mix(ui::theme.surface, BLACK, 0.03f)
                                           : ui::Mix(ui::theme.surface, WHITE, 0.05f);
        ui::Text(brand, Vector2{pos.x, pos.y + 1}, size, hi, ui::Face::DisplayBlack, tracking);
        ui::Text(brand, Vector2{pos.x, pos.y - 1}, size, lo, ui::Face::DisplayBlack, tracking);
        ui::Text(brand, pos, size, Fade(fill, alpha), ui::Face::DisplayBlack, tracking);
        rlPopMatrix();
    }

    // ── Nav: px-4, gap 20, padding-top 20; items pl-10 pr-4 py-3.5 rounded-xl ──
    struct NavItem {
        const char* label;
        View view;
        void (*icon)(Vector2, float, Color);
    };
    const NavItem items[] = {
        {"Now Playing", View::NowPlaying, [](Vector2 c, float s, Color col) { ui::IconPlay(c, s, col); }},
        {"Search", View::Search, [](Vector2 c, float s, Color col) { ui::IconSearch(c, s, col); }},
        {"Library", View::Library, [](Vector2 c, float s, Color col) { ui::IconMusicNote(c, s, col); }},
        {"Albums", View::Albums, [](Vector2 c, float s, Color col) { ui::IconDisc(c, s, col); }},
        {"Favorites", View::Favorites, [](Vector2 c, float s, Color col) { ui::IconHeart(c, s, col, false); }},
        {"Playlists", View::Playlists, [](Vector2 c, float s, Color col) { ui::IconList(c, s, col); }},
        {"Settings", View::Settings, [](Vector2 c, float s, Color col) { ui::IconSettings(c, s, col); }},
    };
    View effective = view_;
    if (view_ == View::AlbumDetail) effective = View::Albums;
    if (view_ == View::PlaylistDetail) effective = View::Playlists;
    const float itemH = 14 + 24 + 14;  // py-3.5 around a 24px line
    float y = full.y + 24 + 28 + 12 + 20;
    float activeY = -1;
    for (const auto& item : items) {
        const Rectangle row{full.x + 16, y, kSidebarW - 32, itemH};
        const bool active = effective == item.view;
        const bool hov = ui::Hover(row);
        const float ht = ui::Ease(std::string("nav#") + item.label, hov || active ? 1.0f : 0.0f, 0.15f);
        const float press = ui::Spring(std::string("nav#p") + item.label,
                                       hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 0.98f : 1.0f, 1.0f);
        const Color col = Fade(ui::Mix(ui::theme.textSecondary, ui::theme.text, ht), alpha);
        rlPushMatrix();
        rlTranslatef(row.x + row.width / 2, row.y + row.height / 2, 0);
        rlScalef(press, press, 1);
        rlTranslatef(-(row.x + row.width / 2), -(row.y + row.height / 2), 0);
        item.icon(Vector2{row.x + 40 + 10, row.y + itemH / 2}, 20, col);
        ui::TextV(item.label, row.x + 40 + 20 + 16, row.y + itemH / 2, 16, col);
        rlPopMatrix();
        if (active) activeY = row.y + itemH / 2;
        if (ui::Clicked(row)) Navigate(item.view);
        y += itemH + 20;
    }
    // Active indicator: 2x16 rounded bar at the item's left edge, slides
    // between items (layoutId) over 0.3s.
    if (activeY >= 0) {
        const float iy = ui::Ease("nav#indicator", activeY, 0.3f, activeY);
        ui::RoundedRect(Rectangle{full.x + 16, iy - 8, 2, 16}, 1, Fade(ui::theme.textSecondary, alpha));
    }

    // ── Up Next: bottom section, border-top, px-4 py-4 ──
    {
        // Rows: 32px art + 2 lines; up to two entries.
        std::vector<const Track*> next;
        if (player_.QueueSize() > 0 && player_.Repeat() != RepeatMode::One) {
            for (int i = 1; i <= 2; i++) {
                const int pos = player_.OrderPos() + i;
                if (pos < player_.QueueSize()) {
                    if (const Track* t = player_.TrackAtOrderPos(pos)) next.push_back(t);
                } else if (player_.Repeat() == RepeatMode::All && player_.QueueSize() > 0) {
                    if (const Track* t = player_.TrackAtOrderPos(pos % player_.QueueSize())) next.push_back(t);
                }
            }
        }
        const float rowH = 32 + 12;  // py-1.5 around 32px art
        const float listH = next.empty() ? 20.0f : next.size() * rowH + (next.size() - 1) * 8;
        const float sectionH = 16 + 14 + 12 + listH + 16;
        const float top = full.y + full.height - sectionH;
        DrawLineEx(Vector2{full.x, top}, Vector2{full.x + kSidebarW, top}, 1, ui::theme.borderSubtle);
        ui::Text("UP NEXT", Vector2{full.x + 16, top + 16}, 11, Fade(ui::theme.textSecondary, alpha),
                 ui::Face::SansMedium, 0.55f);
        float ry = top + 16 + 14 + 12;
        if (next.empty()) {
            ui::Text("Nothing queued", Vector2{full.x + 24, ry + 2}, 11, Fade(ui::theme.textTertiary, alpha));
        }
        for (size_t i = 0; i < next.size(); i++) {
            const Track* t = next[i];
            const Rectangle row{full.x + 16, ry, kSidebarW - 32, rowH};
            const float hov = ui::Ease("upnext#" + t->id, ui::Hover(row) ? 1.0f : 0.0f, 0.15f, 0.0f);
            if (hov > 0.01f) ui::RoundedRect(row, 6, Fade(ui::theme.hover, hov * alpha));
            const Album* album = library_.AlbumById(t->albumId);
            DrawAlbumArt(Rectangle{row.x + 8, row.y + 6, 32, 32}, ResolveArt(t, album), 4);
            const float tx = row.x + 8 + 32 + 10;
            ui::TextEllipsis(t->title, Vector2{tx, row.y + 7}, row.width - 58, 12, Fade(ui::theme.text, alpha),
                             ui::Face::SansMedium);
            ui::TextEllipsis(t->artist, Vector2{tx, row.y + 23}, row.width - 58, 10,
                             Fade(ui::theme.textSecondary, alpha));
            if (ui::Clicked(row)) {
                player_.JumpTo(player_.OrderPos() + static_cast<int>(i) + 1);
                manualSkip_ = true;
            }
            ry += rowH + 8;
        }
    }
    ui::EndClip();

    // Scan status floats above the Up Next section while a scan runs.
    if (library_.ScanActive()) {
        const ScanStatus st = library_.Status();
        const float sy = full.y + full.height - 150;
        const std::string label = st.total > 0 ? TextFormat("Scanning %d / %d", st.current, st.total)
                                               : "Scanning...";
        ui::Text(label, Vector2{full.x + 24, sy}, 11, Fade(ui::theme.textTertiary, alpha));
        const Rectangle track{full.x + 24, sy + 18, kSidebarW - 48, 3};
        ui::RoundedRect(track, 2, ui::theme.borderSubtle);
        if (st.total > 0) {
            const float frac = static_cast<float>(st.current) / static_cast<float>(st.total);
            ui::RoundedRect(Rectangle{track.x, track.y, track.width * frac, track.height}, 2, ui::theme.accent);
        }
    }
}

void App::DrawTitleBar(Rectangle r) {
    // px-4 pt-3 pb-2: 32px buttons, rounded-lg, tertiary -> primary on hover.
    const float by = r.y + 12;
    const auto chromeButton = [&](const std::string& key, float x, Color col,
                                  const std::function<void(Vector2, Color)>& draw) {
        const Rectangle b{x, by, 32, 32};
        return ui::IconButton(key, b, col, ui::theme.text, 1.05f, 0.95f, draw);
    };
    // Left: sidebar toggle (plus 64px traffic-light spacer when closed — a
    // macOS-only affordance; we keep the button flush).
    float x = r.x + 16;
    if (chromeButton("tb.sidebar", x, ui::theme.textTertiary,
                     [](Vector2 c, Color col) { ui::IconSidebar(c, 17, col); })) {
        ToggleSidebar();
    }
    // Depth-mask batch indicator: spinner + "Processing art".
    if (depth_.Busy() && config_.depthLayers) {
        const float ix = x + 32 + 8 + 4;
        ui::IconSpinner(Vector2{ix + 7, by + 16}, 14, ui::theme.textTertiary);
        ui::TextV("Processing art", ix + 14 + 6, by + 16, 12,
                  ui::theme.textTertiary);
    }

    // Right: queue, fullscreen, theme (gap-1)
    float rx = r.x + r.width - 16 - 32;
    if (chromeButton("tb.theme", rx, ui::theme.textSecondary, [](Vector2 c, Color col) {
            if (ui::theme.light) ui::IconSun(c, 16, col);
            else ui::IconMoon(c, 16, col);
        })) {
        config_.theme = ui::theme.light ? "dark" : "light";
        ApplyTheme();
        config_.Save();
    }
    rx -= 36;
    if (chromeButton("tb.fullscreen", rx, ui::theme.textTertiary, [this](Vector2 c, Color col) {
            if (fullscreen_) ui::IconShrink(c, 15, col);
            else ui::IconExpand(c, 15, col);
        })) {
        // Entering fullscreen from the toolbar lands on the immersive Now
        // Playing view; F11 stays a plain toggle for whatever is on screen.
        if (!fullscreen_ && player_.Current() != nullptr && view_ != View::NowPlaying) {
            Navigate(View::NowPlaying);
        }
        ToggleFullscreenMode();
    }
    rx -= 36;
    if (chromeButton("tb.queue", rx, config_.queuePanel ? ui::theme.accent : ui::theme.textTertiary,
                     [](Vector2 c, Color col) { ui::IconList(c, 17, col); })) {
        ToggleQueuePanel();
    }
}

void App::DrawMiniPlayer(Rectangle r) {
    const Track* cur = player_.Current();
    if (cur == nullptr) return;
    const Album* album = library_.AlbumById(cur->albumId);

    backdrop_.DrawGlass(r, ui::theme.glassSurface, config_.glassBlur);
    DrawLineEx(Vector2{r.x, r.y}, Vector2{r.x + r.width, r.y}, 1, ui::theme.borderSubtle);

    const float cy = r.y + r.height / 2;
    const Rectangle playR{r.x + r.width - 16 - 40 - 4 - 40, cy - 20, 40, 40};
    const Rectangle nextR{r.x + r.width - 16 - 40, cy - 20, 40, 40};
    const Rectangle progressHit{r.x, r.y - 8, r.width, 20};

    // Scrubbable hairline progress on the top edge (2px, 4px hovered)
    const float length = player_.TimeLength();
    if (!seekDragging_) seekValue_ = length > 0 ? player_.TimePlayed() / length : 0;
    const bool wasDragging = seekDragging_;
    ui::Slider(Rectangle{r.x, r.y - 2, r.width, 4}, &seekValue_, &seekDragging_);
    if (wasDragging && !seekDragging_) player_.SeekTo(seekValue_ * length);
    const float lineH = ui::Ease("mini.prog", (ui::Hover(progressHit) || seekDragging_) ? 4.0f : 2.0f, 0.15f);
    DrawRectangleRec(Rectangle{r.x, r.y, r.width, lineH}, ui::theme.borderSubtle);
    DrawRectangleRec(Rectangle{r.x, r.y, r.width * Clamp(seekValue_, 0.0f, 1.0f), lineH}, ui::theme.accent);

    // Art 48px rounded-lg + track info (px-4 gap-3)
    DrawAlbumArt(Rectangle{r.x + 16, cy - 24, 48, 48}, ResolveArt(cur, album), 8);
    const float infoX = r.x + 16 + 48 + 12;
    const float infoW = playR.x - infoX - 12;
    ui::TextEllipsis(cur->title, Vector2{infoX, cy - 17}, infoW, 14, ui::theme.text, ui::Face::SansMedium);
    ui::TextEllipsis(cur->artist, Vector2{infoX, cy + 2}, infoW, 12, ui::theme.textTertiary);

    // Play/pause + next: 40px round buttons, hover scale 1.1 + bg-hover
    const auto roundButton = [&](const std::string& key, Rectangle b, Color col,
                                 const std::function<void(Vector2, Color)>& draw) {
        const bool hov = ui::Hover(b);
        const float ha = ui::Ease(key + "#bg", hov ? 1.0f : 0.0f, 0.15f, 0.0f);
        if (ha > 0.01f) DrawCircleV(Vector2{b.x + 20, b.y + 20}, 20, Fade(ui::theme.hover, ha));
        return ui::IconButton(key, b, col, col, 1.1f, 0.9f, draw);
    };
    if (roundButton("mini.play", playR, ui::theme.text, [this](Vector2 c, Color col) {
            if (player_.IsPlaying()) ui::IconPause(c, 20, col);
            else ui::IconPlay(c, 20, col);
        })) {
        player_.TogglePause();
    }
    if (roundButton("mini.next", nextR, ui::theme.textSecondary,
                    [](Vector2 c, Color col) { ui::IconSkipForward(c, 18, col); })) {
        player_.Next();
        manualSkip_ = true;
    }

    // Anywhere else on the bar expands into Now Playing
    if (ui::Clicked(r) && !ui::Hover(playR) && !ui::Hover(nextR) && !ui::Hover(progressHit)) {
        Navigate(View::NowPlaying);
    }
}

void App::DrawQueuePanel(Rectangle r) {
    const float alpha = Clamp(r.width / kQueueW, 0.0f, 1.0f);
    backdrop_.DrawGlass(r, ui::theme.glassSurface, config_.glassBlur);
    DrawLineEx(Vector2{r.x, r.y}, Vector2{r.x, r.y + r.height}, 1, ui::theme.borderSubtle);
    const Rectangle full{r.x, r.y, kQueueW, r.height};
    (void)alpha;
    ui::BeginClip(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(std::ceil(r.width)),
                  static_cast<int>(r.height));

    enum class Mode { Queue, NowPlayingList, Empty };
    Mode mode = Mode::Empty;
    std::vector<const Track*> rows;
    std::string header = "QUEUE";
    const bool nowPlayingSource = player_.Source() == QueueSource::NowPlaying;
    const bool hasNowPlayingTracks = !playlists_.NowPlaying().trackIds.empty();
    if (nowPlayingSource || (player_.QueueSize() == 0 && hasNowPlayingTracks)) {
        mode = Mode::NowPlayingList;
        rows = ResolveTracks(playlists_.NowPlaying().trackIds);
        header = TextFormat("NOW PLAYING · %d TRACKS", static_cast<int>(rows.size()));
    } else if (player_.QueueSize() > 0) {
        mode = Mode::Queue;
        rows.reserve(player_.QueueSize());
        for (int i = 0; i < player_.QueueSize(); i++) rows.push_back(player_.TrackAtOrderPos(i));
        if (player_.Shuffle()) {
            header = TextFormat("SHUFFLE QUEUE · %d TRACKS", player_.QueueSize());
        } else {
            std::string title;
            switch (player_.Source()) {
                case QueueSource::Playlist: {
                    const Playlist* p = playlists_.ById(player_.SourceId());
                    title = p != nullptr ? p->name : "Playlist";
                    break;
                }
                case QueueSource::Album: {
                    const Album* a = library_.AlbumById(player_.SourceId());
                    title = a != nullptr ? a->title : "Album";
                    break;
                }
                case QueueSource::Library: title = "Library"; break;
                default: {
                    const Track* cur = player_.Current();
                    const Album* a = cur != nullptr ? library_.AlbumById(cur->albumId) : nullptr;
                    title = a != nullptr ? a->title : "";
                }
            }
            for (auto& ch : title) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            header = "NOW PLAYING · " + title;
        }
    }

    // Header (p-4, mb-4): font-display 12px bold tracking-wider uppercase
    const float hy = full.y + 16;
    float rightX = full.x + kQueueW - 16;
    if (!rows.empty()) {
        const std::string clear = "CLEAR";
        const float cw = ui::Measure(clear, 10, ui::Face::SansMedium, 0.5f).x + 12;
        const Rectangle cR{rightX - cw, hy - 2, cw, 20};
        const bool hov = ui::Hover(cR);
        ui::TextV(clear, cR.x + 6, cR.y + 10, 10, hov ? ui::theme.text : ui::theme.textTertiary,
                  ui::Face::SansMedium);
        if (ui::Clicked(cR)) {
            if (nowPlayingSource || mode == Mode::NowPlayingList) {
                playlists_.NowPlaying().trackIds.clear();
                playlists_.Save();
            }
            player_.ClearQueue();
            ui::EndClip();
            return;
        }
        rightX = cR.x - 4;
    }
    if (hasNowPlayingTracks && !nowPlayingSource) {
        const Rectangle pR{rightX - 24, hy - 4, 24, 24};
        if (ui::IconButton("queue.play", pR, ui::theme.accent, ui::theme.accent, 1.1f, 0.9f,
                           [](Vector2 c, Color col) { ui::IconPlay(c, 14, col); })) {
            const auto np = ResolveTracks(playlists_.NowPlaying().trackIds);
            if (!np.empty()) PlayFromTrackList(np, 0, QueueSource::NowPlaying, Playlists::kNowPlayingId);
        }
        rightX = pR.x - 4;
    }
    ui::TextEllipsis(header, Vector2{full.x + 16, hy + 1}, rightX - full.x - 24, 12, ui::theme.textTertiary,
                     ui::Face::Display);

    const Rectangle list{full.x + 16, hy + 16 + 16, kQueueW - 32, full.y + full.height - (hy + 32) - 16};
    if (rows.empty()) {
        ui::TextCentered("Queue is empty", Vector2{full.x + kQueueW / 2, list.y + 60}, 14,
                         ui::theme.textTertiary);
        ui::EndClip();
        return;
    }
    const TableResult res = DrawTrackList(list, rows, &queueScroll_, mode == Mode::NowPlayingList, true);
    if (res.clicked >= 0) {
        if (mode == Mode::Queue) {
            player_.JumpTo(res.clicked);
            manualSkip_ = true;
            MarkActivity();
        } else {
            PlayFromTrackList(rows, res.clicked, QueueSource::NowPlaying, Playlists::kNowPlayingId);
        }
    }
    if (res.removed >= 0 && rows[res.removed] != nullptr) {
        const std::string tid = rows[res.removed]->id;
        playlists_.RemoveTrack(Playlists::kNowPlayingId, tid);
        if (nowPlayingSource) player_.RemoveTrackId(tid);
        MarkActivity();
    }
    ui::EndClip();
}

// ── Popup menus ──

void App::OpenMenu(Rectangle anchor, std::vector<MenuItem> items, const std::string& header,
                   bool preferAbove) {
    menu_.open = true;
    menu_.justOpened = true;
    menu_.anchor = anchor;
    menu_.items = std::move(items);
    menu_.header = header;
    menu_.preferAbove = preferAbove;
    menu_.newInput = false;
    menu_.newText.clear();
    MarkActivity();
}

void App::OpenTrackMenu(const std::string& trackId, Rectangle anchor, bool preferAbove) {
    const Track* t = library_.TrackById(trackId);
    if (t == nullptr) return;
    std::vector<MenuItem> items;
    const bool inNp = playlists_.Contains(Playlists::kNowPlayingId, t->id);
    for (const auto& p : playlists_.All()) {
        if (p.id == Playlists::kFavoritesId) continue;
        const bool has = playlists_.Contains(p.id, t->id);
        const std::string pid = p.id;
        items.push_back({p.name,
                         [this, pid, trackId, has, inNp] {
                             if (pid == Playlists::kNowPlayingId) {
                                 if (inNp) {
                                     playlists_.RemoveTrack(pid, trackId);
                                     if (player_.Source() == QueueSource::NowPlaying) player_.RemoveTrackId(trackId);
                                 } else {
                                     playlists_.AddTrack(pid, trackId);
                                     if (player_.Source() == QueueSource::NowPlaying) player_.Append(trackId);
                                 }
                             } else if (has) {
                                 playlists_.RemoveTrack(pid, trackId);
                             } else {
                                 playlists_.AddTrack(pid, trackId);
                             }
                         },
                         has});
    }
    menu_.trackId = trackId;
    OpenMenu(anchor, std::move(items), "Add to playlist", preferAbove);
}

void App::DrawMenu() {
    if (!menu_.open) return;
    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    const float w = 200, ih = 32, headerH = menu_.header.empty() ? 0 : 27;
    const bool addToPlaylist = !menu_.trackId.empty() && !menu_.header.empty();
    // Menus with checkable rows (playlist membership, settings dropdowns) indent
    // every label past the check column so the mark never overlaps the text.
    const bool hasChecks = addToPlaylist || std::any_of(menu_.items.begin(), menu_.items.end(),
                                                         [](const MenuItem& it) { return it.checked; });
    float h = 8 + headerH;
    for (const auto& it : menu_.items) h += ih + (it.separatorAbove ? 9 : 0);
    if (addToPlaylist) h += 9 + ih;  // divider + "+ New Playlist"
    // Position: above the anchor, right-aligned; flip below if it overflows.
    const float pad = 8;
    float top = menu_.anchor.y - 4 - h;
    float left = menu_.anchor.x + menu_.anchor.width - w;
    if (!menu_.preferAbove || top < pad) top = menu_.anchor.y + menu_.anchor.height + 4;
    top = std::max(pad, std::min(top, H - h - pad));
    left = std::max(pad, std::min(left, W - w - pad));
    const Rectangle box{left, top, w, h};
    const float open = ui::Ease("menu#open", 1.0f, 0.15f, 0.0f);
    const Rectangle d = ui::Scaled(Rectangle{box.x, box.y + 4 * (1 - open), box.width, box.height},
                                   0.95f + 0.05f * open);
    ui::Shadow(d, 12, 0, 8, 32, 0, Fade(BLACK, 0.3f * open));
    ui::RoundedRect(d, 12, Fade(ui::theme.glassSurface, open));
    ui::RoundedRectLines(d, 12, 1, Fade(ui::theme.borderSubtle, open));

    bool clickedItem = false;
    float y = d.y + 4;
    if (!menu_.header.empty()) {
        std::string hdr = menu_.header;
        for (auto& ch : hdr) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        ui::Text(hdr, Vector2{d.x + 12, y + 7}, 10, Fade(ui::theme.textTertiary, open), ui::Face::SansMedium, 0.5f);
        y += headerH;
    }
    const auto row = [&](const std::string& label, bool checked, bool secondary, bool indent) {
        const Rectangle rr{d.x, y, d.width, ih};
        const bool hov = ui::HoverRaw(rr);
        if (hov) DrawRectangleRec(rr, Fade(ui::theme.hover, open));
        float tx = d.x + 12;
        if (checked) ui::IconCheck(Vector2{tx + 6, y + ih / 2}, 12, Fade(ui::theme.text, open));
        if (indent) tx += 20;
        ui::TextEllipsis(label, Vector2{tx, y + 9}, d.width - (tx - d.x) - 12, 12,
                         Fade(secondary ? ui::theme.textSecondary : ui::theme.text, open));
        const bool clicked = !menu_.justOpened && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hov;
        y += ih;
        return clicked;
    };
    for (const auto& it : menu_.items) {
        if (it.separatorAbove) {
            DrawRectangleRec(Rectangle{d.x + 12, y + 4, d.width - 24, 1}, Fade(ui::theme.borderSubtle, open));
            y += 9;
        }
        if (row(it.label, it.checked, it.secondary, hasChecks)) {
            it.fn();
            clickedItem = true;
        }
    }
    if (addToPlaylist) {
        DrawRectangleRec(Rectangle{d.x + 12, y + 4, d.width - 24, 1}, Fade(ui::theme.borderSubtle, open));
        y += 9;
        if (menu_.newInput) {
            const Rectangle field{d.x + 12, y + 6, d.width - 24 - 40, ih - 12};
            if (menu_.newText.empty()) ui::TextV("Name...", field.x, field.y + field.height / 2, 12, ui::theme.textTertiary);
            const int res = ui::TextInput(field, &menu_.newText, 12);
            const Rectangle addR{d.x + d.width - 12 - 36, y + 6, 36, ih - 12};
            ui::TextCentered("Add", Vector2{addR.x + 18, addR.y + addR.height / 2}, 12, ui::theme.accent,
                             ui::Face::SansMedium);
            const bool submit = res == 1 || (!menu_.justOpened && ui::ClickedRaw(addR));
            if (submit) {
                std::string name = menu_.newText;
                while (!name.empty() && name.back() == ' ') name.pop_back();
                if (!name.empty()) {
                    const std::string id = playlists_.Create(name).id;
                    playlists_.AddTrack(id, menu_.trackId);
                }
                menu_.open = false;
            } else if (res == -1) {
                menu_.open = false;
            }
            y += ih;
            clickedItem = submit;
        } else if (row("+ New Playlist", false, true, false)) {
            menu_.newInput = true;
            menu_.newText.clear();
            menu_.justOpened = true;  // swallow this click
        }
    }
    if (!menu_.justOpened &&
        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
        if (clickedItem || !ui::HoverRaw(box)) menu_.open = false;
    }
    if (!menu_.open) menu_.trackId.clear();
    menu_.justOpened = false;
}

void App::DrawToast(float chromeH) {
    if (GetTime() >= toastUntil_) return;
    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    const Vector2 m = ui::Measure(toast_, 13);
    const float w = std::min(m.x + 32, W - 40), h = 40;
    const float fade = std::min(1.0f, static_cast<float>(toastUntil_ - GetTime()) / 0.3f);
    const Rectangle box{W - 20 - w, H - chromeH - h - 16, w, h};
    ui::Shadow(box, 12, 0, 8, 32, 0, Fade(BLACK, 0.4f * fade));
    ui::RoundedRect(box, 12, Fade(ui::theme.elevated, fade));
    ui::RoundedRectLines(box, 12, 1, Fade(ui::theme.borderSubtle, fade));
    ui::TextEllipsis(toast_, Vector2{box.x + 16, box.y + (h - m.y) / 2}, w - 32, 13, Fade(ui::theme.text, fade));
}
