SKIPUNZIP=0

ui_print "- Genshin FPS Unlock Zygisk v4.0.0"

if [ ! -f "$MODPATH/zygisk/arm64-v8a.so" ]; then
  ui_print "! arm64-v8a.so missing"
  abort "! This module only supports arm64-v8a"
fi

if [ ! -f "$MODPATH/config.json" ]; then
  ui_print "- Writing default config.json"
  cat > "$MODPATH/config.json" << 'EOF'
{
  "fps": 120,
  "powerSave": false,
  "delaySeconds": 8,
  "packages": [
    "com.miHoYo.Yuanshen",
    "com.miHoYo.GenshinImpact",
    "com.miHoYo.ys"
  ]
}
EOF
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644

PKG=io.github.unlockfps
install_companion() {
  local apk="$MODPATH/app.apk"
  local tmp="/data/local/tmp/genshin-fps-unlock.apk"
  [ -f "$apk" ] || return 1
  cp -f "$apk" "$tmp" || return 1
  chmod 644 "$tmp"
  chcon u:object_r:apk_data_file:s0 "$tmp" 2>/dev/null || true
  if pm install -r -g --user 0 "$tmp" >/dev/null 2>&1; then
    rm -f "$tmp"
    return 0
  fi
  pm uninstall --user 0 "$PKG" >/dev/null 2>&1
  if pm install -g --user 0 "$tmp" >/dev/null 2>&1; then
    rm -f "$tmp"
    return 0
  fi
  rm -f "$tmp"
  return 1
}

if [ -f "$MODPATH/app.apk" ]; then
  ui_print "- Installing companion app"
  if [ "$(getprop sys.boot_completed)" = "1" ] && install_companion; then
    ui_print "- Companion app installed"
  else
    ui_print "! Companion will install after reboot (or install app.apk from the zip)"
  fi
fi

ui_print "- Enable ZygiskNext (or ReZygisk/NeoZygisk) and reboot"
ui_print "- Use the companion app to change FPS"
