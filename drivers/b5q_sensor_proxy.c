// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Jerry-zirui (https://github.com/Jerry-zirui/postmarketOS_on_zflip5)
/*
 * b5q-sensor-proxy: Lightweight net.hadess.SensorProxy D-Bus bridge for
 * Samsung Galaxy Z Flip 5 (SM8550) Qualcomm Snapdragon Sensor Core via libssc.
 *
 * Bridges SSCSensorAccelerometer (QRTR Service 400 -> ST LSM6DSV on ADSP)
 * to the standard freedesktop net.hadess.SensorProxy system bus API used by
 * QtSensors / KWin / KDE Plasma Mobile / monitor-sensor.
 *
 * Build on target:
 *   gcc -O2 -Wall -Wextra -o /usr/local/bin/b5q-sensor-proxy \
 *       /usr/local/src/b5q_sensor_proxy.c \
 *       $(pkg-config --cflags --libs libssc-glib gio-2.0) -lm
 */

#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <glib-unix.h>
#include <libssc-sensor-accelerometer.h>
#include <libssc-sensor.h>

#define BUS_NAME        "net.hadess.SensorProxy"
#define OBJECT_PATH     "/net/hadess/SensorProxy"
#define INTERFACE_NAME  "net.hadess.SensorProxy"

/* Orientation thresholds from iio-sensor-proxy / sensorfw */
#define RADIANS_TO_DEGREES   (180.0 / M_PI)
#define SAME_AXIS_LIMIT      5
#define THRESHOLD_LANDSCAPE  35
#define THRESHOLD_PORTRAIT   35
#define THRESHOLD_FACE_DOWN  70
#define THRESHOLD_TILT       20
#define TILT_MAX             90

typedef enum {
	ORIENTATION_UNDEFINED = 0,
	ORIENTATION_NORMAL,
	ORIENTATION_BOTTOM_UP,
	ORIENTATION_LEFT_UP,
	ORIENTATION_RIGHT_UP,
} OrientationUp;

typedef enum {
	TILT_UNDEFINED = 0,
	TILT_VERTICAL,
	TILT_UP,
	TILT_DOWN,
	FACE_UP,
	FACE_DOWN,
} Tilt;

static const char *const orientation_strings[] = {
	"undefined",
	"normal",
	"bottom-up",
	"left-up",
	"right-up",
};

static const char *const tilt_strings[] = {
	"undefined",
	"vertical",
	"tilted-up",
	"tilted-down",
	"face-up",
	"face-down",
};

static const gchar introspection_xml[] =
	"<node>"
	"  <interface name='net.hadess.SensorProxy'>"
	"    <method name='ClaimAccelerometer'/>"
	"    <method name='ReleaseAccelerometer'/>"
	"    <method name='ClaimLight'/>"
	"    <method name='ReleaseLight'/>"
	"    <method name='ClaimProximity'/>"
	"    <method name='ReleaseProximity'/>"
	"    <property name='HasAccelerometer' type='b' access='read'/>"
	"    <property name='AccelerometerOrientation' type='s' access='read'/>"
	"    <property name='AccelerometerTilt' type='s' access='read'/>"
	"    <property name='HasAmbientLight' type='b' access='read'/>"
	"    <property name='LightLevelUnit' type='s' access='read'/>"
	"    <property name='LightLevel' type='d' access='read'/>"
	"    <property name='HasProximity' type='b' access='read'/>"
	"    <property name='ProximityNear' type='b' access='read'/>"
	"  </interface>"
	"</node>";

typedef struct {
	GMainLoop *loop;
	GDBusConnection *conn;
	GDBusNodeInfo *introspection;
	guint owner_id;
	guint reg_id;
	guint retry_timer_id;

	/* Map: strdup(dbus_sender) -> GUINT_TO_POINTER(name_watch_id) */
	GHashTable *claimants;

	SSCSensorAccelerometer *accel;
	gulong meas_sig_id;
	gboolean ssc_open;
	gboolean debug;

	OrientationUp orientation;
	Tilt tilt;
} SensorProxyApp;

static void update_ssc_streaming(SensorProxyApp *app);
static gboolean try_init_ssc_cb(gpointer user_data);

static OrientationUp
calc_orientation(OrientationUp prev, double x, double y, double z)
{
	OrientationUp ret = prev;
	long portrait_rotation;
	long landscape_rotation;

	portrait_rotation = lround(atan2(x, sqrt(y * y + z * z)) * RADIANS_TO_DEGREES);
	landscape_rotation = lround(atan2(y, sqrt(x * x + z * z)) * RADIANS_TO_DEGREES);

	/* Avoid flipping while straddling both thresholds */
	if (labs(portrait_rotation) > THRESHOLD_PORTRAIT &&
	    labs(landscape_rotation) > THRESHOLD_LANDSCAPE)
		return prev;

	if (labs(portrait_rotation) > THRESHOLD_PORTRAIT) {
		ret = (portrait_rotation > 0) ? ORIENTATION_LEFT_UP : ORIENTATION_RIGHT_UP;

		if (prev == ORIENTATION_LEFT_UP || prev == ORIENTATION_RIGHT_UP) {
			if (labs(portrait_rotation) < SAME_AXIS_LIMIT)
				ret = prev;
		}
	} else if (labs(landscape_rotation) > THRESHOLD_LANDSCAPE) {
		ret = (landscape_rotation > 0) ? ORIENTATION_BOTTOM_UP : ORIENTATION_NORMAL;

		if (prev == ORIENTATION_BOTTOM_UP || prev == ORIENTATION_NORMAL) {
			if (labs(landscape_rotation) < SAME_AXIS_LIMIT)
				ret = prev;
		}
	}

	return ret;
}

static Tilt
calc_tilt(double x, double y, double z)
{
	long tilt_deg = lround(atan2(z, sqrt(x * x + y * y)) * RADIANS_TO_DEGREES);
	long abs_tilt = labs(tilt_deg);

	if (abs_tilt <= THRESHOLD_TILT)
		return TILT_VERTICAL;
	if (abs_tilt < THRESHOLD_FACE_DOWN)
		return (tilt_deg > 0) ? TILT_UP : TILT_DOWN;
	if (abs_tilt <= TILT_MAX)
		return (tilt_deg > 0) ? FACE_UP : FACE_DOWN;
	return TILT_UNDEFINED;
}

static void
emit_properties_changed(SensorProxyApp *app, gboolean orient_changed, gboolean tilt_changed)
{
	GVariantBuilder builder;
	GVariantBuilder invalidated;
	GError *error = NULL;

	if (!app->conn || (!orient_changed && !tilt_changed))
		return;

	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
	if (orient_changed) {
		g_variant_builder_add(&builder, "{sv}", "AccelerometerOrientation",
				      g_variant_new_string(orientation_strings[app->orientation]));
	}
	if (tilt_changed) {
		g_variant_builder_add(&builder, "{sv}", "AccelerometerTilt",
				      g_variant_new_string(tilt_strings[app->tilt]));
	}
	g_variant_builder_init(&invalidated, G_VARIANT_TYPE("as"));

	g_dbus_connection_emit_signal(
		app->conn,
		NULL,
		OBJECT_PATH,
		"org.freedesktop.DBus.Properties",
		"PropertiesChanged",
		g_variant_new("(sa{sv}as)", INTERFACE_NAME, &builder, &invalidated),
		&error);

	if (error) {
		fprintf(stderr, "[b5q-sensor] PropertiesChanged failed: %s\n", error->message);
		g_clear_error(&error);
	}
}

static void
on_measurement(SSCSensorAccelerometer *sensor G_GNUC_UNUSED,
	       gfloat accel_x,
	       gfloat accel_y,
	       gfloat accel_z,
	       gpointer user_data)
{
	SensorProxyApp *app = user_data;
	/*
	 * Z Flip 5 SM8550 ADSP SSC axes use Android coordinates (+X right,
	 * +Y up, +Z out of screen for proper acceleration). Negating X and Y
	 * aligns with sensorfw/iio-sensor-proxy orientation_calc() while
	 * preserving +Z = face-up.
	 */
	double x = -(double) accel_x;
	double y = -(double) accel_y;
	double z = (double) accel_z;

	OrientationUp new_orient = calc_orientation(app->orientation, x, y, z);
	Tilt new_tilt = calc_tilt(x, y, z);
	gboolean orient_changed = (new_orient != app->orientation);
	gboolean tilt_changed = (new_tilt != app->tilt);

	if (app->debug || orient_changed) {
		fprintf(stderr,
			"[b5q-sensor] accel=(%.3f, %.3f, %.3f) orient=%s -> %s tilt=%s\n",
			accel_x, accel_y, accel_z,
			orientation_strings[app->orientation],
			orientation_strings[new_orient],
			tilt_strings[new_tilt]);
		fflush(stderr);
	}

	app->orientation = new_orient;
	app->tilt = new_tilt;

	emit_properties_changed(app, orient_changed, tilt_changed);
}

static void
close_ssc_sensor(SensorProxyApp *app)
{
	GError *error = NULL;

	if (!app->accel)
		return;

	if (app->meas_sig_id) {
		g_signal_handler_disconnect(app->accel, app->meas_sig_id);
		app->meas_sig_id = 0;
	}

	if (app->ssc_open) {
		if (!ssc_sensor_accelerometer_close_sync(app->accel, NULL, &error)) {
			fprintf(stderr, "[b5q-sensor] close_sync warning: %s\n",
				error ? error->message : "unknown");
			g_clear_error(&error);
		}
		app->ssc_open = FALSE;
	}
}

static void
update_ssc_streaming(SensorProxyApp *app)
{
	guint count = g_hash_table_size(app->claimants);
	GError *error = NULL;

	if (!app->accel)
		return;

	if (count > 0 && !app->ssc_open) {
		if (!app->meas_sig_id) {
			app->meas_sig_id = g_signal_connect(
				app->accel, "measurement",
				G_CALLBACK(on_measurement), app);
		}
		if (!ssc_sensor_accelerometer_open_sync(app->accel, NULL, &error)) {
			fprintf(stderr, "[b5q-sensor] open_sync failed: %s (will re-init)\n",
				error ? error->message : "unknown");
			g_clear_error(&error);
			if (app->meas_sig_id) {
				g_signal_handler_disconnect(app->accel, app->meas_sig_id);
				app->meas_sig_id = 0;
			}
			g_clear_object(&app->accel);
			if (!app->retry_timer_id)
				app->retry_timer_id = g_timeout_add_seconds(2, try_init_ssc_cb, app);
			return;
		}
		app->ssc_open = TRUE;
		fprintf(stderr, "[b5q-sensor] SSC accelerometer opened (claimants=%u)\n", count);
		fflush(stderr);
	} else if (count == 0 && app->ssc_open) {
		close_ssc_sensor(app);
		fprintf(stderr, "[b5q-sensor] SSC accelerometer closed (claimants=0)\n");
		fflush(stderr);
	}
}

static gboolean
try_init_ssc_cb(gpointer user_data)
{
	SensorProxyApp *app = user_data;
	GError *error = NULL;
	gchar *name = NULL;

	if (app->accel) {
		app->retry_timer_id = 0;
		return G_SOURCE_REMOVE;
	}

	app->accel = ssc_sensor_accelerometer_new_sync(NULL, &error);
	if (!app->accel) {
		if (app->debug) {
			fprintf(stderr, "[b5q-sensor] Waiting for SSC service 400: %s\n",
				error ? error->message : "unknown");
		}
		g_clear_error(&error);
		return G_SOURCE_CONTINUE;
	}

	g_object_get(app->accel, SSC_SENSOR_NAME, &name, NULL);
	fprintf(stderr, "[b5q-sensor] Connected to SSC accelerometer: %s\n",
		name ? name : "unknown");
	fflush(stderr);
	g_free(name);

	app->retry_timer_id = 0;
	update_ssc_streaming(app);
	return G_SOURCE_REMOVE;
}

static void
on_claimant_vanished(GDBusConnection *conn G_GNUC_UNUSED,
		     const gchar *name,
		     gpointer user_data)
{
	SensorProxyApp *app = user_data;
	gpointer val = NULL;

	if (g_hash_table_lookup_extended(app->claimants, name, NULL, &val)) {
		guint watch_id = GPOINTER_TO_UINT(val);
		g_hash_table_remove(app->claimants, name);
		if (watch_id)
			g_bus_unwatch_name(watch_id);
		fprintf(stderr, "[b5q-sensor] Claimant %s vanished (claimants=%u)\n",
			name, g_hash_table_size(app->claimants));
		fflush(stderr);
		update_ssc_streaming(app);
	}
}

static void
add_claimant(SensorProxyApp *app, const gchar *sender)
{
	if (!sender || g_hash_table_contains(app->claimants, sender))
		return;

	guint watch_id = g_bus_watch_name_on_connection(
		app->conn,
		sender,
		G_BUS_NAME_WATCHER_FLAGS_NONE,
		NULL,
		on_claimant_vanished,
		app,
		NULL);

	g_hash_table_insert(app->claimants, g_strdup(sender), GUINT_TO_POINTER(watch_id));
	fprintf(stderr, "[b5q-sensor] ClaimAccelerometer from %s (claimants=%u)\n",
		sender, g_hash_table_size(app->claimants));
	fflush(stderr);
	update_ssc_streaming(app);
}

static void
remove_claimant(SensorProxyApp *app, const gchar *sender)
{
	gpointer val = NULL;

	if (!sender)
		return;

	if (g_hash_table_lookup_extended(app->claimants, sender, NULL, &val)) {
		guint watch_id = GPOINTER_TO_UINT(val);
		g_hash_table_remove(app->claimants, sender);
		if (watch_id)
			g_bus_unwatch_name(watch_id);
		fprintf(stderr, "[b5q-sensor] ReleaseAccelerometer from %s (claimants=%u)\n",
			sender, g_hash_table_size(app->claimants));
		fflush(stderr);
		update_ssc_streaming(app);
	}
}

static void
handle_method_call(GDBusConnection *connection G_GNUC_UNUSED,
		   const gchar *sender,
		   const gchar *object_path G_GNUC_UNUSED,
		   const gchar *interface_name G_GNUC_UNUSED,
		   const gchar *method_name,
		   GVariant *parameters G_GNUC_UNUSED,
		   GDBusMethodInvocation *invocation,
		   gpointer user_data)
{
	SensorProxyApp *app = user_data;

	if (g_strcmp0(method_name, "ClaimAccelerometer") == 0) {
		add_claimant(app, sender);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "ReleaseAccelerometer") == 0) {
		remove_claimant(app, sender);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "ClaimLight") == 0 ||
		   g_strcmp0(method_name, "ReleaseLight") == 0 ||
		   g_strcmp0(method_name, "ClaimProximity") == 0 ||
		   g_strcmp0(method_name, "ReleaseProximity") == 0) {
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else {
		g_dbus_method_invocation_return_dbus_error(
			invocation,
			"org.freedesktop.DBus.Error.UnknownMethod",
			"Unknown method on net.hadess.SensorProxy");
	}
}

static GVariant *
handle_get_property(GDBusConnection *connection G_GNUC_UNUSED,
		    const gchar *sender G_GNUC_UNUSED,
		    const gchar *object_path G_GNUC_UNUSED,
		    const gchar *interface_name G_GNUC_UNUSED,
		    const gchar *property_name,
		    GError **error,
		    gpointer user_data)
{
	SensorProxyApp *app = user_data;

	if (g_strcmp0(property_name, "HasAccelerometer") == 0)
		return g_variant_new_boolean(TRUE);
	if (g_strcmp0(property_name, "AccelerometerOrientation") == 0)
		return g_variant_new_string(orientation_strings[app->orientation]);
	if (g_strcmp0(property_name, "AccelerometerTilt") == 0)
		return g_variant_new_string(tilt_strings[app->tilt]);
	if (g_strcmp0(property_name, "HasAmbientLight") == 0)
		return g_variant_new_boolean(FALSE);
	if (g_strcmp0(property_name, "LightLevelUnit") == 0)
		return g_variant_new_string("lux");
	if (g_strcmp0(property_name, "LightLevel") == 0)
		return g_variant_new_double(0.0);
	if (g_strcmp0(property_name, "HasProximity") == 0)
		return g_variant_new_boolean(FALSE);
	if (g_strcmp0(property_name, "ProximityNear") == 0)
		return g_variant_new_boolean(FALSE);

	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		    "Unknown property %s", property_name);
	return NULL;
}

static const GDBusInterfaceVTable interface_vtable = {
	.method_call = handle_method_call,
	.get_property = handle_get_property,
	.set_property = NULL,
};

static void
on_bus_acquired(GDBusConnection *connection,
		const gchar *name G_GNUC_UNUSED,
		gpointer user_data)
{
	SensorProxyApp *app = user_data;
	GError *error = NULL;

	app->conn = connection;
	app->reg_id = g_dbus_connection_register_object(
		connection,
		OBJECT_PATH,
		app->introspection->interfaces[0],
		&interface_vtable,
		app,
		NULL,
		&error);

	if (app->reg_id == 0) {
		fprintf(stderr, "[b5q-sensor] Failed to register %s: %s\n",
			OBJECT_PATH, error ? error->message : "unknown");
		g_clear_error(&error);
		g_main_loop_quit(app->loop);
		return;
	}

	fprintf(stderr, "[b5q-sensor] Exported %s on system D-Bus\n", OBJECT_PATH);
	fflush(stderr);
}

static void
on_name_acquired(GDBusConnection *connection G_GNUC_UNUSED,
		 const gchar *name,
		 gpointer user_data)
{
	SensorProxyApp *app = user_data;

	fprintf(stderr, "[b5q-sensor] Acquired D-Bus name %s\n", name);
	fflush(stderr);

	if (try_init_ssc_cb(app) == G_SOURCE_CONTINUE && !app->retry_timer_id)
		app->retry_timer_id = g_timeout_add_seconds(2, try_init_ssc_cb, app);
}

static void
on_name_lost(GDBusConnection *connection G_GNUC_UNUSED,
	     const gchar *name,
	     gpointer user_data)
{
	SensorProxyApp *app = user_data;

	fprintf(stderr, "[b5q-sensor] Lost D-Bus name %s, exiting\n", name);
	fflush(stderr);
	g_main_loop_quit(app->loop);
}

static gboolean
on_signal_quit(gpointer user_data)
{
	SensorProxyApp *app = user_data;

	fprintf(stderr, "[b5q-sensor] Signal received, shutting down cleanly\n");
	fflush(stderr);
	g_main_loop_quit(app->loop);
	return G_SOURCE_REMOVE;
}

static void
unwatch_entry(gpointer key G_GNUC_UNUSED, gpointer value, gpointer user_data G_GNUC_UNUSED)
{
	guint watch_id = GPOINTER_TO_UINT(value);
	if (watch_id)
		g_bus_unwatch_name(watch_id);
}

int
main(int argc, char **argv)
{
	SensorProxyApp app;
	GError *error = NULL;

	memset(&app, 0, sizeof(app));
	app.orientation = ORIENTATION_NORMAL;
	app.tilt = FACE_UP;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-v") == 0)
			app.debug = TRUE;
	}

	app.introspection = g_dbus_node_info_new_for_xml(introspection_xml, &error);
	if (!app.introspection) {
		fprintf(stderr, "[b5q-sensor] Introspection XML error: %s\n",
			error ? error->message : "unknown");
		g_clear_error(&error);
		return 1;
	}

	app.claimants = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	app.loop = g_main_loop_new(NULL, FALSE);

	g_unix_signal_add(SIGINT, on_signal_quit, &app);
	g_unix_signal_add(SIGTERM, on_signal_quit, &app);

	app.owner_id = g_bus_own_name(
		G_BUS_TYPE_SYSTEM,
		BUS_NAME,
		G_BUS_NAME_OWNER_FLAGS_REPLACE | G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
		on_bus_acquired,
		on_name_acquired,
		on_name_lost,
		&app,
		NULL);

	g_main_loop_run(app.loop);

	if (app.retry_timer_id) {
		g_source_remove(app.retry_timer_id);
		app.retry_timer_id = 0;
	}

	close_ssc_sensor(&app);
	g_clear_object(&app.accel);

	g_hash_table_foreach(app.claimants, unwatch_entry, NULL);
	g_hash_table_destroy(app.claimants);

	if (app.reg_id && app.conn)
		g_dbus_connection_unregister_object(app.conn, app.reg_id);
	if (app.owner_id)
		g_bus_unown_name(app.owner_id);

	g_dbus_node_info_unref(app.introspection);
	g_main_loop_unref(app.loop);

	return 0;
}
