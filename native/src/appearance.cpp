#include "appearance.h"

#ifdef CYMAVEIL_HAVE_DBUS

#include <dbus/dbus.h>

#include "raylib.h"  // TraceLog

namespace appearance {
namespace {

// org.freedesktop.appearance color-scheme: 0 = no preference, 1 = dark, 2 = light.
Scheme FromValue(unsigned value) {
    switch (value) {
        case 1: return Scheme::Dark;
        case 2: return Scheme::Light;
        default: return Scheme::Unknown;
    }
}

// The portal wraps the value in a variant; some implementations double-wrap
// (Read returns v<v<u>>). Recurse through variants until we reach the uint32.
bool ReadScheme(DBusMessageIter* iter, Scheme* out) {
    const int type = dbus_message_iter_get_arg_type(iter);
    if (type == DBUS_TYPE_VARIANT) {
        DBusMessageIter inner;
        dbus_message_iter_recurse(iter, &inner);
        return ReadScheme(&inner, out);
    }
    if (type == DBUS_TYPE_UINT32) {
        dbus_uint32_t v = 0;
        dbus_message_iter_get_basic(iter, &v);
        *out = FromValue(v);
        return true;
    }
    return false;
}

}  // namespace

Scheme SystemScheme() {
    DBusError err;
    dbus_error_init(&err);
    DBusConnection* conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (conn == nullptr) {
        if (dbus_error_is_set(&err)) dbus_error_free(&err);
        return Scheme::Unknown;
    }
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    DBusMessage* msg = dbus_message_new_method_call(
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings", "Read");
    Scheme result = Scheme::Unknown;
    if (msg != nullptr) {
        const char* ns = "org.freedesktop.appearance";
        const char* key = "color-scheme";
        dbus_message_append_args(msg, DBUS_TYPE_STRING, &ns, DBUS_TYPE_STRING, &key,
                                 DBUS_TYPE_INVALID);
        // 400 ms is plenty for a local portal; never hang the UI on a missing one.
        DBusMessage* reply =
            dbus_connection_send_with_reply_and_block(conn, msg, 400, &err);
        if (reply != nullptr) {
            DBusMessageIter iter;
            if (dbus_message_iter_init(reply, &iter)) ReadScheme(&iter, &result);
            dbus_message_unref(reply);
        } else if (dbus_error_is_set(&err)) {
            TraceLog(LOG_INFO, "Appearance: portal color-scheme unavailable (%s)", err.message);
        }
        dbus_message_unref(msg);
    }

    if (dbus_error_is_set(&err)) dbus_error_free(&err);
    dbus_connection_close(conn);
    dbus_connection_unref(conn);
    return result;
}

}  // namespace appearance

#else  // !CYMAVEIL_HAVE_DBUS

namespace appearance {
Scheme SystemScheme() { return Scheme::Unknown; }
}  // namespace appearance

#endif
