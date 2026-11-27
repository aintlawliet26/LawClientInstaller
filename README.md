# LawClient Installer

Native bootstrap installers for LawClient on Windows and Android.

## User flow

The Windows installer uses a custom LawClient UI and an NSIS-style install flow.

1. Choose the installation directory. Default: `%APPDATA%\\LawClient`.
2. Click **Install LawClient**.
3. The bootstrapper downloads the current LawClient NSIS installer from the LawClient API.
4. The complete installer is verified with SHA-256 before execution.
5. The verified NSIS installer runs silently with `/S` and receives the selected folder through `/D=`.
6. The finish screen shows **Open LawClient** (checked by default) and **Done**.
7. Clicking **Done** optionally starts LawClient and closes the installer.

The downloaded NSIS installer is stored only temporarily under `%TEMP%` and is deleted after setup finishes.

## Build

Windows with Visual Studio 2022 C++ tools:

```powershell
.\\build-local.ps1
```

Or push to GitHub and use `.github/workflows/build-windows.yml`. The workflow artifact contains `LawClientInstaller.exe` and its SHA-256 file.

## Security

- Manifest is HTTPS only.
- Installer URL is restricted to `lawclient.online` / `www.lawclient.online` over HTTPS.
- Full SHA-256 is checked before execution.
- The installer is never executed when the hash does not match.
- No Tauri, WebView, or Chromium runtime is bundled in this bootstrapper.

## Android installer

The separate Android bootstrapper supports Android 8 or later on ARM64 devices. It downloads the current stable LawClient APK, verifies its size, SHA-256, app ID, version code, and signing certificate, and opens the Android installer for confirmation. After an install, the bootstrapper offers to open LawClient.

Build it with JDK 17 and the Android SDK with platform 36 and Build Tools 35.0.0. Set `JAVA_HOME`, `ANDROID_HOME`, and `LAWCLIENT_ANDROID_STORE_PASSWORD`, then run:

```powershell
pwsh ./android/build-release.ps1 -KeyStore <keystore> -KeyAlias <alias>
```

Upload `out/LawClientInstaller.apk` as the Android download on the website. The APK uses its own package ID, `com.lawliet.lawclient.installer`. It reads the current LawClient version from the [stable Android release](https://github.com/aintlawliet26/LawClientInstaller/releases/tag/android-stable), so updating the main LawClient APK only requires updating that feed.
