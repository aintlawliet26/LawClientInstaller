# LawClient Installer

Small native Windows bootstrap installer for LawClient.

## Flow

1. GET `https://lawclient.online/api/v1/launcher/installer/windows-x86_64`
2. Read the latest **successful GitHub-published** LawClient NSIS release metadata.
3. Download the immutable HTTPS installer URL.
4. Verify the complete file against the advertised SHA-256.
5. Run the verified NSIS installer silently with `/S`.
6. Open `%LOCALAPPDATA%\\LawClient\\lawclient.exe`.
7. Future LawClient updates continue to use the existing commit-build self updater.

The bootstrapper itself does not bundle WebView/Tauri/Chromium or the LawClient installer payload, keeping the EXE small.

## Build

Windows with Visual Studio 2022 C++ tools:

```powershell
.\\build-local.ps1
```

Or push to GitHub and use `.github/workflows/build-windows.yml`. The workflow artifact contains only `LawClientInstaller.exe`.

## Security

- Manifest is HTTPS only.
- Installer URL is restricted to `lawclient.online` / `www.lawclient.online` over HTTPS.
- Full SHA-256 is checked before execution.
- The installer is never executed when the hash does not match.
