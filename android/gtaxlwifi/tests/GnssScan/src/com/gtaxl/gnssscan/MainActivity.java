/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.gtaxl.gnssscan;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.location.GnssStatus;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;
import android.widget.TextView;

/**
 * Requests locations from the GPS provider while the activity is in the
 * foreground, and logs everything to logcat (tag GnssScan):
 *
 *   SAT  on every GnssStatus: satellites seen, used in the fix, max C/N0
 *   FIX  on every location: coordinates, accuracy, fix time and offset from
 *        the system clock — an offset of ~619 315 200 s (1024 weeks) is the
 *        uncorrected rollover (§54.5)
 */
public class MainActivity extends Activity {

    private static final String TAG = "GnssScan";
    private LocationManager lm;
    private TextView text;
    private final long start = SystemClock.elapsedRealtime();

    private final GnssStatus.Callback statusCallback = new GnssStatus.Callback() {
        @Override
        public void onSatelliteStatusChanged(GnssStatus s) {
            int used = 0;
            float max = 0;
            StringBuilder types = new StringBuilder();
            for (int i = 0; i < s.getSatelliteCount(); i++) {
                if (s.usedInFix(i)) used++;
                max = Math.max(max, s.getCn0DbHz(i));
                types.append(s.getConstellationType(i));
            }
            String line = String.format("SAT t=%ds seen=%d used=%d cn0max=%.1f const=%s",
                    (SystemClock.elapsedRealtime() - start) / 1000,
                    s.getSatelliteCount(), used, max, types);
            Log.i(TAG, line);
            text.setText(line);
        }

        @Override
        public void onFirstFix(int ms) {
            Log.i(TAG, "FIRSTFIX ttff=" + ms + "ms");
        }
    };

    private final LocationListener listener = new LocationListener() {
        @Override
        public void onLocationChanged(Location l) {
            long offset = (l.getTime() - System.currentTimeMillis()) / 1000;
            String line = String.format("FIX t=%ds lat=%.6f lon=%.6f acc=%.0fm time=%d offset=%ds",
                    (SystemClock.elapsedRealtime() - start) / 1000,
                    l.getLatitude(), l.getLongitude(), l.getAccuracy(), l.getTime(), offset);
            Log.i(TAG, line);
            text.append("\n" + line);
        }
    };

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        text = new TextView(this);
        text.setTextSize(16);
        setContentView(text);
        lm = getSystemService(LocationManager.class);
        Log.i(TAG, "feature location.gps = " + getPackageManager()
                .hasSystemFeature(PackageManager.FEATURE_LOCATION_GPS)
                + ", gps enabled = " + lm.isProviderEnabled(LocationManager.GPS_PROVIDER)
                + ", hw = " + lm.getGnssHardwareModelName() + " " + lm.getGnssYearOfHardware());
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION)
                != PackageManager.PERMISSION_GRANTED) {
            Log.e(TAG, "missing ACCESS_FINE_LOCATION");
            text.setText("missing ACCESS_FINE_LOCATION");
            return;
        }
        lm.registerGnssStatusCallback(getMainExecutor(), statusCallback);
        lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 0, getMainExecutor(), listener);
        Log.i(TAG, "request started");
    }

    @Override
    protected void onPause() {
        super.onPause();
        lm.removeUpdates(listener);
        lm.unregisterGnssStatusCallback(statusCallback);
        Log.i(TAG, "request stopped");
    }
}
