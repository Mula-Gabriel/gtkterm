-- 07_tcp_client.lua
-- Connexion TCP vers un serveur distant (ex. équipement réseau, ESP32, etc.)
-- Envoie des commandes et affiche les réponses.
--
-- Pour tester sans matériel : lancez  `nc -l 4000`  dans un terminal,
-- puis exécutez ce script.

local HOST = "127.0.0.1"
local PORT = 4000

gtkterm.log(string.format("Connexion TCP vers %s:%d ...", HOST, PORT))

-- Délai de connexion : 3 s (5 s par défaut). Échec : false et la raison (« timeout », connexion refusée…).
local ok, err = gtkterm.open(1, "tcp_client", HOST, PORT, 3000)
if not ok then
    gtkterm.log("Connexion échouée : " .. err)
    return
end
gtkterm.log("Connecté.")

-- Dialogue simple : envoyer une commande, attendre la réponse
local function query(cmd, expect, timeout_ms)
    gtkterm.log("TX : " .. cmd)
    local envoye, raison = gtkterm.send(1, cmd .. "\n")
    if not envoye then
        gtkterm.log("Envoi impossible : " .. raison)
        return nil
    end
    local r = gtkterm.wait_for(1, expect, timeout_ms or 3000)
    if r then
        gtkterm.log("RX : " .. r:gsub("[\r\n]+", " "))
    else
        gtkterm.log("TIMEOUT (attendait '" .. expect .. "')")
    end
    return r
end

query("HELLO",  "\n", 3000)
query("STATUS", "\n", 3000)
query("VERSION","\n", 3000)

-- Boucle de polling : toutes les 2 s pendant 10 s
gtkterm.log("Polling toutes les 2 s (5 fois)...")
for i = 1, 5 do
    local envoye, raison = gtkterm.send(1, string.format("POLL %d\n", i))
    if not envoye then
        gtkterm.log("Envoi impossible : " .. raison)
        break
    end
    local r = gtkterm.wait_for(1, "\n", 2000)
    gtkterm.log(string.format("[%d/5] %s", i, r and r:gsub("[\r\n]","") or "timeout"))
    gtkterm.sleep(2000)
end

gtkterm.close(1)
gtkterm.log("Connexion fermée.")
