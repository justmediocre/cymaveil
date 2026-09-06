// Browse views: Library, Albums, Album detail, Playlists, Playlist detail,
// Search, Settings and the shared TrackList / AlbumCard / EmptyState pieces —
// ports of src/components/views/*.tsx and their building blocks.
#include "app.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "raymath.h"
#include "rlgl.h"

#include "paths.h"
#include "ui.h"

#ifndef CYMAVEIL_VERSION
#define CYMAVEIL_VERSION "dev"
#endif

namespace {

constexpr float kPageX = 40.0f;     // px-10
constexpr float kRowH = 44.0f;      // TrackRow ROW_HEIGHT
constexpr float kHeaderH = 36.0f;   // LibraryView letter header

std::string LetterFor(const std::string& title) {
    size_t i = 0;
    while (i < title.size() && title[i] == ' ') i++;
    if (i >= title.size()) return "#";
    const unsigned char c = static_cast<unsigned char>(title[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return std::string(1, static_cast<char>(std::toupper(c)));
    return "#";
}

std::string Plural(int n, const char* one, const char* many) {
    return TextFormat("%d %s", n, n == 1 ? one : many);
}

// eq-bar-1/2/3 keyframes: heights at 0/25/50/75/100% over 1.2s ease-in-out.
float EqBarHeight(int bar, double t) {
    static const float keys[3][5] = {{4, 10, 6, 12, 4}, {8, 4, 12, 6, 8}, {6, 12, 4, 8, 6}};
    const float delay = bar * 0.2f;
    float phase = static_cast<float>(std::fmod(t - delay, 1.2));
    if (phase < 0) phase += 1.2f;
    const float seg = phase / 0.3f;
    const int i = std::min(3, static_cast<int>(seg));
    const float f = ui::EaseInOut(seg - i);
    return keys[bar][i] + (keys[bar][i + 1] - keys[bar][i]) * f;
}

}  // namespace

void App::DrawAlbumArt(Rectangle r, const Art* art, float radius, float placeholderIcon) {
    const Texture2D* tex = art_.Get(art);
    if (tex != nullptr) {
        ui::RoundedTexture(*tex, ui::CoverSrc(*tex), r, radius, WHITE);
    } else {
        ui::RoundedRect(r, radius, ui::theme.elevated);
        if (placeholderIcon > 0) {
            ui::IconDisc(Vector2{r.x + r.width / 2, r.y + r.height / 2}, placeholderIcon,
                         ui::theme.textTertiary, 1.5f);
        }
    }
}

float App::DrawPageHeader(Rectangle r, const std::string& title, const std::string& count) {
    // pt-6 pb-5: 24px/700 display title + 14px tertiary count, gap-3
    const float y = r.y + 24;
    ui::Text(title, Vector2{r.x + kPageX, y}, 24, ui::theme.text, ui::Face::Display);
    const float tw = ui::Measure(title, 24, ui::Face::Display).x;
    ui::TextV(count, r.x + kPageX + tw + 12, y + 16, 14, ui::theme.textTertiary);
    return y + 32 + 20;
}

bool App::BackLink(Vector2 pos, const std::string& label, const std::string& key) {
    const float w = 16 + 4 + ui::Measure(label, 14).x;
    const Rectangle r{pos.x - 4, pos.y - 4, w + 8, 28};
    const bool hov = ui::Hover(r);
    const float t = ui::Ease(key, hov ? 1.0f : 0.0f, 0.15f, 0.0f);
    const Color col = ui::Mix(ui::theme.textSecondary, ui::theme.text, t);
    const float dx = -2.0f * t;
    ui::IconChevronLeft(Vector2{pos.x + 8 + dx, pos.y + 10}, 16, col);
    ui::TextV(label, pos.x + 20 + dx, pos.y + 10, 14, col);
    return ui::Clicked(r);
}

void App::DrawEmptyState(Rectangle r, const char* icon, const char* title, const char* subtitle) {
    const float t = ui::Ease(std::string("empty#") + title, 1.0f, 0.35f, 0.0f);
    const Vector2 c{r.x + r.width / 2, r.y + r.height / 2};
    const float yOff = 12 * (1 - t);
    // Block: 64 tile + gap 16 + title 24 + subtitle 20 + gap 16 + button 40
    const float blockH = 64 + 16 + 24 + 4 + 20 + 16 + 40;
    float y = c.y - blockH / 2 + yOff;
    ui::RoundedRect(Rectangle{c.x - 32, y, 64, 64}, 16, Fade(ui::theme.accentDim, t));
    const Color ic = Fade(ui::theme.accent, t);
    if (std::string(icon) == "music") ui::IconMusicNote(Vector2{c.x, y + 32}, 32, ic);
    else if (std::string(icon) == "folder") ui::IconFolder(Vector2{c.x, y + 32}, 32, ic);
    else ui::IconDisc(Vector2{c.x, y + 32}, 32, ic);
    y += 64 + 16;
    ui::TextCentered(title, Vector2{c.x, y + 12}, 16, Fade(ui::theme.text, t), ui::Face::SansSemiBold);
    y += 24 + 4;
    ui::TextCentered(subtitle, Vector2{c.x, y + 10}, 14, Fade(ui::theme.textSecondary, t));
    y += 20 + 16;
    // "Add Music Folder": accent pill, white text, px-5 py-2.5 text-sm medium.
    const std::string label = library_.ScanActive() ? "Scanning..." : "Add Music Folder";
    const float bw = 20 + 16 + 8 + ui::Measure(label, 14, ui::Face::SansMedium).x + 20;
    const Rectangle b{c.x - bw / 2, y, bw, 40};
    const bool hov = ui::Hover(b) && !library_.ScanActive();
    const float sc = ui::Spring("empty#btn", hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 0.95f : hov ? 1.05f : 1.0f, 1.0f);
    const Rectangle d = ui::Scaled(b, sc);
    ui::RoundedRect(d, 999, Fade(ui::theme.accent, (library_.ScanActive() ? 0.5f : 1.0f) * t));
    ui::IconFolder(Vector2{d.x + 20 + 8, d.y + 20}, 16, Fade(WHITE, t));
    ui::TextV(label, d.x + 20 + 16 + 8, d.y + 20, 14, Fade(WHITE, t), ui::Face::SansMedium);
    if (ui::Clicked(b) && !library_.ScanActive()) {
        settingsTab_ = SettingsTab::Library;
        folderInputActive_ = true;
        Navigate(View::Settings);
    }
    if (library_.ScanActive()) {
        const ScanStatus st = library_.Status();
        const Rectangle track{c.x - 110, y + 40 + 16, 220, 3};
        ui::RoundedRect(track, 2, ui::theme.borderSubtle);
        if (st.total > 0) {
            ui::RoundedRect(Rectangle{track.x, track.y, track.width * st.current / st.total, 3}, 2, ui::theme.accent);
        }
        ui::TextCentered(TextFormat("Scanning %d / %d files", st.current, st.total), Vector2{c.x, track.y + 16}, 11,
                         ui::theme.textTertiary);
    }
}

// ── TrackList ──

App::TableResult App::DrawTrackList(Rectangle r, const std::vector<const Track*>& tracks, float* scroll,
                                    bool removable, bool autoScroll,
                                    const std::vector<std::string>* letters) {
    TableResult out;
    const int n = static_cast<int>(tracks.size());
    // Layout: rows are 44px; a letter header (36px) precedes each new letter.
    std::vector<float> starts(n + 1);
    std::vector<bool> headerBefore(n, false);
    float total = 0;
    std::string prev;
    for (int i = 0; i < n; i++) {
        if (letters != nullptr && (*letters)[i] != prev) {
            headerBefore[i] = true;
            prev = (*letters)[i];
            total += kHeaderH;
        }
        starts[i] = total;
        total += kRowH;
    }
    starts[n] = total;

    const Track* current = player_.Current();
    // Follow the playing track (queue panel): centre it when it changes.
    if (autoScroll && current != nullptr && current->id != queueFollowId_) {
        for (int i = 0; i < n; i++) {
            if (tracks[i] != nullptr && tracks[i]->id == current->id) {
                queueScrollTarget_ = starts[i] - (r.height - kRowH) / 2;
                queueScrollFollow_ = queueFollowId_.empty() ? 2 : 1;  // 2: instant on first show
                break;
            }
        }
        queueFollowId_ = current->id;
    }
    if (autoScroll && queueScrollFollow_ > 0) {
        const float maxScroll = std::max(0.0f, total - r.height);
        const float target = Clamp(queueScrollTarget_, 0.0f, maxScroll);
        if (queueScrollFollow_ == 2) {
            *scroll = target;
            queueScrollFollow_ = 0;
        } else {
            *scroll += (target - *scroll) * std::min(1.0f, GetFrameTime() * 10.0f);
            if (std::fabs(target - *scroll) < 0.5f) {
                *scroll = target;
                queueScrollFollow_ = 0;
            }
        }
        if (GetMouseWheelMove() != 0 && ui::Hover(r)) queueScrollFollow_ = 0;
    }

    ui::ScrollArea(r, total, scroll);
    BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(std::ceil(r.width)),
                     static_cast<int>(r.height));
    const bool listHover = ui::Hover(r);
    for (int i = 0; i < n; i++) {
        const float y0 = r.y + starts[i] - *scroll;
        if (y0 + kRowH + kHeaderH < r.y) continue;
        if (y0 - kHeaderH > r.y + r.height) break;
        float y = y0;
        if (headerBefore[i]) {
            const float hy = y - kHeaderH;
            // flex items-end px-3, 14px display bold, border-bottom subtle, pb-1
            ui::Text((*letters)[i], Vector2{r.x + 12, hy + kHeaderH - 4 - 17}, 14, ui::theme.textSecondary,
                     ui::Face::Display);
            DrawRectangleRec(Rectangle{r.x, hy + kHeaderH - 1, r.width, 1}, ui::theme.borderSubtle);
        }
        const Track* tp = tracks[i];
        const Rectangle row{r.x, y, r.width, kRowH};
        if (tp == nullptr) {
            ui::Text("(missing track)", Vector2{r.x + 12, y + 14}, 13, ui::theme.textTertiary);
            continue;
        }
        const Track& t = *tp;
        const bool isCurrent = current != nullptr && current->id == t.id;
        const bool hov = listHover && ui::Hover(row);
        const float ht = ui::Ease("row#" + t.id, hov ? 1.0f : 0.0f, 0.15f, 0.0f);
        if (isCurrent) ui::RoundedRect(row, 8, ui::theme.accentDim);
        else if (ht > 0.01f) ui::RoundedRect(row, 8, Fade(ui::theme.hover, ht));

        float x = r.x + 12;
        // Track number / eq indicator (w-5, mono 12, right aligned)
        if (isCurrent) {
            const bool playing = player_.IsPlaying();
            for (int b = 0; b < 3; b++) {
                const float bh = playing ? EqBarHeight(b, GetTime()) : 6.0f;
                const float bx = x + 20 - (3 - b) * 3 - (2 - b) * 2;
                ui::RoundedRect(Rectangle{bx, y + kRowH / 2 - bh / 2, 3, bh}, 1.5f, ui::theme.accent);
            }
        } else {
            ui::TextRight(t.trackNum > 0 ? TextFormat("%d", t.trackNum) : "", Vector2{x + 20, y + 15}, 12,
                          ui::theme.textTertiary, ui::Face::Mono);
        }
        x += 20 + 12;
        const Album* album = library_.AlbumById(t.albumId);
        DrawAlbumArt(Rectangle{x, y + 6, 32, 32}, ResolveArt(&t, album), 4);
        x += 32 + 12;

        // Right side: duration + action buttons
        const std::string dur = ui::FormatTime(t.duration);
        const float durW = ui::Measure(dur, 11, ui::Face::Mono).x;
        float rx = r.x + r.width - 12;
        ui::Text(dur, Vector2{rx - durW, y + 16}, 11, ui::theme.textTertiary, ui::Face::Mono);
        rx -= durW + 12;
        const bool fav = playlists_.IsFavorite(t.id);
        int actions = 1 + (removable ? 1 : 0) + 1;
        rx -= actions * 24 + (actions - 1) * 4;
        const float ax0 = rx;
        float ax = ax0;
        const auto action = [&](const std::string& key, bool alwaysVisible, Color col,
                                const std::function<void(Vector2, Color)>& draw) {
            const Rectangle b{ax, y + kRowH / 2 - 12, 24, 24};
            const float vis = alwaysVisible ? 1.0f : ht;
            bool clicked = false;
            if (vis > 0.01f) {
                clicked = ui::IconButton(key, b, Fade(col, vis), Fade(ui::theme.text, vis), 1.15f, 0.9f, draw);
            }
            ax += 28;
            return clicked;
        };
        if (action("fav#" + t.id, fav, fav ? ui::theme.accent : ui::theme.textTertiary,
                   [fav](Vector2 c, Color col) { ui::IconHeart(c, 13, col, fav); })) {
            playlists_.ToggleFavorite(t.id);
        }
        const Rectangle plusR{ax, y + kRowH / 2 - 12, 24, 24};
        if (action("plus#" + t.id, false, ui::theme.textTertiary,
                   [](Vector2 c, Color col) { ui::IconPlus(c, 14, col); })) {
            OpenTrackMenu(t.id, plusR, true);
        }
        if (removable && action("rm#" + t.id, false, ui::theme.textTertiary,
                                [](Vector2 c, Color col) { ui::IconClose(c, 12, col); })) {
            out.removed = i;
        }

        // Title + artist fill the middle
        const float infoW = ax0 - 12 - x;
        ui::TextEllipsis(t.title, Vector2{x, y + 6}, infoW, 14, ui::theme.text, ui::Face::SansMedium);
        ui::TextEllipsis(!t.artist.empty() ? t.artist : (album != nullptr ? album->artist : ""),
                         Vector2{x, y + 24}, infoW, 12, ui::theme.textTertiary);

        // Row click (outside the action buttons) plays; right-click opens the menu.
        const bool overActions = ui::Hover(Rectangle{ax0, y, ax - ax0, kRowH}) && ht > 0.01f;
        if (hov && !overActions) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) out.clicked = i;
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                const Vector2 m = GetMousePosition();
                OpenTrackMenu(t.id, Rectangle{m.x, m.y, 0, 0}, false);
            }
        }
    }
    EndScissorMode();
    return out;
}

// ── Album grid ──

void App::DrawAlbumGrid(Rectangle r, const std::vector<const Album*>& albums, float* scroll) {
    // repeat(auto-fill, minmax(160px, 1fr)) gap-4
    const float gap = 16;
    const int cols = std::max(1, static_cast<int>((r.width + gap) / (160 + gap)));
    const float cellW = (r.width - (cols - 1) * gap) / cols;
    const float cardH = cellW + 8 + 20 + 16;  // art, mb-2, title line, artist line
    const int rows = (static_cast<int>(albums.size()) + cols - 1) / cols;
    const float contentH = rows * (cardH + gap) - gap + 24;
    ui::ScrollArea(r, contentH, scroll);
    BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(std::ceil(r.width)),
                     static_cast<int>(r.height));
    const bool gridHover = ui::Hover(r);
    for (size_t i = 0; i < albums.size(); i++) {
        const int row = static_cast<int>(i) / cols, col = static_cast<int>(i) % cols;
        const float x = r.x + col * (cellW + gap);
        const float y = r.y + row * (cardH + gap) - *scroll;
        if (y + cardH < r.y || y > r.y + r.height) continue;
        const Album& a = *albums[i];
        const Rectangle card{x, y, cellW, cardH};
        const bool hov = gridHover && ui::Hover(card);
        const Rectangle fab{x + cellW - 8 - 36, y + cellW - 8 - 36, 36, 36};
        const bool overFab = hov && ui::Hover(fab);
        const float sc = ui::Spring("card#" + a.id, hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !overFab ? 0.97f
                                                      : hov ? 1.03f : 1.0f, 1.0f);
        const Vector2 cc{x + cellW / 2, y + cardH / 2};
        rlPushMatrix();
        rlTranslatef(cc.x, cc.y, 0);
        rlScalef(sc, sc, 1);
        rlTranslatef(-cc.x, -cc.y, 0);
        DrawAlbumArt(Rectangle{x, y, cellW, cellW}, &a.art, 12, 40);
        const float fa = ui::Ease("fab#" + a.id, hov ? 1.0f : 0.0f, 0.15f, 0.0f);
        if (fa > 0.01f) {
            DrawCircleV(Vector2{fab.x + 18, fab.y + 18}, 18, Fade(ui::theme.accent, fa));
            ui::IconPlay(Vector2{fab.x + 18 + 1, fab.y + 18}, 18, Fade(ui::theme.bg, fa));
        }
        ui::TextEllipsis(a.title, Vector2{x, y + cellW + 8 + 2}, cellW, 14, ui::theme.text, ui::Face::SansMedium);
        ui::TextEllipsis(a.artist, Vector2{x, y + cellW + 8 + 22}, cellW, 12, ui::theme.textTertiary);
        rlPopMatrix();
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (overFab) PlayAlbum(a.id);
            else OpenAlbum(a.id);
        }
    }
    EndScissorMode();
}

// ── Views ──

void App::DrawLibraryView(Rectangle r) {
    if (library_.Tracks().empty()) {
        DrawEmptyState(r, "music", "No tracks yet", "Add a music folder to get started");
        return;
    }
    const auto tracks = AllTracksSorted();
    float y = DrawPageHeader(r, "Library", Plural(static_cast<int>(tracks.size()), "track", "tracks"));
    if (ui::Pill("lib.shuffle", Vector2{r.x + r.width - kPageX - ui::PillWidth("Shuffle All", true), r.y + 24 + 3},
                 "Shuffle All", ui::theme.accentDim, ui::theme.accent,
                 [](Vector2 c, float s, Color col) { ui::IconShuffle(c, s, col); })) {
        ShufflePlay(tracks);
    }

    std::vector<std::string> letters;
    letters.reserve(tracks.size());
    std::vector<std::string> available;
    for (const Track* t : tracks) {
        letters.push_back(LetterFor(t->title));
        if (available.empty() || available.back() != letters.back()) available.push_back(letters.back());
    }
    const bool rail = available.size() > 1;
    const Rectangle list{r.x + kPageX, y, r.width - 2 * kPageX - (rail ? 24 : 0), r.y + r.height - y - 24};
    const TableResult res = DrawTrackList(list, tracks, &libScroll_, false, false, &letters);
    if (res.clicked >= 0) PlayFromTrackList(tracks, res.clicked);

    if (rail) {
        // Alphabet rail: 20px column, letters 10px bold, active accent on accent-dim
        // Determine the visible letter from the scroll offset.
        float acc = 0;
        std::string visible = available.front();
        std::string prev;
        std::vector<std::pair<std::string, float>> offsets;
        for (size_t i = 0; i < tracks.size(); i++) {
            if (letters[i] != prev) {
                prev = letters[i];
                offsets.emplace_back(prev, acc);
                acc += kHeaderH;
            }
            if (acc + kRowH > libScroll_ && visible == available.front() && offsets.size() > 0 &&
                acc >= libScroll_ - kHeaderH) {
                visible = letters[i];
                break;
            }
            acc += kRowH;
        }
        for (const auto& [l, off] : offsets) {
            if (off <= libScroll_ + 1) visible = l;
        }
        const float railX = r.x + r.width - kPageX - 20;
        const float cellH = std::min(22.0f, list.height / available.size());
        float ly = list.y;
        for (const auto& l : available) {
            const Rectangle b{railX + 1, ly, 18, cellH};
            const bool active = l == visible;
            if (active) ui::RoundedRect(b, 4, ui::theme.accentDim);
            ui::TextCentered(l, Vector2{b.x + 9, b.y + cellH / 2}, 10, active ? ui::theme.accent : ui::theme.textTertiary,
                             ui::Face::SansBold);
            if (ui::Clicked(b)) {
                for (const auto& [ol, off] : offsets)
                    if (ol == l) libScroll_ = off;
            }
            ly += cellH;
        }
    }
}

void App::DrawAlbumsView(Rectangle r) {
    if (library_.Albums().empty()) {
        DrawEmptyState(r, "disc", "No albums yet", "Add a music folder to get started");
        return;
    }
    std::vector<const Album*> albums;
    albums.reserve(library_.Albums().size());
    for (const auto& a : library_.Albums()) albums.push_back(&a);
    std::sort(albums.begin(), albums.end(), [](const Album* a, const Album* b) {
        return Lower(a->title) < Lower(b->title);
    });
    const float y = DrawPageHeader(r, "Albums", Plural(static_cast<int>(albums.size()), "album", "albums"));
    if (ui::Pill("albums.shuffle", Vector2{r.x + r.width - kPageX - ui::PillWidth("Shuffle All", true), r.y + 24 + 3},
                 "Shuffle All", ui::theme.accentDim, ui::theme.accent,
                 [](Vector2 c, float s, Color col) { ui::IconShuffle(c, s, col); })) {
        ShufflePlay(AllTracksSorted());
    }
    DrawAlbumGrid(Rectangle{r.x + kPageX, y, r.width - 2 * kPageX, r.y + r.height - y}, albums, &albumsScroll_);
}

void App::DrawAlbumDetailView(Rectangle r) {
    const Album* album = library_.AlbumById(detailAlbumId_);
    if (album == nullptr) {
        Navigate(View::Albums);
        return;
    }
    const auto tracks = library_.AlbumTracks(album->id);
    if (BackLink(Vector2{r.x + kPageX, r.y + 24}, "Albums", "back.albums")) {
        Navigate(View::Albums);
        return;
    }
    // Header: 200px cover + bottom-aligned text column (gap-6, mb-6)
    const float hy = r.y + 24 + 20 + 16;
    DrawAlbumArt(Rectangle{r.x + kPageX, hy, 200, 200}, &album->art, 12, 64);
    const float tx = r.x + kPageX + 200 + 24;
    const float maxW = r.width - kPageX - (tx - r.x);
    float ty = hy + 200;  // justify-end: stack from the bottom
    const bool hasButtons = !tracks.empty();
    if (hasButtons) {
        ty -= 26;
        float bx = tx;
        if (ui::Pill("ad.play", Vector2{bx, ty}, "Play", ui::theme.accentDim, ui::theme.accent,
                     [](Vector2 c, float s, Color col) { ui::IconPlay(c, s, col); })) {
            PlayAlbum(album->id);
        }
        bx += ui::PillWidth("Play", true) + 8;
        if (ui::Pill("ad.shuffle", Vector2{bx, ty}, "Shuffle", ui::theme.accentDim, ui::theme.accent,
                     [](Vector2 c, float s, Color col) { ui::IconShuffle(c, s, col); })) {
            ShufflePlay(tracks, QueueSource::Album, album->id);
        }
        ty -= 12;
    }
    ty -= 20;
    std::string meta = album->year > 0 ? TextFormat("%d · ", album->year) : "";
    meta += Plural(static_cast<int>(tracks.size()), "track", "tracks");
    ui::Text(meta, Vector2{tx, ty + 2}, 14, ui::theme.textTertiary);
    ty -= 8 + 24;
    ui::TextEllipsis(album->artist, Vector2{tx, ty + 3}, maxW, 16, ui::theme.textSecondary);
    ty -= 4 + 32;
    ui::TextEllipsis(album->title, Vector2{tx, ty + 3}, maxW, 24, ui::theme.text, ui::Face::Display);

    const float listY = hy + 200 + 24;
    const Rectangle list{r.x + kPageX, listY, r.width - 2 * kPageX, r.y + r.height - listY - 24};
    const TableResult res = DrawTrackList(list, tracks, &detailScroll_, false);
    if (res.clicked >= 0) PlayFromTrackList(tracks, res.clicked, QueueSource::Album, album->id);
}

void App::ShowNewPlaylistInput() {
    newPlaylistInput_ = true;
    newPlaylistText_.clear();
    MarkActivity();
}

void App::DrawPlaylistsView(Rectangle r) {
    const auto& lists = playlists_.All();
    float y = DrawPageHeader(r, "Playlists", Plural(static_cast<int>(lists.size()), "playlist", "playlists"));
    float px = r.x + r.width - kPageX - ui::PillWidth("+ New Playlist", false);
    if (ui::Pill("pl.new", Vector2{px, r.y + 24 + 3}, "+ New Playlist", ui::theme.accentDim, ui::theme.accent)) {
        ShowNewPlaylistInput();
    }
    px -= 8 + ui::PillWidth("Import .m3u8", false);
    if (ui::Pill("pl.import", Vector2{px, r.y + 24 + 3}, "Import .m3u8", ui::theme.elevated, ui::theme.textSecondary)) {
        Toast("Drop a .m3u / .m3u8 file anywhere on the window to import it");
    }

    // Inline "new playlist" input (NewPlaylistInput.tsx): elevated field + Create/Cancel
    if (newPlaylistInput_) {
        const Rectangle field{r.x + kPageX, y, r.width - 2 * kPageX - 8 - 68 - 8 - 60, 36};
        ui::RoundedRect(field, 8, ui::theme.elevated);
        if (newPlaylistText_.empty()) ui::TextV("Playlist name...", field.x + 12, field.y + 18, 14, ui::theme.textTertiary);
        const int res = ui::TextInput(Rectangle{field.x + 12, field.y, field.width - 24, field.height},
                                      &newPlaylistText_, 14);
        const Rectangle createR{field.x + field.width + 8, y + 2, 68, 32};
        const Rectangle cancelR{createR.x + createR.width + 8, y + 2, 60, 32};
        ui::RoundedRect(createR, 8, ui::theme.accentDim);
        ui::TextCentered("Create", Vector2{createR.x + 34, createR.y + 16}, 12, ui::theme.accent, ui::Face::SansMedium);
        ui::TextCentered("Cancel", Vector2{cancelR.x + 30, cancelR.y + 16}, 12, ui::theme.textTertiary);
        bool submit = res == 1 || ui::Clicked(createR);
        if (submit) {
            std::string name = newPlaylistText_;
            while (!name.empty() && name.back() == ' ') name.pop_back();
            if (!name.empty()) {
                playlists_.Create(name);
                newPlaylistInput_ = false;
            }
        }
        if (res == -1 || ui::Clicked(cancelR)) newPlaylistInput_ = false;
        y += 36 + 16;
    }

    // Rows: Favorites, Now Playing, then user playlists (space-y-1)
    std::vector<const Playlist*> order;
    if (const Playlist* f = playlists_.ById(Playlists::kFavoritesId)) order.push_back(f);
    if (const Playlist* np = playlists_.ById(Playlists::kNowPlayingId)) order.push_back(np);
    for (const auto& p : lists)
        if (!Playlists::IsSystem(p.id)) order.push_back(&p);

    const float rowH = 40 + 24, stride = rowH + 4;
    const Rectangle area{r.x + kPageX, y, r.width - 2 * kPageX, r.y + r.height - y};
    ui::ScrollArea(area, order.size() * stride + 24, &playlistsScroll_);
    BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(std::ceil(area.width)),
                     static_cast<int>(area.height));
    for (size_t i = 0; i < order.size(); i++) {
        const Playlist& p = *order[i];
        const float ry = area.y + i * stride - playlistsScroll_;
        if (ry + rowH < area.y || ry > area.y + area.height) continue;
        const Rectangle row{area.x, ry, area.width, rowH};
        const bool hov = ui::Hover(row) && ui::Hover(area);
        const float ht = ui::Ease("pl#" + p.id, hov ? 1.0f : 0.0f, 0.15f, 0.0f);
        const float sc = ui::Spring("pl#s" + p.id, hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 0.99f : 1.0f, 1.0f);
        const Rectangle d = ui::Scaled(row, sc);
        if (ht > 0.01f) ui::RoundedRect(d, 12, Fade(ui::theme.hover, ht));
        const bool isFav = p.id == Playlists::kFavoritesId, isNp = p.id == Playlists::kNowPlayingId;
        const Rectangle tile{d.x + 16, d.y + 12, 40, 40};
        ui::RoundedRect(tile, 8, (isFav || isNp) ? ui::theme.accentDim : ui::theme.elevated);
        const Color ic = (isFav || isNp) ? ui::theme.accent : ui::theme.textTertiary;
        const Vector2 tc{tile.x + 20, tile.y + 20};
        if (isFav) ui::IconHeart(tc, 18, ic, true);
        else if (isNp) ui::IconPlay(tc, 18, ic);
        else ui::IconList(tc, 18, ic);
        ui::TextEllipsis(p.name, Vector2{d.x + 16 + 40 + 12, d.y + 14}, d.width - 100, 14, ui::theme.text, ui::Face::SansMedium);
        ui::Text(Plural(static_cast<int>(p.trackIds.size()), "track", "tracks"), Vector2{d.x + 16 + 40 + 12, d.y + 34},
                 12, ui::theme.textTertiary);
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) OpenPlaylist(p.id);
    }
    if (order.size() <= 2 && lists.size() <= 2) {
        ui::TextCentered("No playlists yet — create one above",
                         Vector2{area.x + area.width / 2, area.y + order.size() * stride + 64}, 14,
                         ui::theme.textTertiary);
    }
    EndScissorMode();
}

void App::DrawPlaylistDetailView(Rectangle r) {
    const Playlist* p = playlists_.ById(detailPlaylistId_);
    if (p == nullptr) {
        Navigate(View::Playlists);
        return;
    }
    const bool isFav = p->id == Playlists::kFavoritesId;
    const bool isNowPlaying = p->id == Playlists::kNowPlayingId;
    const bool isProtected = isFav || isNowPlaying;
    const auto tracks = ResolveTracks(p->trackIds);
    const QueueSource src = isNowPlaying ? QueueSource::NowPlaying : QueueSource::Playlist;

    const bool favView = view_ == View::Favorites;
    if (BackLink(Vector2{r.x + kPageX, r.y + 24}, favView ? "Library" : "Playlists", "back.playlists")) {
        Navigate(favView ? View::Library : View::Playlists);
        return;
    }
    // Header: 64px icon tile + text (gap-4, mb-6)
    const float hy = r.y + 24 + 20 + 16;
    const Rectangle tile{r.x + kPageX, hy, 64, 64};
    ui::RoundedRect(tile, 12, isFav ? ui::theme.accentDim : ui::theme.elevated);
    if (isFav) ui::IconHeart(Vector2{tile.x + 32, tile.y + 32}, 28, ui::theme.accent, true);
    else ui::IconList(Vector2{tile.x + 32, tile.y + 32}, 28, ui::theme.textTertiary);
    const float tx = tile.x + 64 + 16;
    const float maxW = r.width - kPageX - (tx - r.x);
    if (editPlaylistId_ == p->id) {
        const int res = ui::TextInput(Rectangle{tx, hy, maxW, 32}, &editText_, 24, ui::Face::Display);
        if (res != 0) {
            std::string name = editText_;
            while (!name.empty() && name.back() == ' ') name.pop_back();
            if (res == 1 && !name.empty() && name != p->name) playlists_.Rename(p->id, name);
            editPlaylistId_.clear();
        }
    } else {
        const Rectangle nameR{tx, hy, std::min(maxW, ui::Measure(p->name, 24, ui::Face::Display).x + 8), 32};
        ui::TextEllipsis(p->name, Vector2{tx, hy + 3}, maxW, 24, ui::theme.text, ui::Face::Display);
        if (!isProtected && ui::Clicked(nameR)) {
            editPlaylistId_ = p->id;
            editText_ = p->name;
        }
    }
    ui::Text(Plural(static_cast<int>(tracks.size()), "track", "tracks"), Vector2{tx, hy + 32 + 4 + 2}, 14,
             ui::theme.textTertiary);
    float bx = tx;
    const float by = hy + 32 + 4 + 20 + 12;
    if (!tracks.empty()) {
        if (ui::Pill("pd.shuffle", Vector2{bx, by}, "Shuffle", ui::theme.accentDim, ui::theme.accent,
                     [](Vector2 c, float s, Color col) { ui::IconShuffle(c, s, col); })) {
            ShufflePlay(tracks, src, p->id);
        }
        bx += ui::PillWidth("Shuffle", true) + 8;
        if (ui::Pill("pd.export", Vector2{bx, by}, "Export", ui::theme.elevated, ui::theme.textSecondary)) {
            ExportPlaylist(*p);
        }
        bx += ui::PillWidth("Export", false) + 8;
    }
    if (!isProtected) {
        const bool armed = deleteArmId_ == p->id;
        const std::string label = armed ? "Confirm delete" : "Delete";
        if (ui::Pill("pd.delete", Vector2{bx, by}, label, ui::theme.elevated,
                     armed ? ui::theme.accent : ui::theme.textSecondary)) {
            if (armed) {
                playlists_.Remove(p->id);
                deleteArmId_.clear();
                Navigate(View::Playlists);
                return;
            }
            deleteArmId_ = p->id;
        }
    }

    const float listY = hy + std::max(64.0f, by + 26 - hy) + 24;
    const Rectangle list{r.x + kPageX, listY, r.width - 2 * kPageX, r.y + r.height - listY - 24};
    if (tracks.empty()) {
        ui::TextCentered(isFav ? "No favorites yet — click the heart on any track"
                               : "No tracks — add some from your library",
                         Vector2{list.x + list.width / 2, list.y + 64}, 14, ui::theme.textTertiary);
        return;
    }
    const TableResult res = DrawTrackList(list, tracks, &plDetailScroll_, !isFav);
    if (res.clicked >= 0) PlayFromTrackList(tracks, res.clicked, src, p->id);
    if (res.removed >= 0) {
        const std::string tid = tracks[res.removed]->id;
        playlists_.RemoveTrack(p->id, tid);
        if (isNowPlaying && player_.Source() == QueueSource::NowPlaying) player_.RemoveTrackId(tid);
    }
}

void App::DrawSearchView(Rectangle r) {
    // Search field: elevated rounded-xl px-4 py-3, icon 18 + 14px input
    const Rectangle box{r.x + kPageX, r.y + 24, r.width - 2 * kPageX, 44};
    ui::RoundedRect(box, 12, ui::theme.elevated);
    ui::RoundedRectLines(box, 12, 2, ui::theme.accent);  // focus-within outline (always focused)
    ui::IconSearch(Vector2{box.x + 16 + 9, box.y + 22}, 18, ui::theme.textTertiary);
    const Rectangle input{box.x + 16 + 18 + 12, box.y, box.width - 16 - 18 - 12 - 16, box.height};
    if (searchQuery_.empty()) ui::TextV("Search tracks and albums...", input.x, input.y + 22, 14, ui::theme.textTertiary);
    const int res = ui::TextInput(input, &searchQuery_, 14);
    if (res == -1) {
        if (searchQuery_.empty()) Navigate(View::Library);
        else searchQuery_.clear();
        return;
    }

    std::string q = Lower(searchQuery_);
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    while (!q.empty() && q.back() == ' ') q.pop_back();
    const float top = box.y + box.height + 20;
    if (q.empty()) {
        ui::TextCentered("Start typing to search", Vector2{r.x + r.width / 2, top + 96}, 14, ui::theme.textTertiary);
        return;
    }
    if (!searchCacheValid_ || searchCacheKey_ != q || searchCacheGen_ != libGeneration_) {
        const auto contains = [&](const std::string& s) { return Lower(s).find(q) != std::string::npos; };
        searchAlbums_.clear();
        for (const auto& a : library_.Albums())
            if (contains(a.title) || contains(a.artist)) searchAlbums_.push_back(&a);
        searchTracks_.clear();
        for (const auto& t : library_.Tracks()) {
            const Album* a = library_.AlbumById(t.albumId);
            if (contains(t.title) || contains(t.artist) || (a != nullptr && contains(a->artist)))
                searchTracks_.push_back(&t);
        }
        searchCacheKey_ = q;
        searchCacheGen_ = libGeneration_;
        searchCacheValid_ = true;
    }
    const auto& albums = searchAlbums_;
    const auto& tracks = searchTracks_;
    if (albums.empty() && tracks.empty()) {
        ui::TextCentered(TextFormat("No results for \"%s\"", searchQuery_.c_str()),
                         Vector2{r.x + r.width / 2, top + 96}, 14, ui::theme.textTertiary);
        return;
    }
    float y = top;
    const float bottom = r.y + r.height - 24;
    if (!albums.empty()) {
        ui::Text("ALBUMS", Vector2{r.x + kPageX, y}, 12, ui::theme.textTertiary, ui::Face::Display, 0.6f);
        y += 16 + 12;
        const float gridH = std::min((bottom - y) * 0.4f, 300.0f);
        DrawAlbumGrid(Rectangle{r.x + kPageX, y, r.width - 2 * kPageX, gridH}, albums, &searchAlbumsScroll_);
        y += gridH + 24;
    }
    if (!tracks.empty() && y < bottom - 60) {
        ui::Text("TRACKS", Vector2{r.x + kPageX, y}, 12, ui::theme.textTertiary, ui::Face::Display, 0.6f);
        y += 16 + 12;
        const TableResult tr = DrawTrackList(Rectangle{r.x + kPageX, y, r.width - 2 * kPageX, bottom - y}, tracks,
                                             &searchTracksScroll_, false);
        if (tr.clicked >= 0) PlayFromTrackList(tracks, tr.clicked);
    }
}

// ── Settings ──

void App::DrawSettingsView(Rectangle r) {
    ui::Text("Settings", Vector2{r.x + kPageX, r.y + 24}, 24, ui::theme.text, ui::Face::Display);
    // Tab bar: px-4 py-2.5 text-sm medium; active accent + accent-dim + 2px underline
    struct Tab {
        const char* label;
        SettingsTab id;
    };
    const Tab tabs[] = {{"Library", SettingsTab::Library},
                        {"Playback", SettingsTab::Playback},
                        {"Visuals", SettingsTab::Visuals},
                        {"Depth Layers", SettingsTab::DepthLayers},
                        {"About", SettingsTab::About}};
    float tx = r.x + kPageX;
    const float ty = r.y + 24 + 32 + 8;
    for (const auto& tab : tabs) {
        const float w = ui::Measure(tab.label, 14, ui::Face::SansMedium).x + 32;
        const Rectangle b{tx, ty, w, 40};
        const bool active = settingsTab_ == tab.id;
        const bool hov = ui::Hover(b);
        const float ht = ui::Ease(std::string("stab#") + tab.label, hov && !active ? 1.0f : 0.0f, 0.15f, 0.0f);
        if (active) {
            DrawRectangleRec(Rectangle{b.x, b.y, b.width, b.height}, ui::theme.accentDim);
            ui::RoundedRect(Rectangle{b.x + 8, b.y + b.height - 1, b.width - 16, 2}, 1, ui::theme.accent);
        }
        ui::TextCentered(tab.label, Vector2{b.x + w / 2, b.y + 20}, 14,
                         active ? ui::theme.accent : ui::Mix(ui::theme.textSecondary, ui::theme.text, ht),
                         ui::Face::SansMedium);
        if (ui::Clicked(b)) {
            settingsTab_ = tab.id;
            settingsScroll_ = 0;
        }
        tx += w + 4;
    }
    DrawRectangleRec(Rectangle{r.x + kPageX, ty + 40, r.width - 2 * kPageX, 1}, ui::theme.borderSubtle);

    const Rectangle area{r.x + kPageX, ty + 41 + 24, r.width - 2 * kPageX, r.y + r.height - (ty + 41 + 24)};
    ui::ScrollArea(area, settingsContentH_, &settingsScroll_);
    BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y - 8), static_cast<int>(std::ceil(area.width)),
                     static_cast<int>(area.height + 8));
    const float sectionW = std::min(512.0f, area.width);
    float y = area.y - settingsScroll_;
    const float x0 = area.x;

    // Building blocks (SettingsControls.tsx)
    const auto section = [&](const std::string& title, const std::string& desc = "") {
        std::string up = title;
        for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        ui::Text(up, Vector2{x0, y}, 12, ui::theme.textTertiary, ui::Face::Display, 0.6f);
        y += 16;
        if (!desc.empty()) {
            ui::Text(desc, Vector2{x0, y + 2}, 12, ui::theme.textSecondary);
            y += 16;
        }
        y += 16;
    };
    // A setting row: label + description on the left, control on the right.
    // Returns the row rect; `rowH` fits two lines.
    const auto row = [&](const std::string& label, const std::string& desc, bool disabled = false) {
        const float h = desc.empty() ? 44.0f : 60.0f;
        const Rectangle rr{x0, y, sectionW, h};
        const float ht = ui::Ease("srow#" + label, ui::Hover(rr) && !disabled ? 1.0f : 0.0f, 0.15f, 0.0f);
        if (ht > 0.01f) ui::RoundedRect(rr, 12, Fade(ui::theme.hover, ht));
        const float a = disabled ? 0.5f : 1.0f;
        ui::Text(label, Vector2{rr.x + 16, rr.y + (desc.empty() ? 12 : 12)}, 14, Fade(ui::theme.text, a),
                 ui::Face::SansMedium);
        if (!desc.empty()) ui::TextEllipsis(desc, Vector2{rr.x + 16, rr.y + 32}, sectionW - 160, 12, Fade(ui::theme.textSecondary, a));
        y += h + 4;
        return rr;
    };
    const auto toggleRow = [&](const std::string& key, const std::string& label, const std::string& desc,
                               bool* value, bool disabled = false) {
        const Rectangle rr = row(label, desc, disabled);
        if (ui::Toggle(key, Vector2{rr.x + rr.width - 16 - 40, rr.y + rr.height / 2 - 11}, *value) && !disabled) {
            *value = !*value;
            config_.Save();
            return true;
        }
        return false;
    };
    int sliderId = 0;
    const auto sliderRow = [&](const std::string& label, const std::string& desc, float* value, float lo,
                               float hi, float step, const std::string& shown, bool disabled = false) {
        const Rectangle rr = row(label, desc, disabled);
        const int id = ++sliderId;
        ui::TextRight(shown, Vector2{rr.x + rr.width - 16, rr.y + rr.height / 2 - 7}, 12, ui::theme.textSecondary,
                      ui::Face::Mono);
        const Rectangle sr{rr.x + rr.width - 16 - 36 - 8 - 96, rr.y + rr.height / 2 - 8, 96, 16};
        float v01 = (*value - lo) / (hi - lo);
        bool dragging = rangeDragging_ && rangeDragId_ == id;
        const bool changed = !disabled && ui::RangeSlider(sr, &v01, &dragging);
        if (dragging) {
            rangeDragging_ = true;
            rangeDragId_ = id;
        } else if (rangeDragId_ == id) {
            rangeDragging_ = false;
            rangeDragId_ = -1;
        }
        if (changed) {
            float nv = lo + std::round(v01 * (hi - lo) / step) * step;
            nv = std::clamp(nv, lo, hi);
            if (nv != *value) {
                *value = nv;
                return true;
            }
        }
        return false;
    };
    const auto selectRow = [&](const std::string& label, const std::string& desc, const std::string& current,
                               const std::vector<std::pair<std::string, std::string>>& options,
                               const std::function<void(const std::string&)>& onPick, bool disabled = false) {
        const Rectangle rr = row(label, desc, disabled);
        std::string shown = current;
        for (const auto& [val, lab] : options)
            if (val == current) shown = lab;
        const float w = ui::Measure(shown, 14).x + 24 + 16;
        const Rectangle b{rr.x + rr.width - 16 - w, rr.y + rr.height / 2 - 16, w, 32};
        ui::RoundedRect(b, 8, ui::theme.elevated);
        ui::RoundedRectLines(b, 8, 1, ui::theme.border);
        ui::TextV(shown, b.x + 12, b.y + 16, 14, ui::theme.text);
        ui::IconChevronLeft(Vector2{b.x + b.width - 12, b.y + 16}, 14, ui::theme.textTertiary);
        if (ui::Clicked(b) && !disabled) {
            std::vector<MenuItem> items;
            for (const auto& [val, lab] : options) {
                const std::string v = val;
                items.push_back({lab, [onPick, v] { onPick(v); }, v == current});
            }
            OpenMenu(b, std::move(items), "", false);
        }
    };

    switch (settingsTab_) {
        case SettingsTab::Library: {
            section("Library");
            const auto& folders = library_.Folders();
            if (folders.empty() && !library_.ScanActive()) {
                ui::Text("No folders added yet. Add a folder to start building your library.",
                         Vector2{x0 + 16, y + 12}, 14, ui::theme.textSecondary);
                y += 44;
            }
            std::string removeFolder;
            for (const auto& f : folders) {
                const Rectangle rr{x0, y, sectionW, 44};
                const float ht = ui::Ease("folder#" + f, ui::Hover(rr) ? 1.0f : 0.0f, 0.15f, 0.0f);
                if (ht > 0.01f) ui::RoundedRect(rr, 12, Fade(ui::theme.hover, ht));
                ui::IconFolder(Vector2{rr.x + 16 + 8, rr.y + 22}, 16, ui::theme.textSecondary);
                ui::TextEllipsis(f, Vector2{rr.x + 16 + 16 + 12, rr.y + 13}, sectionW - 100, 14, ui::theme.text);
                const Rectangle rm{rr.x + rr.width - 16 - 28, rr.y + 8, 28, 28};
                if (ht > 0.01f && ui::IconButton("folder.rm#" + f, rm, Fade(ui::theme.textTertiary, ht), ui::theme.accent,
                                                 1.0f, 0.95f, [](Vector2 c, Color col) { ui::IconClose(c, 14, col, 2.0f); })) {
                    removeFolder = f;
                }
                y += 44 + 4;
            }
            if (library_.ScanActive()) {
                const ScanStatus st = library_.Status();
                const Rectangle track{x0 + 16, y + 8, sectionW - 32, 3};
                ui::RoundedRect(track, 2, ui::theme.borderSubtle);
                if (st.total > 0) ui::RoundedRect(Rectangle{track.x, track.y, track.width * st.current / st.total, 3}, 2, ui::theme.accent);
                ui::Text(TextFormat("Scanning %d / %d files", st.current, st.total), Vector2{x0 + 16, y + 17}, 10, ui::theme.textTertiary);
                y += 40;
            }
            // Add folder: path field + accent-dim button (no native picker; drop also works)
            y += 8;
            const Rectangle field{x0 + 16, y, sectionW - 32 - 8 - 110, 36};
            ui::RoundedRect(field, 8, ui::theme.elevated);
            ui::RoundedRectLines(field, 8, 1, folderInputActive_ ? ui::theme.accent : ui::theme.border);
            const Rectangle fieldText{field.x + 12, field.y, field.width - 24, field.height};
            const auto commitAdd = [&]() {
                std::string path = folderInput_;
                while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) path.erase(path.begin());
                while (!path.empty() && (path.back() == ' ' || path.back() == '\t' || path.back() == '\n')) path.pop_back();
                if (path.empty()) return;
                if (path == "~" || (path.size() >= 2 && path[0] == '~' && path[1] == '/')) path = paths::Home() + path.substr(1);
                if (DirectoryExists(path.c_str())) {
                    library_.AddFolder(path);
                    SyncWatcher();
                    Toast("Added folder");
                    folderInput_.clear();
                    folderInputActive_ = false;
                } else {
                    Toast("Folder not found");
                }
            };
            if (folderInputActive_) {
                const int res = ui::TextInput(fieldText, &folderInput_, 14);
                if (res == 1) commitAdd();
                else if (res == -1) folderInputActive_ = false;
            } else {
                ui::TextEllipsis(folderInput_.empty() ? "/path/to/music" : folderInput_, Vector2{fieldText.x, field.y + 10},
                                 fieldText.width, 14, folderInput_.empty() ? ui::theme.textTertiary : ui::theme.text);
                if (ui::Clicked(field)) folderInputActive_ = true;
            }
            const Rectangle addB{field.x + field.width + 8, y, 110, 36};
            const bool addHov = ui::Hover(addB);
            ui::RoundedRect(addB, 8, addHov ? ui::theme.elevated : ui::theme.accentDim);
            ui::IconPlus(Vector2{addB.x + 12 + 7, addB.y + 18}, 14, ui::theme.accent, 2.0f);
            ui::TextV("Add Folder", addB.x + 12 + 14 + 8, addB.y + 18, 14, ui::theme.accent);
            if (ui::Clicked(addB)) commitAdd();
            if (folderInputActive_ && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !ui::Hover(field) && !ui::Hover(addB)) {
                folderInputActive_ = false;
            }
            y += 36 + 12;
            ui::Text("Tip: drop a folder anywhere on the window to add it.", Vector2{x0 + 16, y}, 12, ui::theme.textTertiary);
            y += 24;
            if (!removeFolder.empty()) {
                library_.RemoveFolder(removeFolder);
                libGeneration_++;
                RebuildMosaic();
                SyncWatcher();
                Toast("Removed folder");
            }
            break;
        }
        case SettingsTab::Playback: {
            section("Integration");
            if (toggleRow("s.mpris", "Desktop media controls", "Expose playback over MPRIS (media keys, applets)",
                          &config_.mpris)) {
                if (config_.mpris) mpris_.Start();
                else mpris_.Stop();
            }
            break;
        }
        case SettingsTab::Visuals: {
            section("Appearance");
            selectRow("Theme", "Choose light, dark, or follow your system setting", config_.theme,
                      {{"system", "System"}, {"dark", "Dark"}, {"light", "Light"}}, [this](const std::string& v) {
                          config_.theme = v;
                          ApplyTheme();
                          config_.Save();
                      });
            y += 32;
            section("Effects");
            toggleRow("s.viz", "Canvas Visualizer", "Frequency bars on album art", &config_.canvasVisualizer);
            toggleRow("s.glass", "Glass Blur", "Frosted backdrop behind panels", &config_.glassBlur);
            toggleRow("s.glow", "Ambient Glow", "Subtle colored underglow around album art edges", &config_.ambientGlow);
            toggleRow("s.bass", "Bass Hit Zoom", "Album art pulses on strong bass hits", &config_.bassShake);
            toggleRow("s.vinyl", "Vinyl Disc", "Spinning vinyl record", &config_.vinylDisc);
            if (toggleRow("s.mosaic", "Background Mosaic", "Isometric album art grid", &config_.mosaicEnabled)) RebuildMosaic();
            if (toggleRow("s.flat", "Flat mode", config_.mosaicFlat ? "Flat 2D grid" : "Isometric 3D perspective",
                          &config_.mosaicFlat, !config_.mosaicEnabled)) backdrop_.MarkDirty();
            {
                float op = std::round(config_.mosaicOpacity * 100);
                if (sliderRow("Opacity", "Background mosaic transparency", &op, 0, 100, 1, TextFormat("%d%%", static_cast<int>(op)),
                              !config_.mosaicEnabled)) {
                    config_.mosaicOpacity = op / 100.0f;
                    backdrop_.MarkDirty();
                    config_.Save();
                }
            }
            {
                static const std::vector<std::pair<std::string, std::string>> trans{
                    {"flip", "3D Flip"}, {"shrink-grow", "Shrink/Grow"}, {"cross-fade", "Cross Fade"},
                    {"fade", "Fade"}, {"iris", "Iris"}, {"random", "Random"}};
                selectRow("Mosaic transition", "Animation when a tile swaps its cover", config_.mosaicTransition, trans,
                          [this](const std::string& v) {
                              config_.mosaicTransition = v;
                              config_.Save();
                          }, !config_.mosaicEnabled);
            }
            {
                float dens = static_cast<float>(config_.mosaicDensity);
                if (sliderRow("Mosaic density", "Number of tile columns in the background grid", &dens, 4, 14, 1,
                              TextFormat("%d", static_cast<int>(dens)), !config_.mosaicEnabled)) {
                    config_.mosaicDensity = static_cast<int>(dens);
                    RebuildMosaic();
                    config_.Save();
                }
            }
            y += 32;
            section("Visualizer");
            {
                std::vector<std::pair<std::string, std::string>> styles;
                for (int i = 0; i < Visualizer::kStyleCount; i++) {
                    const auto s = static_cast<Visualizer::Style>(i);
                    styles.emplace_back(Visualizer::StyleName(s), Visualizer::StyleLabel(s));
                }
                styles.emplace_back("random", "Random");
                selectRow("Style", "Click the album art to cycle styles while playing", config_.visualizerStyle, styles,
                          [this](const std::string& v) {
                              config_.visualizerStyle = v;
                              manualPick_ = -1;
                              config_.Save();
                          });
            }
            {
                float inten = static_cast<float>(config_.visualizerIntensity);
                if (sliderRow("Intensity", "Bar brightness and glow strength", &inten, 10, 100, 5,
                              TextFormat("%d", static_cast<int>(inten)))) {
                    config_.visualizerIntensity = static_cast<int>(inten);
                    config_.Save();
                }
            }
            break;
        }
        case SettingsTab::DepthLayers: {
            section("Depth layers", "Foreground masks let the visualizer play behind the subject of the cover.");
            if (toggleRow("s.depth", "Depth layers", "Separate the cover's subject from its background (downloads a ~25 MB model once)",
                          &config_.depthLayers)) {
                fg_.checkedKey.clear();
            }
            {
                const Rectangle rr = row("Status", TextFormat("Engine: %s", depth_.StatusText()));
                if (depth_.Busy()) ui::IconSpinner(Vector2{rr.x + rr.width - 16 - 10, rr.y + rr.height / 2}, 16, ui::theme.textTertiary);
            }
            {
                const Rectangle rr = row("Paint a mask by hand", "Open the brush editor for the cover on screen (B)");
                const float w = ui::PillWidth("Open editor", false);
                const bool ok = artView_.HasDisplayed();
                if (ui::Pill("s.brush", Vector2{rr.x + rr.width - 16 - w, rr.y + rr.height / 2 - 13}, "Open editor",
                             ui::theme.accentDim, Fade(ui::theme.accent, ok ? 1.0f : 0.5f)) && ok) {
                    OpenBrushEditor(artView_.Displayed());
                }
            }
            break;
        }
        case SettingsTab::About: {
            ui::Text("Cymaveil", Vector2{x0, y}, 20, ui::theme.text, ui::Face::Display);
            y += 28;
            ui::Text(TextFormat("Version %s (native)", CYMAVEIL_VERSION), Vector2{x0, y + 2}, 12, ui::theme.textSecondary);
            y += 24;
            ui::Text("Your music player deserves a glow up.", Vector2{x0, y}, 14, ui::theme.textSecondary);
            y += 24;
            ui::Text("MIT licensed. Fonts: Outfit, Bricolage Grotesque, JetBrains Mono (SIL OFL 1.1).", Vector2{x0, y}, 12,
                     ui::theme.textTertiary);
            y += 20;
            ui::Text("Shortcuts: Space play/pause · Ctrl+←/→ prev/next · ←/→ seek · ↑/↓ volume · S shuffle · R repeat",
                     Vector2{x0, y}, 12, ui::theme.textTertiary);
            y += 18;
            ui::Text("Ctrl+T queue · Ctrl+F search · 1–4 views · , settings · B paint mask · F11 fullscreen · F3 debug",
                     Vector2{x0, y}, 12, ui::theme.textTertiary);
            y += 24;
            break;
        }
    }
    EndScissorMode();
    settingsContentH_ = (y + settingsScroll_) - area.y + 24;
}
