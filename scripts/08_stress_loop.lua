-- 08_stress_loop.lua
-- Test de robustesse : envoie en boucle sur le port principal et vérifie l'écho.
-- Utile pour valider qu'une liaison série fonctionne sans perte de données.
-- Nécessite que le port soit configuré en loopback (RX=TX câblés ensemble)
-- ou qu'un équipement renvoyant l'écho soit connecté.

local N          = 50        -- nombre de cycles
local TIMEOUT_MS = 10      -- timeout echo par cycle
local DELAY_MS   = 100       -- pause entre cycles

local ok_count  = 0
local err_count = 0
local t_start   = gtkterm.time()

gtkterm.log(string.format("Démarrage stress-test : %d cycles, timeout %d ms", N, TIMEOUT_MS))
gtkterm.clear(0)

for i = 1, N do
    local msg = string.format("PING%04d\r\n", i)
    gtkterm.send(0, msg)

    local r = gtkterm.wait_for(0, "PING" .. string.format("%04d", i), TIMEOUT_MS)
    if r then
        ok_count = ok_count + 1
    else
        err_count = err_count + 1
        gtkterm.log(string.format("ERREUR cycle %d — pas d'écho", i))
    end

    -- Afficher la progression tous les 10 cycles
    if i % 10 == 0 then
        gtkterm.log(string.format("  %d/%d  OK=%d ERR=%d", i, N, ok_count, err_count))
    end

    gtkterm.sleep(DELAY_MS)
end

local elapsed = gtkterm.time() - t_start
gtkterm.log(string.format("=== Résultats ==="))
gtkterm.log(string.format("  Cycles   : %d", N))
gtkterm.log(string.format("  Succès   : %d (%.0f%%)", ok_count, ok_count / N * 100))
gtkterm.log(string.format("  Erreurs  : %d", err_count))
gtkterm.log(string.format("  Durée    : %.1f s", elapsed))

-- Sauvegarder le résumé
gtkterm.save_csv("/tmp/stress_result.csv",
    os.date("%Y-%m-%d %H:%M:%S"), N, ok_count, err_count,
    string.format("%.1f", elapsed))
gtkterm.log("Résultat ajouté dans /tmp/stress_result.csv")
