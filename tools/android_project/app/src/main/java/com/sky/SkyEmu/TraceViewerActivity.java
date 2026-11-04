package com.sky.SkyEmu;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;

/** Offline, app-local viewer for EmuCap GBA JSON recordings. */
public class TraceViewerActivity extends Activity {
    private static final int FILE_REQUEST = 602;
    private static final long MAX_TRACE_BYTES = 40L * 1024L * 1024L;
    private ValueCallback<Uri[]> fileCallback;
    private WebView webView;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        webView = new WebView(this);
        setContentView(webView);
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
                if (path == null) return;
                try {
                    File directory = new File(getExternalFilesDir(null),
                            "emucap-traces").getCanonicalFile();
                    File file = new File(path).getCanonicalFile();
                    if (!file.getPath().startsWith(directory.getPath() + File.separator)
                            || !file.getName().endsWith(".json")
                            || file.length() > MAX_TRACE_BYTES || !file.isFile()) {
                        throw new IllegalArgumentException("Invalid trace path or size.");
                    }
                    StringBuilder buffer = new StringBuilder((int) file.length());
                    try (BufferedReader reader = new BufferedReader(
                            new InputStreamReader(new FileInputStream(file), "UTF-8"))) {
                        char[] chunk = new char[8192];
                        int count;
                        while ((count = reader.read(chunk)) != -1) {
                            buffer.append(chunk, 0, count);
                            if (buffer.length() > MAX_TRACE_BYTES)
                                throw new IllegalArgumentException("Trace too large.");
                        }
                    }
                    view.evaluateJavascript("window.loadTraceFromAndroid(" +
                            JSONObject.quote(buffer.toString()) + ");", null);
                } catch (Exception error) {
                    Toast.makeText(TraceViewerActivity.this,
                            "Could not load recording: " + error.getMessage(),
                            Toast.LENGTH_LONG).show();
                }
            }
        });
        webView.loadUrl("file:///android_asset/emucap_trace_viewer.html");
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
