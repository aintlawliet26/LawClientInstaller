# LawClient Installer

Small native Windows bootstrap installer for LawClient with a custom LawClient UI and an NSIS-style install flow.

## User flow

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
