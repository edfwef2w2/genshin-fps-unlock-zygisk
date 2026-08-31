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

if [ -f "$MODPATH/app.apk" ]; then
  ui_print "- Installing companion app"
  pm install -r "$MODPATH/app.apk" >/dev/null 2>&1 || ui_print "! Install app.apk from the zip manually"
fi

ui_print "- Enable ZygiskNext (or ReZygisk/NeoZygisk) and reboot"
ui_print "- Use the companion app to change FPS"
