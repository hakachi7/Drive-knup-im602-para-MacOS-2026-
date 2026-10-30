#!/bin/bash
# Cadastra a impressora Knup KP-IM602 (POS-80C) ligada na USB.
# Uso: sudo /Library/Printers/Knup/adicionar-impressora.sh
FILA="Knup_IM602"
PPD="/Library/Printers/PPDs/Contents/Resources/Knup-IM602.ppd"

if [ "$(id -u)" -ne 0 ]; then
  exec sudo "$0" "$@"
fi

URI=$(lpinfo -v 2>/dev/null | awk '{print $2}' | grep -i '^usb://' | grep -i -E 'POS-?80|Printer/POS' | head -1)
if [ -z "$URI" ]; then
  echo "Impressora nao encontrada na USB. Ligue a impressora, conecte o cabo e rode de novo:"
  echo "  sudo /Library/Printers/Knup/adicionar-impressora.sh"
  exit 1
fi

lpadmin -p "$FILA" -D "Knup IM602 80mm" -L "USB" -E -v "$URI" -P "$PPD" -o printer-is-shared=false
cupsenable "$FILA"; cupsaccept "$FILA"
echo "Impressora 'Knup IM602 80mm' pronta ($URI)."
