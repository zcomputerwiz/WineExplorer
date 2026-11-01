#!/bin/bash
set -euo pipefail

DISP=":99"
EVIDENCE="/tmp/evidence-ui"
mkdir -p "$EVIDENCE"
rm -f "$EVIDENCE"/step*.png

echo "[1] Starting Xvfb on $DISP..."
pkill -9 -f "Xvfb $DISP" || true
Xvfb "$DISP" -screen 0 1024x768x24 -ac >/dev/null 2>&1 &
XPID=$!
sleep 2
DISPLAY="$DISP" xhost + >/dev/null 2>&1

echo "[2] Preparing prefix, stage, and clean receipt state..."
sudo rm -rf /tmp/wexsetup-wine
sudo cp -a /etc/skel/.wine /tmp/wexsetup-wine
sudo chown -R wexsetup:wexsetup /tmp/wexsetup-wine
sudo rm -f /var/lib/wineexplorer/component-receipts/corefonts.json /var/lib/wineexplorer/component-receipts/d3dcompiler_47.json
sudo rm -f /etc/wineexplorer/setup/stage-oobe-done

sudo pkill -f 'wex-components --oobe-coordinator' || true
sudo rm -rf /run/wex-oobe
sudo mkdir -p /run/wex-oobe /etc/wineexplorer/setup
echo oobe | sudo tee /run/wex-oobe/stage /etc/wineexplorer/setup/stage >/dev/null
sudo chmod 644 /run/wex-oobe/stage /etc/wineexplorer/setup/stage
sudo chown -R root:root /run/wex-oobe

echo "[3] Starting OOBE coordinator..."
nohup sudo /usr/local/bin/wex-components --oobe-coordinator >/dev/null 2>&1 &
sleep 2

echo "[4] Launching oobe.exe in Wine Desktop..."
nohup sudo -u wexsetup env DISPLAY="$DISP" WINEPREFIX=/tmp/wexsetup-wine WINEDEBUG=-all WEX_OOBE_SOCKET=/run/wex-oobe/coordinator.sock /usr/local/bin/wine explorer /desktop=OOBE,1024x768 oobe.exe >/dev/null 2>&1 &
WPID=$!
sleep 6

WID=$(DISPLAY="$DISP" xdotool search --name "OOBE - Wine Desktop" | head -n 1)
echo "    Found Wine Desktop window ID: $WID"

send_key() {
    DISPLAY="$DISP" xdotool windowfocus "$WID"
    DISPLAY="$DISP" xdotool key --window "$WID" "$1"
    sleep 2
}

echo "[5] Step 1: Welcome Page..."
DISPLAY="$DISP" import -window root "$EVIDENCE/step1_welcome.png"

echo "[6] Step 2: Updates Page..."
send_key "alt+n"
DISPLAY="$DISP" import -window root "$EVIDENCE/step2_updates.png"

echo "[7] Step 3: Connectivity Page..."
send_key "alt+n"
DISPLAY="$DISP" import -window root "$EVIDENCE/step3_connectivity.png"

echo "[8] Step 4: Components Catalog Page..."
send_key "alt+n"
DISPLAY="$DISP" import -window root "$EVIDENCE/step4_components.png"

echo "[9] Step 5: Open Review Terms for CoreFonts..."
send_key "Down"
send_key "alt+r"
sleep 2
DISPLAY="$DISP" import -window root "$EVIDENCE/step5_review_dialog.png"

echo "[10] Step 6: Accept CoreFonts Terms..."
send_key "alt+a"
sleep 1
DISPLAY="$DISP" import -window root "$EVIDENCE/step6_review_accepted.png"
send_key "Return"
sleep 3

echo "[11] Step 6b: Open Review Terms for d3dcompiler_47..."
send_key "Down"
sleep 1
send_key "Down"
sleep 1
send_key "alt+r"
sleep 3
DISPLAY="$DISP" import -window root "$EVIDENCE/step6b_d3dcompiler_review.png"

echo "[12] Step 6c: Accept d3dcompiler_47 Terms..."
send_key "alt+a"
sleep 1
DISPLAY="$DISP" import -window root "$EVIDENCE/step6c_d3dcompiler_accepted.png"
send_key "Return"
sleep 3
DISPLAY="$DISP" import -window root "$EVIDENCE/step7_components_checked.png"

echo "[13] Step 7: Trigger Install..."
send_key "alt+n"
sleep 2
DISPLAY="$DISP" import -window root "$EVIDENCE/step8_installing.png"

echo "     Waiting for installation receipts and worker completion..."
for i in $(seq 1 150); do
    if [ -f /var/lib/wineexplorer/component-receipts/corefonts.json ] && [ -f /var/lib/wineexplorer/component-receipts/d3dcompiler_47.json ]; then
        echo "     Both receipts detected after ${i}x2 seconds!"
        break
    fi
    sleep 2
done
# Allow worker thread to post WM_OOBE_INSTALL_DONE and settle
sleep 8
DISPLAY="$DISP" import -window root "$EVIDENCE/step9_installed.png"

echo "[12] Step 8: Users Page..."
# Focus 'Your name:' (IDC_USER1) via Alt+Y and enter TestUser
send_key "alt+y"
sleep 1
DISPLAY="$DISP" xdotool type --window "$WID" "TestUser"
sleep 2
DISPLAY="$DISP" import -window root "$EVIDENCE/step10_users.png"

echo "[13] Step 9: Advance to Password Page..."
send_key "alt+n"
sleep 2
DISPLAY="$DISP" import -window root "$EVIDENCE/step11_password.png"

echo "[14] Step 10: Advance to Finish Page..."
send_key "alt+n"
sleep 2
DISPLAY="$DISP" import -window root "$EVIDENCE/step12_finish.png"

echo "[15] Step 11: Complete OOBE Setup..."
send_key "Return"
sleep 3
if [ -f /etc/wineexplorer/setup/stage-oobe-done ]; then
    echo "     OOBE stage-oobe-done signal detected!"
fi

echo "[15] Cleaning up..."
kill "$WPID" 2>/dev/null || true
sudo -u wexsetup DISPLAY="$DISP" WINEPREFIX=/tmp/wexsetup-wine /usr/local/bin/wineserver -k 2>/dev/null || true
sudo pkill -f 'wex-components --oobe-coordinator' || true
kill "$XPID" 2>/dev/null || true

echo "=== All screenshots captured ==="
mkdir -p /media/sf_WineExplorer/evidence-ui
cp -f "$EVIDENCE"/step*.png /media/sf_WineExplorer/evidence-ui/ || true
sha256sum "$EVIDENCE"/step*.png
