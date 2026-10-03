/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.gtaxl.stretchholepunch;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * A SurfaceView in the middle of a scrolling list. It shows what the hole
 * punch does when the overscroll spring stretches the container: with the
 * §45.8 patch the node is no longer promoted to a RenderLayer, and the hole
 * branch in RenderNodeDrawable.cpp:305 is no longer taken.
 *
 * The surface draws a grid: if the hole ended up in the wrong place, or the
 * surface did not follow the stretch, it is visible by eye and in a
 * screencap.
 */
public class MainActivity extends Activity implements SurfaceHolder.Callback {

    @Override
    protected void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        ScrollView scroller = new ScrollView(this);
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setBackgroundColor(0xFF202020);

        for (int i = 0; i < 10; i++) column.addView(row("row " + i));

        SurfaceView surface = new SurfaceView(this);
        surface.getHolder().addCallback(this);
        column.addView(surface, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 700));

        for (int i = 10; i < 40; i++) column.addView(row("row " + i));

        scroller.addView(column);
        setContentView(scroller);
    }

    private TextView row(String label) {
        TextView t = new TextView(this);
        t.setText(label);
        t.setTextSize(26);
        t.setPadding(32, 28, 32, 28);
        t.setTextColor(Color.WHITE);
        return t;
    }

    @Override public void surfaceCreated(SurfaceHolder h) { drawGrid(h); }
    @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int a) { drawGrid(h); }
    @Override public void surfaceDestroyed(SurfaceHolder h) { }

    private void drawGrid(SurfaceHolder holder) {
        Canvas c = holder.lockCanvas();
        if (c == null) return;
        c.drawColor(Color.rgb(0, 190, 255));
        Paint p = new Paint();
        p.setColor(Color.BLACK);
        p.setStrokeWidth(4);
        for (int x = 0; x <= c.getWidth(); x += 80) c.drawLine(x, 0, x, c.getHeight(), p);
        for (int y = 0; y <= c.getHeight(); y += 80) c.drawLine(0, y, c.getWidth(), y, p);
        holder.unlockCanvasAndPost(c);
    }
}
