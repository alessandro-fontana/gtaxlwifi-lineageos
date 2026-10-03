/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.gtaxl.p2pscan;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.net.wifi.p2p.WifiP2pDevice;
import android.net.wifi.p2p.WifiP2pDeviceList;
import android.net.wifi.p2p.WifiP2pManager;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.widget.TextView;

/**
 * Twenty seconds of Wi-Fi Direct discovery, with the result on logcat (tag
 * P2pScan) and on screen. It answers a single question: does P2P find
 * anything on this tablet?
 *
 * It also prints what the system DECLARES next to what it DOES - but here,
 * unlike BLE, the two cannot be separated: without the feature the service
 * does not exist at all, which is why the declaration was added first, as
 * an experiment.
 */
public class MainActivity extends Activity {

    private static final String TAG = "P2pScan";
    private WifiP2pManager manager;
    private WifiP2pManager.Channel channel;
    private TextView text;
    private BroadcastReceiver receiver;

    @Override
    protected void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        text = new TextView(this);
        text.setTextSize(16);
        setContentView(text);

        boolean declared = getPackageManager()
                .hasSystemFeature(PackageManager.FEATURE_WIFI_DIRECT);
        Log.i(TAG, "hasSystemFeature(wifi.direct) = " + declared);
        text.setText("feature wifi.direct: " + declared);

        manager = (WifiP2pManager) getSystemService(Context.WIFI_P2P_SERVICE);
        if (manager == null) {
            Log.e(TAG, "WifiP2pManager missing: the service did not start");
            text.append("\nWifiP2pManager: missing");
            return;
        }
        channel = manager.initialize(this, getMainLooper(), null);
        Log.i(TAG, "channel = " + channel);
        text.append("\nchannel: " + (channel == null ? "null" : "ok"));

        receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context c, Intent i) {
                if (!WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION.equals(i.getAction())) return;
                manager.requestPeers(channel, (WifiP2pDeviceList peers) -> {
                    Log.i(TAG, "known peers: " + peers.getDeviceList().size());
                    text.append("\n--- peer: " + peers.getDeviceList().size());
                    for (WifiP2pDevice d : peers.getDeviceList()) {
                        String line = d.deviceAddress + "  " + d.deviceName + "  status=" + d.status;
                        Log.i(TAG, "  " + line);
                        text.append("\n  " + line);
                    }
                });
            }
        };
        registerReceiver(receiver,
                new IntentFilter(WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION),
                Context.RECEIVER_NOT_EXPORTED);

        manager.discoverPeers(channel, new WifiP2pManager.ActionListener() {
            @Override public void onSuccess() {
                Log.i(TAG, "discoverPeers: started");
                text.append("\ndiscovery started, twenty seconds...");
            }
            @Override public void onFailure(int reason) {
                Log.e(TAG, "discoverPeers FAILED, code " + reason);
                text.append("\ndiscovery FAILED, code " + reason);
            }
        });

        new Handler().postDelayed(() -> {
            manager.stopPeerDiscovery(channel, null);
            Log.i(TAG, "discovery finished");
            text.append("\n\nfinished");
        }, 20000);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (receiver != null) unregisterReceiver(receiver);
    }
}
