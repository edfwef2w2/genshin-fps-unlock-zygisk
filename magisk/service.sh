#!/system/bin/sh
MODDIR=${0%/*}
PKG=io.github.unlockfps

(
  until [ "$(getprop sys.boot_completed)" = "1" ]; do
    sleep 2
  done
  sleep 8
  if [ ! -f "$MODDIR/app.apk" ]; then
    exit 0
  fi
  if pm path "$PKG" >/dev/null 2>&1; then
    exit 0
  fi
  tmp=/data/local/tmp/genshin-fps-unlock.apk
  cp -f "$MODDIR/app.apk" "$tmp" || exit 0
  chmod 644 "$tmp"
  chcon u:object_r:apk_data_file:s0 "$tmp" 2>/dev/null || true
  pm install -r -g --user 0 "$tmp" >/dev/null 2>&1
  rm -f "$tmp"
) &
