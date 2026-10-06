#!/bin/sh
# Vérifie la syntaxe de tous les exemples Lua présents (luac -p : analyse seule, aucun fichier produit).
# Code 77 : test déclaré « sauté » par meson quand luac est absent.
LUAC=$(command -v luac5.4 || command -v luac) || exit 77
exec "$LUAC" -p "$1"/*.lua
