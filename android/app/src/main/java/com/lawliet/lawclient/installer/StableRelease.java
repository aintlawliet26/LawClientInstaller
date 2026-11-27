package com.lawliet.lawclient.installer;

import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.os.Build;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.function.IntConsumer;

final class StableRelease {
    static final String PACKAGE = "com.lawliet.lawclient.android";
    private static final String SIGNER = "76f0ce9a73819b7b6973b8519ae0676ce1f1d81c82c5cec3aa21dd7b0f95d881";
    private static final URI FEED = URI.create(
        "https://github.com/aintlawliet26/LawClientInstaller/releases/download/android-stable/update.json");
    private static final long MAX_APK_BYTES = 256L * 1024 * 1024;

    final long versionCode;
    final long size;
    final String sha256;
    private final URI asset;

    private StableRelease(long versionCode, long size, String sha256, URI asset) {
        this.versionCode = versionCode;
        this.size = size;
        this.sha256 = sha256;
        this.asset = asset;
    }

    static StableRelease fetch() throws Exception {
        HttpURLConnection connection = connect(FEED);
        try (InputStream input = connection.getInputStream()) {
            ByteArrayOutputStream body = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            for (int received; (received = input.read(buffer)) != -1;) {
                if (body.size() + received > 16 * 1024) throw new IOException("Invalid LawClient release information.");
                body.write(buffer, 0, received);
            }
            JSONObject json = new JSONObject(new String(body.toByteArray(), StandardCharsets.UTF_8));
            long code = json.getLong("versionCode");
            long size = json.getLong("size");
            String hash = json.getString("sha256");
            String asset = json.getString("asset");
            if (json.getInt("schema") != 1 || !PACKAGE.equals(json.getString("packageName"))
                || code < 1 || code > 2100000000 || size < 1 || size > MAX_APK_BYTES
                || !hash.matches("[a-f0-9]{64}") || !asset.equals("LawClient-Android-stable-arm64-" + code + ".apk")) {
                throw new IOException("Invalid LawClient release information.");
            }
            return new StableRelease(code, size, hash, FEED.resolve(asset));
        } finally {
            connection.disconnect();
        }
    }

    void download(File apk, PackageManager packages, IntConsumer progress) throws Exception {
        MessageDigest digest = sha256();
        HttpURLConnection connection = connect(asset);
        long total = 0;
        int reported = -1;
        try (InputStream input = connection.getInputStream(); OutputStream file = Files.newOutputStream(apk.toPath())) {
            byte[] buffer = new byte[64 * 1024];
            for (int received; (received = input.read(buffer)) != -1;) {
                if (Thread.currentThread().isInterrupted()) throw new IOException("Download canceled.");
                total += received;
                if (total > size) throw new IOException("The APK is larger than expected.");
                digest.update(buffer, 0, received);
                file.write(buffer, 0, received);
                int percent = (int) (total * 100 / size);
                if (percent != reported) {
                    progress.accept(percent);
                    reported = percent;
                }
            }
        } finally {
            connection.disconnect();
        }
        if (total != size || !sha256.equals(hex(digest.digest()))) throw new IOException("The APK checksum did not match.");
        PackageInfo info = packages.getPackageArchiveInfo(apk.getAbsolutePath(), signatureFlags());
        if (!isLawClient(info) || versionCode(info) != versionCode) {
            throw new IOException("The downloaded APK is not a signed LawClient release.");
        }
    }

    @SuppressWarnings("deprecation")
    static int signatureFlags() {
        return Build.VERSION.SDK_INT >= 28 ? PackageManager.GET_SIGNING_CERTIFICATES : PackageManager.GET_SIGNATURES;
    }

    @SuppressWarnings("deprecation")
    static long versionCode(PackageInfo info) {
        return Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode;
    }

    @SuppressWarnings("deprecation")
    static boolean isLawClient(PackageInfo info) {
        if (info == null || !PACKAGE.equals(info.packageName)) return false;
        Signature[] signatures = Build.VERSION.SDK_INT >= 28
            ? info.signingInfo == null ? null : info.signingInfo.getApkContentsSigners()
            : info.signatures;
        return signatures != null && signatures.length == 1
            && SIGNER.equals(hex(sha256().digest(signatures[0].toByteArray())));
    }

    private static HttpURLConnection connect(URI address) throws IOException {
        for (int redirect = 0; redirect < 6; redirect++) {
            String host = address.getHost();
            if (!"https".equals(address.getScheme()) || address.getUserInfo() != null
                || (address.getPort() != -1 && address.getPort() != 443)
                || !("github.com".equals(host) || "release-assets.githubusercontent.com".equals(host)
                    || "objects.githubusercontent.com".equals(host))) {
                throw new IOException("Invalid LawClient download address.");
            }
            HttpURLConnection connection = (HttpURLConnection) address.toURL().openConnection();
            connection.setInstanceFollowRedirects(false);
            connection.setUseCaches(false);
            connection.setConnectTimeout(15000);
            connection.setReadTimeout(30000);
            int status;
            String location;
            try {
                status = connection.getResponseCode();
                if (status == 200) return connection;
                location = connection.getHeaderField("Location");
            } catch (IOException failure) {
                connection.disconnect();
                throw failure;
            }
            connection.disconnect();
            if (status < 300 || status >= 400 || location == null) {
                throw new IOException("The LawClient download server returned HTTP " + status + ".");
            }
            address = address.resolve(location);
        }
        throw new IOException("Too many redirects while downloading LawClient.");
    }

    private static MessageDigest sha256() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException failure) {
            throw new IllegalStateException(failure);
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) {
            result.append(Character.forDigit((value >> 4) & 15, 16));
            result.append(Character.forDigit(value & 15, 16));
        }
        return result.toString();
    }
}
