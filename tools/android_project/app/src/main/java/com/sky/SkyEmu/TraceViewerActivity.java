package com.sky.SkyEmu;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;
import android.widget.Button;
import android.widget.LinearLayout;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.util.Arrays;

/** Offline, app-local viewer for EmuCap GBA JSON recordings. */
public class TraceViewerActivity extends Activity {
    private static final int FILE_REQUEST = 602;
    private static final long MAX_TRACE_BYTES = 40L * 1024L * 1024L;
    private ValueCallback<Uri[]> fileCallback;
    private WebView webView;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        webView = new WebView(this);
        LinearLayout screen = new LinearLayout(this);
        screen.setOrientation(LinearLayout.VERTICAL);
        LinearLayout controls = new LinearLayout(this);
        Button openAction = new Button(this);
        openAction.setText("Saved action");
        openAction.setAllCaps(false);
        openAction.setOnClickListener(v -> pickSavedTrace(false));
        controls.addView(openAction, new LinearLayout.LayoutParams(0, -2, 1));
        Button openBaseline = new Button(this);
        openBaseline.setText("Saved baseline");
        openBaseline.setAllCaps(false);
        openBaseline.setOnClickListener(v -> pickSavedTrace(true));
        controls.addView(openBaseline, new LinearLayout.LayoutParams(0, -2, 1));
        screen.addView(controls, new LinearLayout.LayoutParams(-1, -2));
        screen.addView(webView, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(screen);
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setAllowFileAccess(true);
        settings.setAllowContentAccess(true);
        settings.setAllowFileAccessFromFileURLs(false);
        settings.setAllowUniversalAccessFromFileURLs(false);
        settings.setBlockNetworkLoads(true);

        webView.setWebChromeClient(new WebChromeClient() {
            @Override public boolean onShowFileChooser(WebView view,
                    ValueCallback<Uri[]> callback, FileChooserParams params) {
                if (fileCallback != null) fileCallback.onReceiveValue(null);
                fileCallback = callback;
                Intent choose = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                choose.setType("*/*");
                choose.addCategory(Intent.CATEGORY_OPENABLE);
                choose.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                try {
                    startActivityForResult(choose, FILE_REQUEST);
                } catch (Exception error) {
                    fileCallback.onReceiveValue(null);
                    fileCallback = null;
                    Toast.makeText(TraceViewerActivity.this,
                            "No file picker available", Toast.LENGTH_LONG).show();
                }
                return true;
            }
        });

        webView.setWebViewClient(new WebViewClient() {
            @Override public void onPageFinished(WebView view, String url) {
                String path = getIntent().getStringExtra("trace_path");
                if (path != null) loadTraceFile(new File(path), false);
            }
        });
        webView.loadUrl("file:///android_asset/emucap_trace_viewer.html");
    }

    private File traceDirectory() {
        File base = getExternalFilesDir(null);
        if (base == null) base = getFilesDir();
        return new File(base, "emucap-traces");
    }

    private void pickSavedTrace(boolean baseline) {
        File[] files = traceDirectory().listFiles(file ->
                file.isFile() && file.getName().endsWith(".json"));
        if (files == null || files.length == 0) {
            Toast.makeText(this, "No saved traces yet. Record an action first.",
                    Toast.LENGTH_LONG).show();
            return;
        }
        Arrays.sort(files, (left, right) ->
                Long.compare(right.lastModified(), left.lastModified()));
        int count = Math.min(files.length, 100);
        String[] labels = new String[count];
        for (int i = 0; i < count; i++)
            labels[i] = files[i].getName();
        new AlertDialog.Builder(this)
                .setTitle(baseline ? "Choose idle baseline" : "Choose action recording")
                .setItems(labels, (dialog, index) -> loadTraceFile(files[index], baseline))
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void loadTraceFile(File file, boolean baseline) {
        new Thread(() -> {
            try {
                File directory = traceDirectory().getCanonicalFile();
                File verified = file.getCanonicalFile();
                if (!verified.getPath().startsWith(directory.getPath() + File.separator)
                        || !verified.getName().endsWith(".json")
                        || !verified.isFile() || verified.length() > MAX_TRACE_BYTES) {
                    throw new IllegalArgumentException("Invalid trace path or file size.");
                }
                StringBuilder buffer = new StringBuilder((int) verified.length());
                try (BufferedReader reader = new BufferedReader(
                        new InputStreamReader(new FileInputStream(verified), "UTF-8"))) {
                    char[] chunk = new char[8192];
                    int count;
                    while ((count = reader.read(chunk)) != -1) {
                        buffer.append(chunk, 0, count);
                        if (buffer.length() > MAX_TRACE_BYTES)
                            throw new IllegalArgumentException("Trace too large.");
                    }
                }
                String script = (baseline
                        ? "window.loadBaselineFromAndroid(" : "window.loadTraceFromAndroid(")
                        + JSONObject.quote(buffer.toString()) + ");";
                runOnUiThread(() -> {
                    if (!isFinishing() && webView != null) webView.evaluateJavascript(script, null);
                });
            } catch (Exception error) {
                runOnUiThread(() -> Toast.makeText(this,
                        "Could not open trace: " + error.getMessage(),
                        Toast.LENGTH_LONG).show());
            }
        }, "EmuCap-Trace-Reader").start();
    }

    @Override protected void onActivityResult(int request, int result, Intent intent) {
        super.onActivityResult(request, result, intent);
        if (request == FILE_REQUEST && fileCallback != null) {
            Uri[] resultUris = WebChromeClient.FileChooserParams.parseResult(result, intent);
            fileCallback.onReceiveValue(resultUris);
            fileCallback = null;
        }
    }

    @Override protected void onDestroy() {
        if (fileCallback != null) {
            fileCallback.onReceiveValue(null);
            fileCallback = null;
        }
        if (webView != null) webView.destroy();
        super.onDestroy();
    }
}
