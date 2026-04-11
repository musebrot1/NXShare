# NXShare – Build Guide (Windows)

## What you need

1. **devkitPro** (free, official Nintendo Switch homebrew SDK)
2. **About 10 minutes** for the installation

---

## Step 1: Install devkitPro

1. Go to: https://github.com/devkitPro/installer/releases
2. Download the latest **`devkitProUpdater-X.X.X.exe`**
3. Run the installer as Administrator
4. When selecting components, check:
   - ✅ **Switch Development** (required)
   - ✅ **devkitARM** (sometimes needed)
5. Let everything install (takes about 5–10 minutes, downloads ~500 MB)
6. The default path is `C:\devkitPro` — leave it as is

---

## Step 2: Open the MSYS2 terminal

devkitPro automatically installs an MSYS2 environment.

1. Open the **Start Menu**
2. Search for **"MSYS2"** or **"devkitPro MSYS2"**
3. Launch it — a terminal window will open

---

## Step 3: Install additional packages

In the MSYS2 terminal:

```bash
pacman -Syu
pacman -S switch-dev
```

Press `Y` and Enter when prompted.

---

## Step 4: Build the project

Navigate to the project folder in the MSYS2 terminal:

```bash
# Replace "YourUsername" with your actual Windows username
cd /c/Users/YourUsername/Downloads/NXShare

# Build
make all
```

If everything worked, you will see at the end:
```
Built: NXShare.nro
```

---

## Step 5: Copy the app to your Switch

1. Insert your SD card into your PC (or use FTP via the ftpd sysmodule)
2. Copy `NXShare.nro` to the `switch/` folder on your SD card:
   ```
   SD:/switch/NXShare.nro
   ```
3. Safely eject the SD card and insert it into the Switch

---

## Step 6: Launch the app

1. Boot your Switch with **Atmosphère** as usual
2. Open the **Homebrew Launcher** (hold the Album button or use Title Override)
3. Launch **NXShare**
4. The screen will show an IP address, e.g. `http://192.168.1.42:8080`
5. Open that URL in a browser on your phone or PC — done! 🎉

---

## Troubleshooting

| Problem | Solution |
|--------|--------|
| `make: command not found` | devkitPro not installed correctly, reinstall |
| `DEVKITPRO not set` | Use the devkitPro MSYS2 terminal, not regular CMD |
| No network connection | Switch WiFi connected? Same router as PC/phone? |
| No media found | Any screenshots/videos on the Switch? Correct emuMMC path? |
| Compilation errors | In MSYS2 terminal: `pacman -S switch-dev` and try again |

---

## Compatibility

- ✅ Atmosphère 1.9.0 and above
- ✅ Firmware 19.x / 20.x
- ✅ SysMMC and emuMMC (album path is detected automatically)
- ✅ JPG screenshots and MP4 videos
- ✅ All modern browsers (Chrome, Firefox, Safari, Edge)

---

## Web app features

- 📷 Preview all screenshots directly in the browser
- 🎬 Play videos directly in the browser
- ⬇️ Download individual files with one click
- ✅ Select and download multiple files at once
- 🔍 Filter by screenshots or videos
- ⟳ Refresh the gallery in the browser
- 📱 Works on mobile browsers too
