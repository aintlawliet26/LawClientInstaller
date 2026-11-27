package com.lawliet.lawclient.installer;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.IOException;
import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class InstallerActivity extends Activity {
    private static final int SOURCE_PERMISSION = 1;
    private static final int INSTALL_APK = 2;
    private static final int BACKGROUND = Color.rgb(16, 18, 23);
    private static final int PRIMARY = Color.rgb(185, 217, 255);
    private final ExecutorService downloads = Executors.newSingleThreadExecutor();
    private TextView status;
    private Button action;
    private ProgressBar progress;
    private StableRelease pendingRelease;
    private File verifiedApk;
    private long installVersion;
    private boolean resumed;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        createLayout();
        if (!Arrays.asList(Build.SUPPORTED_ABIS).contains("arm64-v8a")) {
            show("LawClient requires an ARM64 Android device.", "Install LawClient", null);
        } else if (state != null && state.getLong("installVersion") > 0) {
            installVersion = state.getLong("installVersion");
            String cachedName = state.getString("installFile");
            if (cachedName != null && cachedName.matches("lawclient-\\d+\\.apk")) {
                verifiedApk = new File(new File(getCacheDir(), "verified-downloads"), cachedName);
            }
            show("Waiting for Android to finish installing LawClient…", "Installing…", null);
        } else {
            show("Download the latest stable build on this device.", "Install LawClient", this::checkRelease);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
    }

    @Override
    protected void onPause() {
        resumed = false;
        super.onPause();
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        state.putLong("installVersion", installVersion);
        if (verifiedApk != null) state.putString("installFile", verifiedApk.getName());
        super.onSaveInstanceState(state);
    }

    @Override
    protected void onDestroy() {
        downloads.shutdownNow();
        if (installVersion == 0) discardDownload();
        super.onDestroy();
    }

    private void checkRelease() {
        show("Checking the stable release…", "Checking…", null);
        progress.setIndeterminate(true);
        progress.setVisibility(View.VISIBLE);
        downloads.execute(() -> {
            try {
                StableRelease release = StableRelease.fetch();
                PackageInfo installed = installedPackage();
                if (installed != null && !StableRelease.isLawClient(installed)) {
                    throw new IOException("An app with the LawClient package name is already installed with a different signing certificate.");
                }
                onScreen(() -> {
                    if (installed != null && StableRelease.versionCode(installed) >= release.versionCode) {
                        readyToOpen("LawClient is already up to date.");
                    } else {
                        pendingRelease = release;
                        if (resumed) requestPermissionOrDownload();
                        else show("The release is ready to download.", "Continue", this::requestPermissionOrDownload);
                    }
                });
            } catch (Exception failure) {
                retry(failure);
            }
        });
    }

    private void requestPermissionOrDownload() {
        if (pendingRelease == null) {
            checkRelease();
            return;
        }
        if (getPackageManager().canRequestPackageInstalls()) {
            download(pendingRelease);
            return;
        }
        show("Allow app installs from LawClient Installer in Android Settings.", "Waiting for permission…", null);
        Intent intent = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName()));
        try {
            startActivityForResult(intent, SOURCE_PERMISSION);
        } catch (RuntimeException failure) {
            show("Allow app installs from LawClient Installer in Android Settings.", "Retry", this::requestPermissionOrDownload);
        }
    }

    private void download(StableRelease release) {
        show("Downloading LawClient…", "Downloading…", null);
        progress.setIndeterminate(false);
        progress.setProgress(0);
        progress.setVisibility(View.VISIBLE);
        downloads.execute(() -> {
            File apk = null;
            boolean handedOff = false;
            try {
                File downloadsDir = new File(getCacheDir(), "verified-downloads");
                if (!downloadsDir.isDirectory() && !downloadsDir.mkdirs()) throw new IOException("Could not prepare the download folder.");
                apk = File.createTempFile("lawclient-", ".apk", downloadsDir);
                release.download(apk, getPackageManager(), percent -> onScreen(() -> {
                    progress.setProgress(percent);
                    status.setText("Downloading LawClient… " + percent + "%");
                }));
                File ready = apk;
                handedOff = true;
                runOnUiThread(() -> {
                    if (isDestroyed()) {
                        ready.delete();
                        return;
                    }
                    verifiedApk = ready;
                    pendingRelease = release;
                    if (resumed) openInstaller();
                    else show("LawClient download verified.", "Continue installation", this::openInstaller);
                });
            } catch (Exception failure) {
                retry(failure);
            } finally {
                if (!handedOff && apk != null) apk.delete();
            }
        });
    }

    private void openInstaller() {
        if (verifiedApk == null || pendingRelease == null) {
            checkRelease();
            return;
        }
        try {
            Uri uri = FileProvider.getUriForFile(this, getPackageName() + ".downloads", verifiedApk);
            Intent install = new Intent(Intent.ACTION_VIEW);
            install.setDataAndType(uri, "application/vnd.android.package-archive");
            install.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            install.putExtra(Intent.EXTRA_RETURN_RESULT, true);
            installVersion = pendingRelease.versionCode;
            show("Confirm the installation in the Android installer.", "Installing…", null);
            startActivityForResult(install, INSTALL_APK);
        } catch (RuntimeException failure) {
            installVersion = 0;
            discardDownload();
            retry(failure);
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == SOURCE_PERMISSION) {
            if (getPackageManager().canRequestPackageInstalls()) {
                if (pendingRelease == null) checkRelease();
                else download(pendingRelease);
            } else {
                show("Enable app installs from LawClient Installer to continue.", "Allow installation", this::requestPermissionOrDownload);
            }
        } else if (request == INSTALL_APK) {
            PackageInfo installed = installedPackage();
            long expected = installVersion;
            installVersion = 0;
            discardDownload();
            if (StableRelease.isLawClient(installed) && StableRelease.versionCode(installed) >= expected) {
                readyToOpen("LawClient is installed.");
            } else {
                show("Android did not complete the installation.", "Retry", this::checkRelease);
            }
        }
    }

    private PackageInfo installedPackage() {
        try {
            return getPackageManager().getPackageInfo(StableRelease.PACKAGE, StableRelease.signatureFlags());
        } catch (PackageManager.NameNotFoundException ignored) {
            return null;
        }
    }

    private void readyToOpen(String message) {
        show(message, "Open LawClient", () -> {
            Intent launch = getPackageManager().getLaunchIntentForPackage(StableRelease.PACKAGE);
            if (launch != null) {
                startActivity(launch);
                finish();
            } else {
                show("LawClient could not be opened.", "Retry", this::checkRelease);
            }
        });
    }

    private void discardDownload() {
        if (verifiedApk != null) {
            verifiedApk.delete();
            verifiedApk = null;
        }
    }

    private void retry(Exception failure) {
        String message = failure.getMessage();
        onScreen(() -> show(message == null || message.isEmpty() ? "Could not prepare LawClient." : message,
            "Retry", this::checkRelease));
    }

    private void onScreen(Runnable task) {
        runOnUiThread(() -> {
            if (!isDestroyed()) task.run();
        });
    }

    private void show(String message, String label, Runnable next) {
        status.setText(message);
        action.setText(label);
        action.setEnabled(next != null);
        action.setOnClickListener(next == null ? null : view -> next.run());
        progress.setVisibility(View.GONE);
    }

    private void createLayout() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BACKGROUND);
        scroll.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets safe = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                view.setPadding(safe.left, safe.top, safe.right, safe.bottom);
            }
            return insets;
        });
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(Gravity.CENTER);
        content.setPadding(dp(24), dp(32), dp(24), dp(32));
        scroll.addView(content);

        ImageView icon = new ImageView(this);
        icon.setImageResource(R.drawable.lawclient_icon);
        content.addView(icon, new LinearLayout.LayoutParams(dp(80), dp(80)));

        TextView title = label("LawClient Installer", 28, Color.WHITE);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        title.setMaxWidth(dp(420));
        add(content, title, LinearLayout.LayoutParams.WRAP_CONTENT, 20);
        status = label("", 16, Color.rgb(192, 202, 219));
        status.setMaxWidth(dp(420));
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        add(content, status, LinearLayout.LayoutParams.WRAP_CONTENT, 12);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(100);
        progress.setProgressTintList(ColorStateList.valueOf(PRIMARY));
        add(content, progress, LinearLayout.LayoutParams.MATCH_PARENT, 12);

        action = new Button(this);
        action.setAllCaps(false);
        action.setTextSize(16);
        action.setMinWidth(dp(200));
        action.setMaxWidth(dp(320));
        action.setMinHeight(dp(52));
        action.setPadding(dp(22), dp(10), dp(22), dp(10));
        action.setTextColor(BACKGROUND);
        GradientDrawable button = new GradientDrawable();
        button.setColor(PRIMARY);
        button.setCornerRadius(dp(12));
        action.setBackground(button);
        add(content, action, LinearLayout.LayoutParams.WRAP_CONTENT, 24);
        setContentView(scroll);
    }

    private TextView label(String text, int size, int color) {
        TextView label = new TextView(this);
        label.setText(text);
        label.setTextSize(size);
        label.setTextColor(color);
        label.setGravity(Gravity.CENTER);
        return label;
    }

    private void add(LinearLayout parent, View view, int width, int marginTop) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(width, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(marginTop);
        parent.addView(view, params);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
