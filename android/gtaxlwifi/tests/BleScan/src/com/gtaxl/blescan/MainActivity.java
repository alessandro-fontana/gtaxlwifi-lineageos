/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.gtaxl.blescan;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.widget.TextView;

import java.util.HashSet;
import java.util.Set;

/**
 * A ten-second BLE scan, with the result on logcat (tag BleScan) and on
 * screen. It answers a single question: does an LE scan find anything on
 * this tablet?
 *
 * It also prints what the system DECLARES (hasSystemFeature) next to what it
 * DOES, because the two are separate here: without the feature the LE APIs
 * work anyway, and it is Play and the apps that look at the declaration.
 */
public class MainActivity extends Activity {

    private static final String TAG = "BleScan";
    private final Set<String> seen = new HashSet<>();
    private TextView text;
    private BluetoothLeScanner scanner;

    private final ScanCallback callback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            String addr = result.getDevice().getAddress();
            if (seen.add(addr)) {
                String line = addr + "  rssi=" + result.getRssi()
                        + "  name=" + result.getDevice().getName();
                Log.i(TAG, "found " + line);
                text.append("\n" + line);
            }
        }

        @Override
        public void onScanFailed(int errorCode) {
            Log.e(TAG, "scan failed, code " + errorCode);
            text.append("\nscan FAILED, code " + errorCode);
        }
    };

    @Override
    protected void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        text = new TextView(this);
        text.setTextSize(16);
        setContentView(text);

        boolean declared = getPackageManager()
                .hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE);
        Log.i(TAG, "hasSystemFeature(bluetooth_le) = " + declared);
        text.setText("feature bluetooth_le declared: " + declared);

        BluetoothManager bm = getSystemService(BluetoothManager.class);
        BluetoothAdapter adapter = bm == null ? null : bm.getAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            Log.e(TAG, "adapter missing or off");
            text.append("\nadapter missing or off");
            return;
        }

        scanner = adapter.getBluetoothLeScanner();
        Log.i(TAG, "getBluetoothLeScanner() = " + scanner);
        text.append("\ngetBluetoothLeScanner(): " + (scanner == null ? "null" : "ok"));
        if (scanner == null) return;

        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        scanner.startScan(null, settings, callback);
        Log.i(TAG, "scan started, ten seconds");
        text.append("\nscan started, ten seconds...");

        new Handler().postDelayed(() -> {
            scanner.stopScan(callback);
            Log.i(TAG, "scan finished: " + seen.size() + " distinct devices");
            text.append("\n\nfinished: " + seen.size() + " distinct devices");
        }, 10000);
    }
}
