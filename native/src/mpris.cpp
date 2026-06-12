#include "mpris.h"

std::string MprisFileUrl(const std::string& path) {
    static const char* hex = "0123456789ABCDEF";
    std::string url = "file://";
    for (unsigned char c : path) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' ||
                          c == '.' || c == '~';
        if (safe) {
            url += static_cast<char>(c);
        } else {
            url += '%';
            url += hex[c >> 4];
            url += hex[c & 0xF];
        }
    }
    return url;
}

#ifdef CYMAVEIL_HAVE_DBUS

#include <dbus/dbus.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "raylib.h"  // TraceLog

// raylib's GLFW backend; safe to call from any thread, wakes WaitEvents.
extern "C" void glfwPostEmptyEvent(void);

namespace {

constexpr const char* kBusName = "org.mpris.MediaPlayer2.cymaveil";
constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kRootIface = "org.mpris.MediaPlayer2";
constexpr const char* kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";
constexpr const char* kNoTrackPath = "/org/mpris/MediaPlayer2/TrackList/NoTrack";

constexpr const char* kIntrospectXml = R"(<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect"><arg name="xml" type="s" direction="out"/></method>
  </interface>
  <interface name="org.freedesktop.DBus.Properties">
    <method name="Get">
      <arg name="interface" type="s" direction="in"/>
      <arg name="property" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="GetAll">
      <arg name="interface" type="s" direction="in"/>
      <arg name="properties" type="a{sv}" direction="out"/>
    </method>
    <method name="Set">
      <arg name="interface" type="s" direction="in"/>
      <arg name="property" type="s" direction="in"/>
      <arg name="value" type="v" direction="in"/>
    </method>
    <signal name="PropertiesChanged">
      <arg name="interface" type="s"/>
      <arg name="changed" type="a{sv}"/>
      <arg name="invalidated" type="as"/>
    </signal>
  </interface>
  <interface name="org.mpris.MediaPlayer2">
    <method name="Raise"/>
    <method name="Quit"/>
    <property name="CanQuit" type="b" access="read"/>
    <property name="CanRaise" type="b" access="read"/>
    <property name="HasTrackList" type="b" access="read"/>
    <property name="Identity" type="s" access="read"/>
    <property name="DesktopEntry" type="s" access="read"/>
    <property name="SupportedUriSchemes" type="as" access="read"/>
    <property name="SupportedMimeTypes" type="as" access="read"/>
  </interface>
  <interface name="org.mpris.MediaPlayer2.Player">
    <method name="Next"/>
    <method name="Previous"/>
    <method name="Pause"/>
    <method name="PlayPause"/>
    <method name="Stop"/>
    <method name="Play"/>
    <method name="Seek"><arg name="offset" type="x" direction="in"/></method>
    <method name="SetPosition">
      <arg name="trackid" type="o" direction="in"/>
      <arg name="position" type="x" direction="in"/>
    </method>
    <method name="OpenUri"><arg name="uri" type="s" direction="in"/></method>
    <signal name="Seeked"><arg name="position" type="x"/></signal>
    <property name="PlaybackStatus" type="s" access="read"/>
    <property name="LoopStatus" type="s" access="readwrite"/>
    <property name="Rate" type="d" access="readwrite"/>
    <property name="Shuffle" type="b" access="readwrite"/>
    <property name="Metadata" type="a{sv}" access="read"/>
    <property name="Volume" type="d" access="readwrite"/>
    <property name="Position" type="x" access="read"/>
    <property name="MinimumRate" type="d" access="read"/>
    <property name="MaximumRate" type="d" access="read"/>
    <property name="CanGoNext" type="b" access="read"/>
    <property name="CanGoPrevious" type="b" access="read"/>
    <property name="CanPlay" type="b" access="read"/>
    <property name="CanPause" type="b" access="read"/>
    <property name="CanSeek" type="b" access="read"/>
    <property name="CanControl" type="b" access="read"/>
  </interface>
</node>
)";

// ── variant/dict helpers (libdbus is verbose about containers) ──

void VariantBasic(DBusMessageIter* it, int type, const char* sig, const void* val) {
    DBusMessageIter v;
    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, sig, &v);
    dbus_message_iter_append_basic(&v, type, val);
    dbus_message_iter_close_container(it, &v);
}

void VariantString(DBusMessageIter* it, const std::string& s) {
    const char* p = s.c_str();
    VariantBasic(it, DBUS_TYPE_STRING, "s", &p);
}

void VariantObjectPath(DBusMessageIter* it, const std::string& s) {
    const char* p = s.c_str();
    VariantBasic(it, DBUS_TYPE_OBJECT_PATH, "o", &p);
}

void VariantBool(DBusMessageIter* it, bool b) {
    dbus_bool_t v = b ? TRUE : FALSE;
    VariantBasic(it, DBUS_TYPE_BOOLEAN, "b", &v);
}

void VariantDouble(DBusMessageIter* it, double d) { VariantBasic(it, DBUS_TYPE_DOUBLE, "d", &d); }

void VariantInt64(DBusMessageIter* it, long long x) {
    dbus_int64_t v = x;
    VariantBasic(it, DBUS_TYPE_INT64, "x", &v);
}

void VariantStringArray(DBusMessageIter* it, std::initializer_list<const char*> items) {
    DBusMessageIter v, arr;
    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "as", &v);
    dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "s", &arr);
    for (const char* s : items) dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &s);
    dbus_message_iter_close_container(&v, &arr);
    dbus_message_iter_close_container(it, &v);
}

template <typename F>
void DictEntry(DBusMessageIter* arr, const char* key, F&& writeValue) {
    DBusMessageIter e;
    dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    writeValue(&e);
    dbus_message_iter_close_container(arr, &e);
}

std::string TrackObjectPath(const MprisState& s) {
    if (s.trackId.empty()) return kNoTrackPath;
    // Library ids are 16 hex chars, already valid object-path elements.
    return std::string("/com/cymaveil/track/") + s.trackId;
}

// Metadata map as a variant (a{sv} inside v)
void VariantMetadata(DBusMessageIter* it, const MprisState& s) {
    DBusMessageIter v, arr;
    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "a{sv}", &v);
    dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "{sv}", &arr);
    DictEntry(&arr, "mpris:trackid",
              [&](DBusMessageIter* e) { VariantObjectPath(e, TrackObjectPath(s)); });
    if (!s.trackId.empty()) {
        DictEntry(&arr, "mpris:length", [&](DBusMessageIter* e) { VariantInt64(e, s.lengthUs); });
        DictEntry(&arr, "xesam:title", [&](DBusMessageIter* e) { VariantString(e, s.title); });
        DictEntry(&arr, "xesam:album", [&](DBusMessageIter* e) { VariantString(e, s.album); });
        DictEntry(&arr, "xesam:artist", [&](DBusMessageIter* e) {
            DBusMessageIter av, aa;
            dbus_message_iter_open_container(e, DBUS_TYPE_VARIANT, "as", &av);
            dbus_message_iter_open_container(&av, DBUS_TYPE_ARRAY, "s", &aa);
            const char* artist = s.artist.c_str();
            dbus_message_iter_append_basic(&aa, DBUS_TYPE_STRING, &artist);
            dbus_message_iter_close_container(&av, &aa);
            dbus_message_iter_close_container(e, &av);
        });
        if (!s.artUrl.empty()) {
            DictEntry(&arr, "mpris:artUrl", [&](DBusMessageIter* e) { VariantString(e, s.artUrl); });
        }
    }
    dbus_message_iter_close_container(&v, &arr);
    dbus_message_iter_close_container(it, &v);
}

}  // namespace

struct Mpris::Impl {
    DBusConnection* conn = nullptr;
    std::thread thread;
    int wakeFd = -1;  // eventfd: main thread -> worker (state dirty / shutdown)
    std::atomic<bool> running{false};

    std::mutex mutex;
    MprisState pending;        // latest snapshot from the main thread
    long long seekUs = -1;     // >= 0: emit Seeked at this position
    MprisState published;      // worker-side: last state signalled to the bus
    std::atomic<long long> positionUs{0};

    std::mutex reqMutex;
    std::deque<MprisRequest> requests;

    void Queue(MprisRequest r) {
        {
            std::lock_guard lock(reqMutex);
            requests.push_back(std::move(r));
        }
        glfwPostEmptyEvent();  // break the main loop out of idle event-waiting
    }

    void Worker();
    void EmitChanges();
    DBusHandlerResult Handle(DBusMessage* msg);
    void ReplyEmpty(DBusMessage* msg);
    void ReplyError(DBusMessage* msg, const char* name, const char* text);
    bool AppendRootProp(DBusMessageIter* arr, const char* name);
    bool AppendPlayerProp(DBusMessageIter* arr, const char* name, const MprisState& s);
    void HandlePropsGet(DBusMessage* msg);
    void HandlePropsGetAll(DBusMessage* msg);
    void HandlePropsSet(DBusMessage* msg);
    MprisState Snapshot() {
        std::lock_guard lock(mutex);
        return pending;
    }
};

void Mpris::Start() {
    DBusError err;
    dbus_error_init(&err);
    DBusConnection* conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (conn == nullptr) {
        TraceLog(LOG_WARNING, "MPRIS: no session bus (%s)",
                 dbus_error_is_set(&err) ? err.message : "unknown");
        dbus_error_free(&err);
        return;
    }
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    std::string name = kBusName;
    int ret = dbus_bus_request_name(conn, name.c_str(), DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        // Second instance: the spec's escape hatch is a .instance<pid> suffix.
        name += ".instance" + std::to_string(getpid());
        ret = dbus_bus_request_name(conn, name.c_str(), DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    }
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        TraceLog(LOG_WARNING, "MPRIS: could not own a bus name (%s)",
                 dbus_error_is_set(&err) ? err.message : "in use");
        dbus_error_free(&err);
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
        return;
    }

    impl_ = new Impl();
    impl_->conn = conn;
    impl_->wakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    static const DBusObjectPathVTable vtable = {
        /*unregister_function=*/nullptr,
        /*message_function=*/
        [](DBusConnection*, DBusMessage* msg, void* ud) {
            return static_cast<Impl*>(ud)->Handle(msg);
        },
        nullptr, nullptr, nullptr, nullptr,
    };
    dbus_connection_try_register_object_path(conn, kObjectPath, &vtable, impl_, nullptr);

    impl_->running = true;
    impl_->thread = std::thread([impl = impl_] { impl->Worker(); });
    TraceLog(LOG_INFO, "MPRIS: serving %s", name.c_str());
}

void Mpris::Stop() {
    if (impl_ == nullptr) return;
    impl_->running = false;
    uint64_t one = 1;
    [[maybe_unused]] ssize_t n = write(impl_->wakeFd, &one, sizeof(one));
    impl_->thread.join();
    dbus_connection_close(impl_->conn);
    dbus_connection_unref(impl_->conn);
    close(impl_->wakeFd);
    delete impl_;
    impl_ = nullptr;
}

Mpris::~Mpris() { Stop(); }

void Mpris::Publish(const MprisState& s, double positionSec, double dtSec) {
    if (impl_ == nullptr) return;
    const long long posUs = static_cast<long long>(positionSec * 1e6);
    impl_->positionUs.store(posUs, std::memory_order_relaxed);

    // A seek shows up as the position jumping further than playback could
    // have advanced in one frame (same track, no play/pause flip involved).
    long long seekUs = -1;
    if (everSent_ && s.trackId == lastSent_.trackId && !s.trackId.empty()) {
        const long long expected =
            lastPosUs_ + (s.status == "Playing" ? static_cast<long long>(dtSec * 1e6) : 0);
        if (std::llabs(posUs - expected) > 1'000'000) seekUs = posUs;
    }
    lastPosUs_ = posUs;

    if (everSent_ && s == lastSent_ && seekUs < 0) return;
    lastSent_ = s;
    everSent_ = true;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->pending = s;
        if (seekUs >= 0) impl_->seekUs = seekUs;
    }
    uint64_t one = 1;
    [[maybe_unused]] ssize_t n = write(impl_->wakeFd, &one, sizeof(one));
}

bool Mpris::PollRequest(MprisRequest* out) {
    if (impl_ == nullptr) return false;
    std::lock_guard lock(impl_->reqMutex);
    if (impl_->requests.empty()) return false;
    *out = std::move(impl_->requests.front());
    impl_->requests.pop_front();
    return true;
}

void Mpris::Impl::Worker() {
    int dbusFd = -1;
    dbus_connection_get_socket(conn, &dbusFd);
    bool connected = true;

    while (running) {
        pollfd fds[2] = {{wakeFd, POLLIN, 0}, {dbusFd, POLLIN, 0}};
        poll(fds, connected ? 2 : 1, -1);
        if (!running) break;

        if (fds[0].revents != 0) {
            uint64_t drain;
            while (read(wakeFd, &drain, sizeof(drain)) > 0) {
            }
            EmitChanges();
        }
        if (connected && fds[1].revents != 0) {
            if (dbus_connection_read_write(conn, 0) == FALSE) {
                TraceLog(LOG_WARNING, "MPRIS: bus connection lost");
                connected = false;  // keep serving the wake fd so Stop() works
                continue;
            }
            while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS) {
            }
        }
    }
}

// Emits PropertiesChanged for whatever differs from the last published state,
// plus a Seeked signal when the main thread detected a jump.
void Mpris::Impl::EmitChanges() {
    MprisState s;
    long long seek;
    {
        std::lock_guard lock(mutex);
        s = pending;
        seek = seekUs;
        seekUs = -1;
    }

    const MprisState& p = published;
    std::vector<const char*> changed;
    if (s.status != p.status) changed.push_back("PlaybackStatus");
    if (s.loop != p.loop) changed.push_back("LoopStatus");
    if (s.shuffle != p.shuffle) changed.push_back("Shuffle");
    if (s.volume != p.volume) changed.push_back("Volume");
    if (s.trackId != p.trackId || s.title != p.title || s.artist != p.artist ||
        s.album != p.album || s.artUrl != p.artUrl || s.lengthUs != p.lengthUs) {
        changed.push_back("Metadata");
    }
    if (s.hasQueue != p.hasQueue) {
        changed.push_back("CanGoNext");
        changed.push_back("CanGoPrevious");
    }
    if ((s.hasTrack || s.hasQueue) != (p.hasTrack || p.hasQueue)) changed.push_back("CanPlay");
    if (s.hasTrack != p.hasTrack) {
        changed.push_back("CanPause");
        changed.push_back("CanSeek");
    }
    published = s;

    if (!changed.empty()) {
        DBusMessage* sig =
            dbus_message_new_signal(kObjectPath, kPropsIface, "PropertiesChanged");
        DBusMessageIter it, arr;
        dbus_message_iter_init_append(sig, &it);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &kPlayerIface);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
        for (const char* name : changed) AppendPlayerProp(&arr, name, s);
        dbus_message_iter_close_container(&it, &arr);
        DBusMessageIter inv;
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inv);
        dbus_message_iter_close_container(&it, &inv);
        dbus_connection_send(conn, sig, nullptr);
        dbus_message_unref(sig);
    }
    if (seek >= 0) {
        DBusMessage* sig = dbus_message_new_signal(kObjectPath, kPlayerIface, "Seeked");
        dbus_int64_t pos = seek;
        dbus_message_append_args(sig, DBUS_TYPE_INT64, &pos, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, sig, nullptr);
        dbus_message_unref(sig);
    }
    dbus_connection_flush(conn);
}

void Mpris::Impl::ReplyEmpty(DBusMessage* msg) {
    DBusMessage* reply = dbus_message_new_method_return(msg);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
    dbus_connection_flush(conn);
}

void Mpris::Impl::ReplyError(DBusMessage* msg, const char* name, const char* text) {
    DBusMessage* reply = dbus_message_new_error(msg, name, text);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
    dbus_connection_flush(conn);
}

bool Mpris::Impl::AppendRootProp(DBusMessageIter* arr, const char* name) {
    const std::string n = name;
    if (n == "CanQuit") DictEntry(arr, name, [](DBusMessageIter* e) { VariantBool(e, true); });
    else if (n == "CanRaise") DictEntry(arr, name, [](DBusMessageIter* e) { VariantBool(e, true); });
    else if (n == "HasTrackList")
        DictEntry(arr, name, [](DBusMessageIter* e) { VariantBool(e, false); });
    else if (n == "Identity")
        DictEntry(arr, name, [](DBusMessageIter* e) { VariantString(e, "Cymaveil"); });
    else if (n == "DesktopEntry")
        DictEntry(arr, name, [](DBusMessageIter* e) { VariantString(e, "cymaveil"); });
    else if (n == "SupportedUriSchemes")
        DictEntry(arr, name, [](DBusMessageIter* e) { VariantStringArray(e, {"file"}); });
    else if (n == "SupportedMimeTypes")
        DictEntry(arr, name, [](DBusMessageIter* e) {
            VariantStringArray(e, {"audio/mpeg", "audio/flac", "audio/ogg", "audio/x-wav"});
        });
    else
        return false;
    return true;
}

bool Mpris::Impl::AppendPlayerProp(DBusMessageIter* arr, const char* name, const MprisState& s) {
    const std::string n = name;
    if (n == "PlaybackStatus")
        DictEntry(arr, name, [&](DBusMessageIter* e) { VariantString(e, s.status); });
    else if (n == "LoopStatus")
        DictEntry(arr, name, [&](DBusMessageIter* e) { VariantString(e, s.loop); });
    else if (n == "Rate" || n == "MinimumRate" || n == "MaximumRate")
        DictEntry(arr, name, [](DBusMessageIter* e) { VariantDouble(e, 1.0); });
    else if (n == "Shuffle")
        DictEntry(arr, name, [&](DBusMessageIter* e) { VariantBool(e, s.shuffle); });
    else if (n == "Metadata")
        DictEntry(arr, name, [&](DBusMessageIter* e) { VariantMetadata(e, s); });
    else if (n == "Volume")
        DictEntry(arr, name, [&](DBusMessageIter* e) { VariantDouble(e, s.volume); });
    else if (n == "Position")
        DictEntry(arr, name, [&](DBusMessageIter* e) {
            VariantInt64(e, positionUs.load(std::memory_order_relaxed));
        });
    else if (n == "CanGoNext" || n == "CanGoPrevious")
        DictEntry(arr, name, [&](DBusMessageIter* e) { VariantBool(e, s.hasQueue); });
    else if (n == "CanPlay")
        DictEntry(arr, name, [&](DBusMessageIter* e) { VariantBool(e, s.hasTrack || s.hasQueue); });
    else if (n == "CanPause" || n == "CanSeek")
        DictEntry(arr, name, [&](DBusMessageIter* e) { VariantBool(e, s.hasTrack); });
    else if (n == "CanControl")
        DictEntry(arr, name, [](DBusMessageIter* e) { VariantBool(e, true); });
    else
        return false;
    return true;
}

void Mpris::Impl::HandlePropsGet(DBusMessage* msg) {
    const char* iface = nullptr;
    const char* prop = nullptr;
    if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop,
                               DBUS_TYPE_INVALID)) {
        ReplyError(msg, DBUS_ERROR_INVALID_ARGS, "expected (ss)");
        return;
    }
    // Get returns a bare variant, so this can't share the Append*Prop
    // helpers (they write {sv} dict entries); same values, different shape.
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter it;
    dbus_message_iter_init_append(reply, &it);

    const std::string i = iface;
    const std::string p = prop;
    const MprisState s = Snapshot();
    bool ok = true;
    if (i == kRootIface) {
        if (p == "CanQuit" || p == "CanRaise") VariantBool(&it, true);
        else if (p == "HasTrackList") VariantBool(&it, false);
        else if (p == "Identity") VariantString(&it, "Cymaveil");
        else if (p == "DesktopEntry") VariantString(&it, "cymaveil");
        else if (p == "SupportedUriSchemes") VariantStringArray(&it, {"file"});
        else if (p == "SupportedMimeTypes")
            VariantStringArray(&it, {"audio/mpeg", "audio/flac", "audio/ogg", "audio/x-wav"});
        else ok = false;
    } else if (i == kPlayerIface) {
        if (p == "PlaybackStatus") VariantString(&it, s.status);
        else if (p == "LoopStatus") VariantString(&it, s.loop);
        else if (p == "Rate" || p == "MinimumRate" || p == "MaximumRate") VariantDouble(&it, 1.0);
        else if (p == "Shuffle") VariantBool(&it, s.shuffle);
        else if (p == "Metadata") VariantMetadata(&it, s);
        else if (p == "Volume") VariantDouble(&it, s.volume);
        else if (p == "Position") VariantInt64(&it, positionUs.load(std::memory_order_relaxed));
        else if (p == "CanGoNext" || p == "CanGoPrevious") VariantBool(&it, s.hasQueue);
        else if (p == "CanPlay") VariantBool(&it, s.hasTrack || s.hasQueue);
        else if (p == "CanPause" || p == "CanSeek") VariantBool(&it, s.hasTrack);
        else if (p == "CanControl") VariantBool(&it, true);
        else ok = false;
    } else {
        ok = false;
    }

    if (!ok) {
        dbus_message_unref(reply);
        ReplyError(msg, DBUS_ERROR_UNKNOWN_PROPERTY, prop);
        return;
    }
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
    dbus_connection_flush(conn);
}

void Mpris::Impl::HandlePropsGetAll(DBusMessage* msg) {
    const char* iface = nullptr;
    if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID)) {
        ReplyError(msg, DBUS_ERROR_INVALID_ARGS, "expected (s)");
        return;
    }
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter it, arr;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
    const std::string i = iface;
    if (i == kRootIface) {
        for (const char* name : {"CanQuit", "CanRaise", "HasTrackList", "Identity", "DesktopEntry",
                                 "SupportedUriSchemes", "SupportedMimeTypes"}) {
            AppendRootProp(&arr, name);
        }
    } else if (i == kPlayerIface) {
        const MprisState s = Snapshot();
        for (const char* name :
             {"PlaybackStatus", "LoopStatus", "Rate", "Shuffle", "Metadata", "Volume", "Position",
              "MinimumRate", "MaximumRate", "CanGoNext", "CanGoPrevious", "CanPlay", "CanPause",
              "CanSeek", "CanControl"}) {
            AppendPlayerProp(&arr, name, s);
        }
    }
    dbus_message_iter_close_container(&it, &arr);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
    dbus_connection_flush(conn);
}

void Mpris::Impl::HandlePropsSet(DBusMessage* msg) {
    DBusMessageIter it;
    if (!dbus_message_iter_init(msg, &it) ||
        dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) {
        ReplyError(msg, DBUS_ERROR_INVALID_ARGS, "expected (ssv)");
        return;
    }
    const char* iface = nullptr;
    dbus_message_iter_get_basic(&it, &iface);
    dbus_message_iter_next(&it);
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) {
        ReplyError(msg, DBUS_ERROR_INVALID_ARGS, "expected (ssv)");
        return;
    }
    const char* prop = nullptr;
    dbus_message_iter_get_basic(&it, &prop);
    dbus_message_iter_next(&it);
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_VARIANT) {
        ReplyError(msg, DBUS_ERROR_INVALID_ARGS, "expected (ssv)");
        return;
    }
    DBusMessageIter v;
    dbus_message_iter_recurse(&it, &v);

    const std::string i = iface;
    const std::string p = prop;
    const int vt = dbus_message_iter_get_arg_type(&v);
    if (i == kPlayerIface && p == "Volume" && vt == DBUS_TYPE_DOUBLE) {
        double d = 0;
        dbus_message_iter_get_basic(&v, &d);
        Queue({MprisCommand::SetVolume, d, {}});
    } else if (i == kPlayerIface && p == "Shuffle" && vt == DBUS_TYPE_BOOLEAN) {
        dbus_bool_t b = FALSE;
        dbus_message_iter_get_basic(&v, &b);
        Queue({MprisCommand::SetShuffle, b != FALSE ? 1.0 : 0.0, {}});
    } else if (i == kPlayerIface && p == "LoopStatus" && vt == DBUS_TYPE_STRING) {
        const char* s = nullptr;
        dbus_message_iter_get_basic(&v, &s);
        Queue({MprisCommand::SetLoop, 0, s});
    } else if (i == kPlayerIface && p == "Rate") {
        // Accepted and ignored (fixed-rate player)
    } else {
        ReplyError(msg, DBUS_ERROR_PROPERTY_READ_ONLY, prop);
        return;
    }
    ReplyEmpty(msg);
}

DBusHandlerResult Mpris::Impl::Handle(DBusMessage* msg) {
    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    const char* ifaceC = dbus_message_get_interface(msg);
    const char* memberC = dbus_message_get_member(msg);
    const std::string iface = ifaceC != nullptr ? ifaceC : "";
    const std::string member = memberC != nullptr ? memberC : "";

    if (iface == "org.freedesktop.DBus.Introspectable" && member == "Introspect") {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &kIntrospectXml, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, reply, nullptr);
        dbus_message_unref(reply);
        dbus_connection_flush(conn);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (iface == kPropsIface) {
        if (member == "Get") HandlePropsGet(msg);
        else if (member == "GetAll") HandlePropsGetAll(msg);
        else if (member == "Set") HandlePropsSet(msg);
        else return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (iface == kRootIface) {
        if (member == "Raise") Queue({MprisCommand::Raise, 0, {}});
        else if (member == "Quit") Queue({MprisCommand::Quit, 0, {}});
        else return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        ReplyEmpty(msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (iface == kPlayerIface) {
        if (member == "Next") Queue({MprisCommand::Next, 0, {}});
        else if (member == "Previous") Queue({MprisCommand::Previous, 0, {}});
        else if (member == "Pause") Queue({MprisCommand::Pause, 0, {}});
        else if (member == "PlayPause") Queue({MprisCommand::PlayPause, 0, {}});
        else if (member == "Stop") Queue({MprisCommand::Stop, 0, {}});
        else if (member == "Play") Queue({MprisCommand::Play, 0, {}});
        else if (member == "Seek") {
            dbus_int64_t off = 0;
            if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_INT64, &off, DBUS_TYPE_INVALID)) {
                Queue({MprisCommand::SeekBy, static_cast<double>(off) / 1e6, {}});
            }
        } else if (member == "SetPosition") {
            const char* path = nullptr;
            dbus_int64_t pos = 0;
            if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INT64,
                                      &pos, DBUS_TYPE_INVALID)) {
                std::string id = path;
                if (const auto slash = id.rfind('/'); slash != std::string::npos) {
                    id = id.substr(slash + 1);
                }
                Queue({MprisCommand::SetPosition, static_cast<double>(pos) / 1e6, id});
            }
        } else if (member == "OpenUri") {
            // Unsupported; accept silently per the spec's "may be ignored"
        } else {
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
        ReplyEmpty(msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

#else  // !CYMAVEIL_HAVE_DBUS — stubs so non-dbus builds link

struct Mpris::Impl {};
Mpris::~Mpris() = default;
void Mpris::Start() {}
void Mpris::Stop() {}
void Mpris::Publish(const MprisState&, double, double) {}
bool Mpris::PollRequest(MprisRequest*) { return false; }

#endif
