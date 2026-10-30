#!/bin/bash
# Remove o driver Knup IM602 para Mac.
# Uso: sudo /Library/Printers/Knup/desinstalar.sh
if [ "$(id -u)" -ne 0 ]; then
  exec sudo "$0" "$@"
fi
lpadmin -x Knup_IM602 2>/dev/null
rm -f /Library/Printers/PPDs/Contents/Resources/Knup-IM602.ppd
rm -rf /Library/Printers/Knup
pkgutil --forget cc.jvpereira.knup-im602 >/dev/null 2>&1
echo "Driver Knup IM602 removido."
