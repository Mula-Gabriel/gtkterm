-- 03_wait_for_demo.lua
-- Montre les différents cas de wait_for :
--   - succès : retourne la chaîne reçue jusqu'au pattern (inclus)
--   - timeout : retourne nil
--   - utilisation de read() pour lire tout ce qui est arrivé sans pattern
--
-- Ce script fonctionne sur le port principal (handle 0) :
-- envoyez du texte depuis l'autre côté pendant l'exécution.

gtkterm.log("=== Démo wait_for ===")
gtkterm.log("Envoyez 'hello\\n' depuis l'autre côté dans les 5 secondes...")

-- Cas 1 : attente avec succès attendu
local r = gtkterm.wait_for(0, "hello", 5000)
if r then
    gtkterm.log("Reçu (jusqu'à 'hello') : " .. r)
else
    gtkterm.log("Timeout — rien reçu.")
end

-- Cas 2 : timeout explicite (pattern improbable)
gtkterm.log("Test timeout (pattern 'XYZXYZ', attente 1 s)...")
local r2 = gtkterm.wait_for(0, "XYZXYZ", 1000)
gtkterm.log("Résultat timeout : " .. tostring(r2))  -- doit afficher 'nil'

-- Cas 3 : read() — lit tout ce qui est dans le buffer (sans pattern)
gtkterm.log("Envoyez n'importe quoi dans les 2 secondes...")
gtkterm.sleep(2000)
local buf = gtkterm.read(0)
if #buf > 0 then
    gtkterm.log("read() a récupéré " .. #buf .. " octet(s) : " .. buf:sub(1, 80))
else
    gtkterm.log("read() : buffer vide.")
end

-- Cas 4 : or "timeout" fonctionne maintenant correctement
gtkterm.log("Test du idiome 'r or timeout'...")
local r3 = gtkterm.wait_for(0, "world", 1000)
gtkterm.log(r3 or "timeout")   -- affiche la data OU "timeout", jamais 'true'

gtkterm.log("=== Fin démo ===")
