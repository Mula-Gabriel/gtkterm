-- 11_tcp_server.lua
-- Serveur TCP : attend un client, renvoie chaque ligne reçue (écho), s'arrête sur « quit » ou au bout de 60 s.
-- Pour tester : exécutez ce script, puis dans un terminal :  nc 127.0.0.1 5000
-- « Stop » dans le panneau de scripts arrête le script à tout moment ; le port est alors refermé automatiquement.

local PORT = 5000
local DUREE_MAX_S = 60

local ok, err = gtkterm.open(1, "tcp_server", PORT, "127.0.0.1")
if not ok then
    gtkterm.log("Ouverture du serveur impossible : " .. err)
    return
end
gtkterm.log(string.format("En écoute sur 127.0.0.1:%d (état : %s)", PORT, gtkterm.status(1)))

local debut = gtkterm.time()
local etat_precedent = gtkterm.status(1)
while gtkterm.time() - debut < DUREE_MAX_S do
    local etat = gtkterm.status(1)   -- "listening" sans client, "connected" avec un client
    if etat ~= etat_precedent then
        gtkterm.log("État : " .. etat)
        if etat == "connected" then
            gtkterm.send(1, "Bienvenue ! Tapez « quit » pour arrêter.\n")
        end
        etat_precedent = etat
    end
    if etat == "connected" then
        -- Attente courte : on revient vite voir si le client est parti
        local ligne = gtkterm.wait_for(1, "\n", 500)
        if ligne then
            local texte = ligne:gsub("[\r\n]+$", "")
            gtkterm.log("RX : " .. texte)
            if texte == "quit" then
                gtkterm.send(1, "Au revoir.\n")
                break
            end
            local envoye, raison = gtkterm.send(1, "echo: " .. texte .. "\n")
            if not envoye then
                gtkterm.log("Envoi impossible : " .. raison)   -- "not connected" si le client vient de partir
            end
        end
    else
        gtkterm.sleep(100)
    end
end

gtkterm.close(1)
gtkterm.log("Serveur fermé.")
